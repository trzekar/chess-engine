#include "uci.h"
#include "movegen.h"
#include "position.h"
#include "search.h"
#include "timeman.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <charconv>
#include <chrono>
#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::string_view ENGINE_NAME = "chess-engine";
constexpr std::string_view ENGINE_AUTHOR = "Karol";

// Every line the engine writes, from either thread, goes through send: the mutex keeps two lines
// from interleaving, and the flush delivers each line at once. Through a pipe std::cout is block
// buffered, so without the flush the GUI could see `bestmove` only after its clock ran out.
class Output {
public:
    explicit Output(std::ostream &out) : out_(out) {}

    void send(const std::string &line) {
        const std::lock_guard<std::mutex> lock(mutex_);
        out_ << line << '\n';
        out_.flush();
    }

private:
    std::ostream &out_;
    std::mutex mutex_;
};

// Mate scores are MATE minus the plies to the mate. UCI wants moves: a mate the engine gives in
// N moves is `mate N`, a mate it receives in N moves is `mate -N`.
std::string score_text(int score) {
    if (score >= MATE - MAX_PLY)
        return "mate " + std::to_string((MATE - score + 1) / 2);
    if (score <= -(MATE - MAX_PLY))
        return "mate " + std::to_string(-(MATE + score) / 2);
    return "cp " + std::to_string(score);
}

// "0000" is the UCI name for no move: the position has no legal moves.
std::string move_text(Move m) { return m == Move() ? "0000" : to_uci(m); }

std::string info_line(const FinishedIteration &it) {
    const auto ms = it.time.count();
    // A depth that took under a millisecond still reports a speed instead of dividing by zero.
    const uint64_t nps = it.result.nodes * 1000 / uint64_t(std::max<decltype(ms)>(ms, 1));
    std::string line = "info depth " + std::to_string(it.depth) + " score " +
                       score_text(it.result.score) + " nodes " + std::to_string(it.result.nodes) +
                       " nps " + std::to_string(nps) + " time " + std::to_string(ms);
    // The search keeps no principal variation yet, so pv holds the best move alone.
    if (it.result.best_move != Move())
        line += " pv " + to_uci(it.result.best_move);
    return line;
}

// Reads a whole number, as the values of `go` are; anything else gives nothing. Some GUIs send a
// negative time once the clock has run out, so negative values count as 0.
std::optional<int64_t> parse_number(const std::string &word) {
    int64_t value = 0;
    const char *end = word.data() + word.size();
    const auto [ptr, ec] = std::from_chars(word.data(), end, value);
    if (ec != std::errc() || ptr != end)
        return std::nullopt;
    return std::max<int64_t>(value, 0);
}

class Uci {
public:
    explicit Uci(std::ostream &out) : out_(out) {
        [[maybe_unused]] const bool ok = parse_fen(START_FEN, pos_);
        assert(ok);
    }

    // A running search is stopped and joined first, so a joinable thread is never destroyed:
    // that would call std::terminate.
    ~Uci() { stop_search(); }

    // Returns false on `quit`.
    bool handle(const std::string &line) {
        std::istringstream words(line);
        std::string command;
        words >> command;

        if (command == "uci") {
            out_.send("id name " + std::string(ENGINE_NAME));
            out_.send("id author " + std::string(ENGINE_AUTHOR));
            out_.send("uciok");
        } else if (command == "isready") {
            // Answered by this thread, so also while the search thread is thinking.
            out_.send("readyok");
        } else if (command == "ucinewgame") {
            // Nothing carries over between games yet; the transposition table will (step 4.3).
            stop_search();
        } else if (command == "position") {
            stop_search();
            set_position(words);
        } else if (command == "go") {
            stop_search();
            start_search(words);
        } else if (command == "stop") {
            stop_search();
        } else if (command == "quit") {
            stop_search();
            return false;
        }
        return true;
    }

private:
    // position [startpos | fen <six fields>] [moves m1 m2 ...]. A bad FEN keeps the previous
    // position; the moves are played up to the first illegal one.
    void set_position(std::istringstream &words) {
        std::string word;
        words >> word;

        std::string fen;
        if (word == "startpos") {
            fen = START_FEN;
            words >> word;
        } else if (word == "fen") {
            while (words >> word && word != "moves")
                fen += (fen.empty() ? "" : " ") + word;
        } else {
            out_.send("info string expected startpos or fen");
            return;
        }

        Position pos;
        if (!parse_fen(fen, pos)) {
            out_.send("info string invalid fen: " + fen);
            return;
        }

        // `word` is now "moves" or the last word read; only "moves" starts a move list.
        if (word == "moves") {
            while (words >> word) {
                const Move m = parse_uci_move(pos, word);
                Position next = pos;
                if (m == Move() || !next.make_move(m)) {
                    out_.send("info string illegal move: " + word);
                    break;
                }
                pos = next;
            }
        }
        pos_ = pos;
    }

    // go [wtime btime winc binc movestogo depth movetime infinite]. Other parameters (nodes,
    // mate, ponder, searchmoves) are ignored. Without any limit the engine thinks until `stop`.
    void start_search(std::istringstream &words) {
        SearchLimits limits;
        std::optional<int64_t> time[COLOR_NB];
        std::optional<int64_t> increment[COLOR_NB];
        std::optional<int64_t> movetime;
        bool infinite = false;

        std::string word;
        while (words >> word) {
            std::string value;
            if (word == "infinite") {
                infinite = true;
            } else if (word == "wtime" && words >> value) {
                time[WHITE] = parse_number(value);
            } else if (word == "btime" && words >> value) {
                time[BLACK] = parse_number(value);
            } else if (word == "winc" && words >> value) {
                increment[WHITE] = parse_number(value);
            } else if (word == "binc" && words >> value) {
                increment[BLACK] = parse_number(value);
            } else if (word == "movetime" && words >> value) {
                movetime = parse_number(value);
            } else if (word == "depth" && words >> value) {
                if (const std::optional<int64_t> depth = parse_number(value))
                    limits.depth = int(std::clamp<int64_t>(*depth, 1, MAX_PLY));
            }
            // movestogo is read past like any unknown word: the budget assumes 20 more moves,
            // which never spends more than the clock holds.
        }

        const Color us = pos_.side_to_move;
        if (movetime) {
            limits.time = milliseconds(*movetime);
        } else if (time[us] && !infinite) {
            limits.time =
                time_for_move(milliseconds(*time[us]), milliseconds(increment[us].value_or(0)));
        }

        // think may have set the flag at the end of the previous search; it must start clear.
        stop_ = false;
        search_thread_ = std::thread([this, pos = pos_, limits, infinite] {
            const SearchResult result =
                think(pos, limits, stop_,
                      [this](const FinishedIteration &it) { out_.send(info_line(it)); });
            // In `go infinite` the answer waits for `stop`, even when the search ended earlier
            // (a mate found, MAX_PLY reached): UCI allows bestmove only after stop there.
            while (infinite && !stop_)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            out_.send("bestmove " + move_text(result.best_move));
        });
    }

    void stop_search() {
        if (!search_thread_.joinable())
            return;
        stop_ = true;
        search_thread_.join();
    }

    Output out_;
    Position pos_;
    // Written by this thread (stop) and by the search thread (time out), read by the search.
    std::atomic<bool> stop_{false};
    std::thread search_thread_;
};

} // namespace

void uci_loop(std::istream &in, std::ostream &out) {
    Uci uci(out);
    std::string line;
    // The end of the input counts as quit: the destructor stops and joins a running search.
    while (std::getline(in, line)) {
        // Lines from Windows GUIs may end in "\r\n".
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!uci.handle(line))
            break;
    }
}