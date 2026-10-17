#include "attacks.h"
#include "eval.h"
#include "movegen.h"
#include "search.h"
#include "uci.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// Usage: test_search <mate_positions.epd>
//
// Output goes through iostream rather than printf: MinGW's printf does not accept %llu
// under -Wformat=2 -Werror.

// The perft positions of tests/perft_positions.epd. negamax_root visits every node once, so
// at depth 3 its node count is perft(0) + perft(1) + perft(2) + perft(3) of the published
// counts. Search results are compared with negamax_root in the same positions.
struct Tree {
    const char *fen;
    uint64_t nodes_to_depth_3;
};

static const Tree TREES[] = {
    // 1 + 20 + 400 + 8902
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 9323},
    // 1 + 48 + 2039 + 97862
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 99950},
    // 1 + 14 + 191 + 2812
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 3018},
    // 1 + 6 + 264 + 9467
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 9738},
    {"r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 9738},
    // 1 + 44 + 1486 + 62379
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 63910},
    // 1 + 46 + 2079 + 89890
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 92016},
};

struct MateCase {
    std::string fen;
    int moves = 0;    // dm: mate in this many moves
    std::string best; // bm: the only first move that mates that fast, in UCI notation
};

static std::string_view trim(std::string_view s) {
    const size_t first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

// Parses "<fen> ;dm <N> ;bm <move>". Returns false on a malformed line.
static bool parse_mate_case(std::string_view line, MateCase &c) {
    const size_t dm = line.find(";dm ");
    const size_t bm = line.find(";bm ");
    if (dm == std::string_view::npos || bm == std::string_view::npos || bm < dm)
        return false;

    c.fen = std::string(trim(line.substr(0, dm)));
    const std::string_view moves = trim(line.substr(dm + 4, bm - dm - 4));
    const char *end = moves.data() + moves.size();
    const auto [ptr, ec] = std::from_chars(moves.data(), end, c.moves);
    c.best = std::string(trim(line.substr(bm + 4)));
    return ec == std::errc() && ptr == end && c.moves >= 1 && !c.best.empty();
}

static std::string describe(const SearchResult &r) {
    const std::string move = r.best_move == Move() ? "0000" : to_uci(r.best_move);
    return "move " + move + ", score " + std::to_string(r.score) + ", nodes " +
           std::to_string(r.nodes);
}

// On a failure, prints the position and what the search returned before failing. Messages go
// to stderr: it is unbuffered, so they survive the abort even when ctest pipes the output.
static void require(bool ok, const char *what, const std::string &fen, const SearchResult &r) {
    if (ok)
        return;
    std::cerr << fen << '\n' << what << ": " << describe(r) << '\n';
    std::abort();
}

static Position position_from(const std::string &fen) {
    Position pos;
    if (!parse_fen(fen, pos)) {
        std::cerr << "invalid FEN: " << fen << '\n';
        std::abort();
    }
    return pos;
}

// At depth 0 the search returns the evaluation without looking for moves, so the mated
// position, 2N - 1 plies from the root, needs one ply of depth left: a mate in N is found at
// depth 2N. Its score is MATE minus the plies to the mate.
static int test_mates(const char *path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "cannot open " << path << '\n';
        std::abort();
    }

    int cases = 0;
    std::string line;
    while (std::getline(file, line)) {
        const std::string_view text = trim(line);
        if (text.empty() || text[0] == '#')
            continue;
        MateCase c;
        if (!parse_mate_case(text, c)) {
            std::cerr << "malformed line: " << line << '\n';
            std::abort();
        }

        const Position pos = position_from(c.fen);
        const int depth = 2 * c.moves;
        const int score = MATE - (2 * c.moves - 1);

        const SearchResult ab = search(pos, depth);
        require(ab.score == score, "search: wrong mate score", c.fen, ab);
        require(to_uci(ab.best_move) == c.best, "search: wrong first move", c.fen, ab);

        // negamax_root takes seconds for a mate in 3 under the sanitizers, so only the mates
        // in 2 are checked with it.
        if (c.moves == 2) {
            const SearchResult nm = negamax_root(pos, depth);
            require(nm.score == score, "negamax_root: wrong mate score", c.fen, nm);
            require(to_uci(nm.best_move) == c.best, "negamax_root: wrong first move", c.fen, nm);
        }
        ++cases;
    }
    return cases;
}

