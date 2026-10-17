#include "attacks.h"
#include "eval.h"
#include "movegen.h"
#include "position.h"
#include "timeman.h"
#include "uci.h"
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <istream>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Drives uci_loop as a GUI does: the engine runs on its own thread, commands arrive one at a
// time while it thinks, and every line it writes is recorded with the moment it was written.
//
// Timing checks leave a wide margin (MARGIN) so that they also pass under the sanitizers and on
// a loaded CI machine; they catch a search that ignores the clock, not one that is a little late.

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr milliseconds MARGIN{400};

// The engine's input: a stream that blocks until the test sends the next line.
class Feed : public std::streambuf {
public:
    void send(const std::string &line) {
        const std::lock_guard<std::mutex> lock(mutex_);
        pending_ += line + '\n';
        ready_.notify_one();
    }

    void close() {
        const std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        ready_.notify_one();
    }

protected:
    // Called by the engine's thread when it has read everything handed over so far.
    int_type underflow() override {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return !pending_.empty() || closed_; });
        if (pending_.empty())
            return traits_type::eof();
        current_.swap(pending_);
        pending_.clear();
        setg(current_.data(), current_.data(), current_.data() + current_.size());
        return traits_type::to_int_type(*gptr());
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::string pending_;
    std::string current_;
    bool closed_ = false;
};

struct Line {
    std::string text;
    Clock::time_point at;
};

// The engine's output: complete lines with the moment each one ended.
class Recorder : public std::streambuf {
public:
    std::vector<Line> lines() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return lines_;
    }

protected:
    // No put area is set, so every character arrives here.
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof()))
            return traits_type::not_eof(c);
        const std::lock_guard<std::mutex> lock(mutex_);
        if (traits_type::to_char_type(c) == '\n') {
            lines_.push_back({partial_, Clock::now()});
            partial_.clear();
        } else {
            partial_ += traits_type::to_char_type(c);
        }
        return c;
    }

private:
    std::mutex mutex_;
    std::string partial_;
    std::vector<Line> lines_;
};