// Without legal moves at the root there is no move to return: Move(), and the score of the
// game's end from the point of view of the side to move.
static void test_no_legal_moves() {
    struct Case {
        const char *fen;
        int score;
    };
    static const Case CASES[] = {
        // Fool's mate: White is checkmated.
        {"rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3", -MATE},
        // 1. e4 g5 2. d4 f6 3. Qh5#: Black is checkmated.
        {"rnbqkbnr/ppppp2p/5p2/6pQ/3PP3/8/PPP2PPP/RNB1KBNR b KQkq - 1 3", -MATE},
        // Stalemate with Black to move, then with White to move.
        {"7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 0},
        {"8/8/8/8/8/6q1/5k2/7K w - - 0 1", 0},
    };

    for (const Case &c : CASES) {
        const Position pos = position_from(c.fen);
        for (int depth = 1; depth <= 3; ++depth) {
            const SearchResult nm = negamax_root(pos, depth);
            const SearchResult ab = search(pos, depth);
            for (const SearchResult &r : {nm, ab}) {
                require(r.best_move == Move(), "a move without legal moves", c.fen, r);
                require(r.score == c.score, "wrong score without legal moves", c.fen, r);
                require(r.nodes == 1, "only the root is a node", c.fen, r);
            }
        }
    }
}

static void test_negamax_node_counts() {
    for (const Tree &t : TREES) {
        const SearchResult nm = negamax_root(position_from(t.fen), 3);
        require(nm.nodes == t.nodes_to_depth_3, "negamax_root: node count is not the perft sum",
                t.fen, nm);
    }
}

// With the full window alpha-beta returns the minimax score, and with the same move order
// and ties broken by the first move, the same move. At depth 1 it visits exactly the same
// nodes: the root never cuts and its children are leaves. From depth 2 it visits fewer: once
// the root has raised alpha, replies that refute a later root move cut the rest of its
// subtree. A root that never raises alpha would still find the right score, only slower.
// This holds while alpha-beta cutoffs are the only pruning: quiescence search, null move and
// reductions (steps 4.1, 4.5, 4.6) end the comparison.
static void test_against_negamax() {
    for (const Tree &t : TREES) {
        const Position pos = position_from(t.fen);
        for (int depth = 1; depth <= 3; ++depth) {
            const SearchResult nm = negamax_root(pos, depth);
            const SearchResult ab = search(pos, depth);
            const std::string context = std::string(t.fen) + "\ndepth " + std::to_string(depth) +
                                        ", negamax_root " + describe(nm);
            require(ab.score == nm.score, "search: score differs from negamax_root", context, ab);
            require(ab.best_move == nm.best_move, "search: move differs from negamax_root", context,
                    ab);
            if (depth == 1)
                require(ab.nodes == nm.nodes, "search: node count differs at depth 1", context, ab);
            else
                require(ab.nodes < nm.nodes, "search: no cutoff", context, ab);
        }
    }
}

// With only a depth limit, think runs the same iterations as search: the last one returns the
// score and move of search(pos, depth), and the node count adds up the counts of
// search(pos, 1), ..., search(pos, depth), since the counter runs on across iterations. The
// callback sees every depth once, in order, with the clock never going back.
static void test_think_depth_limit() {
    for (const Tree &t : TREES) {
        const Position pos = position_from(t.fen);
        std::atomic<bool> stop{false};
        std::vector<FinishedIteration> seen;
        SearchLimits limits;
        limits.depth = 3;
        const SearchResult r =
            think(pos, limits, stop, [&](const FinishedIteration &it) { seen.push_back(it); });

        uint64_t nodes = 0;
        for (int depth = 1; depth <= 3; ++depth)
            nodes += search(pos, depth).nodes;
        const SearchResult ab = search(pos, 3);
        require(r.score == ab.score && r.best_move == ab.best_move,
                "think: differs from search at the same depth", t.fen, r);
        require(r.nodes == nodes, "think: node count is not the sum over its iterations", t.fen, r);
        require(!stop, "think: set the stop flag without a time limit", t.fen, r);

        require(seen.size() == 3, "think: one callback per depth", t.fen, r);
        for (size_t i = 0; i < seen.size(); ++i) {
            require(seen[i].depth == int(i) + 1, "think: depths out of order", t.fen,
                    seen[i].result);
            require(i == 0 || seen[i].time >= seen[i - 1].time, "think: the clock went back", t.fen,
                    seen[i].result);
        }
        require(seen.back().result.best_move == r.best_move && seen.back().result.nodes == r.nodes,
                "think: the result is not the last iteration", t.fen, r);
    }
}

// A flag set before the call still gives a move: depth 1 always completes, and nothing after it.
static void test_think_stopped_at_once() {
    const std::string fen(START_FEN);
    const Position pos = position_from(fen);
    std::atomic<bool> stop{true};
    std::vector<int> depths;
    const SearchResult r = think(pos, SearchLimits{}, stop,
                                 [&](const FinishedIteration &it) { depths.push_back(it.depth); });
    require(depths == std::vector<int>{1}, "think: a set flag must stop after depth 1", fen, r);
    require(r.best_move == search(pos, 1).best_move, "think: not the depth-1 move", fen, r);
}

// The budget is kept, with a margin wide enough for the sanitizers. In the start position no
// budget here lasts long enough to reach MAX_PLY, so each one ends by the clock: think sets the
// flag itself. A budget of 0 still gives the depth-1 move.
static void test_think_time_limit() {
    const std::string fen(START_FEN);
    const Position pos = position_from(fen);
    for (const int budget : {0, 50, 200}) {
        std::atomic<bool> stop{false};
        SearchLimits limits;
        limits.time = std::chrono::milliseconds(budget);
        int last_depth = 0;
        const auto start = std::chrono::steady_clock::now();
        const SearchResult r =
            think(pos, limits, stop, [&](const FinishedIteration &it) { last_depth = it.depth; });
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - start)
                              .count();
        require(took <= budget + 400, "think: overran its time budget", fen, r);
        require(stop, "think: the clock ran out but the flag is clear", fen, r);
        require(last_depth >= 1 && r.best_move != Move(), "think: no move after a time limit", fen,
                r);
    }
}

// Without a legal move think returns at once, even without limits: one iteration, no move.
static void test_think_no_legal_moves() {
    const std::string fen = "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3";
    std::atomic<bool> stop{false};
    int calls = 0;
    const SearchResult r = think(position_from(fen), SearchLimits{}, stop,
                                 [&](const FinishedIteration &) { ++calls; });
    require(r.best_move == Move() && r.score == -MATE && calls == 1,
            "think: a checkmated root gives Move(), -MATE and one iteration", fen, r);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: test_search <mate_positions.epd>\n";
        return 2;
    }
    init_attacks();
    init_eval();

    const int mates = test_mates(argv[1]);
    if (mates != 10) {
        std::cerr << "expected 10 mate positions, read " << mates << '\n';
        return 1;
    }
    test_no_legal_moves();
    test_negamax_node_counts();
    test_against_negamax();
    test_think_depth_limit();
    test_think_stopped_at_once();
    test_think_time_limit();
    test_think_no_legal_moves();
    std::cout << "test_search: OK\n";
    return 0;
}