static bool starts_with(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

static void fail(const std::string &what, const std::vector<Line> &lines) {
    std::cerr << what << "\nengine output:\n";
    for (const Line &l : lines)
        std::cerr << "  " << l.text << '\n';
    std::abort();
}

class Session {
public:
    Session() : in_(&feed_), out_(&recorder_), engine_([this] { uci_loop(in_, out_); }) {}

    ~Session() {
        if (engine_.joinable())
            finish();
    }

    Clock::time_point send(const std::string &command) {
        const Clock::time_point now = Clock::now();
        feed_.send(command);
        return now;
    }

    // Waits for the first line after `from` that starts with `prefix` and returns it.
    Line wait_for(std::string_view prefix, size_t from = 0,
                  milliseconds limit = milliseconds(5000)) {
        const Clock::time_point deadline = Clock::now() + limit;
        while (Clock::now() < deadline) {
            const std::vector<Line> all = recorder_.lines();
            for (size_t i = from; i < all.size(); ++i)
                if (starts_with(all[i].text, prefix))
                    return all[i];
            std::this_thread::sleep_for(milliseconds(2));
        }
        fail("no line starting with \"" + std::string(prefix) + "\"", recorder_.lines());
        return {};
    }

    size_t line_count() { return recorder_.lines().size(); }
    std::vector<Line> lines() { return recorder_.lines(); }

    // Sends quit and waits until uci_loop has returned.
    void finish() {
        feed_.send("quit");
        feed_.close();
        engine_.join();
    }

private:
    Feed feed_;
    Recorder recorder_;
    std::istream in_;
    std::ostream out_;
    std::thread engine_;
};

static Position position_after(const char *fen, const std::vector<std::string> &moves) {
    Position pos;
    if (!parse_fen(fen, pos))
        std::abort();
    for (const std::string &uci : moves) {
        const Move m = parse_uci_move(pos, uci);
        Position next = pos;
        if (m == Move() || !next.make_move(m))
            std::abort();
        pos = next;
    }
    return pos;
}

// The move in "bestmove <move>" must be legal in `pos`.
static void require_legal_bestmove(const Line &line, const Position &pos,
                                   const std::vector<Line> &all) {
    const std::string uci = line.text.substr(std::string_view("bestmove ").size());
    const Move m = parse_uci_move(pos, uci);
    Position next = pos;
    if (m == Move() || !next.make_move(m))
        fail("illegal bestmove: " + line.text, all);
}

static long long ms_between(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<milliseconds>(b - a).count();
}

// time_for_move: clock / 20 + increment / 2, never closer than 50 ms to the end of the clock
// and never under 1 ms. The expected budgets are worked out by hand from that rule.
static void test_time_budget() {
    const auto require = [](int clock, int increment, long long expected) {
        const long long got = time_for_move(milliseconds(clock), milliseconds(increment)).count();
        if (got != expected) {
            std::cerr << "time_for_move(" << clock << " ms, " << increment << " ms) = " << got
                      << " ms, expected " << expected << " ms\n";
            std::abort();
        }
    };
    require(8000, 80, 440); // 8000 / 20 + 80 / 2
    require(1000, 80, 90);  // 1000 / 20 + 80 / 2
    require(60, 80, 10);    // 3 + 40 = 43, capped at 60 - 50
    require(30, 80, 1);     // the cap 30 - 50 is negative: the floor of 1 ms wins
    require(0, 0, 1);
}

static void test_handshake() {
    Session s;
    s.send("uci");
    s.wait_for("uciok");
    const std::vector<Line> lines = s.lines();
    if (lines.size() < 3 || !starts_with(lines[0].text, "id name ") ||
        !starts_with(lines[1].text, "id author ") || lines.back().text != "uciok")
        fail("uci must answer id name, id author, then uciok", lines);

    // Unknown commands are ignored; the engine still answers.
    s.send("setoption name Hash value 16");
    s.send("xyzzy");
    s.send("isready");
    s.wait_for("readyok", lines.size());
}

// Every completed depth prints one info line, in order, and the search ends with a legal move.
static void test_go_depth() {
    Session s;
    s.send("position startpos moves e2e4 e7e5");
    s.send("go depth 4");
    const Line best = s.wait_for("bestmove");
    const std::vector<Line> lines = s.lines();

    int expected = 1;
    for (const Line &l : lines) {
        if (!starts_with(l.text, "info depth "))
            continue;
        const std::string prefix = "info depth " + std::to_string(expected) + " score cp ";
        if (!starts_with(l.text, prefix) || l.text.find(" nodes ") == std::string::npos ||
            l.text.find(" nps ") == std::string::npos ||
            l.text.find(" time ") == std::string::npos || l.text.find(" pv ") == std::string::npos)
            fail("bad info line: " + l.text, lines);
        ++expected;
    }
    if (expected != 5)
        fail("go depth 4 must print info for depths 1 to 4", lines);

    require_legal_bestmove(best, position_after(START_FEN.data(), {"e2e4", "e7e5"}), lines);
}

// movetime and the clock: the move comes within the budget plus MARGIN, and not long before it
// (the start position never runs out of depths to search in this time).
static void test_time_limits() {
    Session s;
    s.send("position startpos");

    Clock::time_point sent = s.send("go movetime 300");
    Line best = s.wait_for("bestmove");
    long long took = ms_between(sent, best.at);
    if (took < 250 || took > 300 + MARGIN.count())
        fail("go movetime 300 answered after " + std::to_string(took) + " ms", s.lines());

    // 2000 ms on the clock, no increment: the budget is 2000 / 20 = 100 ms.
    size_t from = s.line_count();
    sent = s.send("go wtime 2000 btime 2000 winc 0 binc 0");
    best = s.wait_for("bestmove", from);
    took = ms_between(sent, best.at);
    if (took > 100 + MARGIN.count())
        fail("a 100 ms budget answered after " + std::to_string(took) + " ms", s.lines());
    require_legal_bestmove(best, position_after(START_FEN.data(), {}), s.lines());

    // Almost no time left: depth 1 still completes and gives a legal move.
    from = s.line_count();
    s.send("go wtime 5 btime 5");
    best = s.wait_for("bestmove", from);
    require_legal_bestmove(best, position_after(START_FEN.data(), {}), s.lines());
}

// go infinite thinks until stop; isready is answered meanwhile; bestmove comes only after stop.
static void test_infinite_and_stop() {
    Session s;
    s.send("position startpos");
    s.send("go infinite");
    s.send("isready");
    s.wait_for("readyok");

    std::this_thread::sleep_for(milliseconds(200));
    for (const Line &l : s.lines())
        if (starts_with(l.text, "bestmove"))
            fail("bestmove before stop in go infinite", s.lines());

    const Clock::time_point sent = s.send("stop");
    const Line best = s.wait_for("bestmove");
    if (ms_between(sent, best.at) > MARGIN.count())
        fail("stop answered after " + std::to_string(ms_between(sent, best.at)) + " ms", s.lines());
    require_legal_bestmove(best, position_after(START_FEN.data(), {}), s.lines());

    // A finished search in go infinite also waits for stop: here the root has no legal move.
    const size_t from = s.line_count();
    s.send("position fen rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    s.send("go infinite");
    std::this_thread::sleep_for(milliseconds(100));
    for (size_t i = from; i < s.lines().size(); ++i)
        if (starts_with(s.lines()[i].text, "bestmove"))
            fail("bestmove before stop in go infinite", s.lines());
    s.send("stop");
    if (s.wait_for("bestmove", from).text != "bestmove 0000")
        fail("a checkmated root has no move: bestmove 0000", s.lines());
}

// quit during a search stops it; uci_loop returns.
static void test_quit_while_thinking() {
    Session s;
    s.send("go infinite");
    std::this_thread::sleep_for(milliseconds(50));
    const Clock::time_point sent = Clock::now();
    s.finish();
    if (ms_between(sent, Clock::now()) > MARGIN.count())
        fail("quit took too long", s.lines());
}

// Mate scores are printed in moves, from the engine's point of view.
static void test_mate_scores() {
    Session s;
    // Mate in 2 for White (tests/mate_positions.epd, position 1).
    s.send("position fen r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1");
    s.send("go depth 4");
    Line best = s.wait_for("bestmove");
    if (best.text != "bestmove d5f6")
        fail("mate in 2 not found", s.lines());
    if (s.wait_for("info depth 4").text.find(" score mate 2 ") == std::string::npos)
        fail("depth 4 must report score mate 2", s.lines());

    // The same position after 1. Nf6+ gxf6: White mates in 1.
    size_t from = s.line_count();
    s.send("position fen r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1 "
           "moves d5f6 g7f6");
    s.send("go depth 2");
    s.wait_for("bestmove", from);
    if (s.wait_for("info depth 2", from).text.find(" score mate 1 ") == std::string::npos)
        fail("depth 2 must report score mate 1", s.lines());

    // Black to move after 1. Nf6+, mated in 1 whatever it plays: score mate -1.
    from = s.line_count();
    s.send("position fen r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1 "
           "moves d5f6");
    s.send("go depth 3");
    s.wait_for("bestmove", from);
    if (s.wait_for("info depth 3", from).text.find(" score mate -1 ") == std::string::npos)
        fail("depth 3 must report score mate -1", s.lines());
}

// Bad input changes nothing: the previous position stays and the engine keeps answering.
static void test_bad_position() {
    Session s;
    s.send("position startpos moves e2e4");
    s.send("position fen this is not a fen");
    s.wait_for("info string invalid fen");
    size_t from = s.line_count();
    s.send("go depth 1");
    Line best = s.wait_for("bestmove", from);
    require_legal_bestmove(best, position_after(START_FEN.data(), {"e2e4"}), s.lines());

    // Moves are played up to the first illegal one.
    s.send("position startpos moves e2e4 e7e5 e1e8 g1f3");
    s.wait_for("info string illegal move: e1e8");
    from = s.line_count();
    s.send("go depth 1");
    best = s.wait_for("bestmove", from);
    require_legal_bestmove(best, position_after(START_FEN.data(), {"e2e4", "e7e5"}), s.lines());
}

int main() {
    init_attacks();
    init_eval();
    test_time_budget();
    test_handshake();
    test_go_depth();
    test_time_limits();
    test_infinite_and_stop();
    test_quit_while_thinking();
    test_mate_scores();
    test_bad_position();
    std::cout << "test_uci: OK\n";
    return 0;
}