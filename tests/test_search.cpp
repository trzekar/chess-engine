#include "attacks.h"
#include "eval.h"
#include "movegen.h"
#include "search.h"
#include "uci.h"
#include <algorithm>
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
// counts. Search results are compared with reference_search in the same positions.
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

// A mate in N ends 2N - 1 plies from the root, with the mated side to move. negamax_root
// evaluates at depth 0 without looking for moves, so it needs one more ply: depth 2N. search
// finds the mate already at depth 2N - 1: quiescence search sees that the side in check has no
// legal move. Its score is MATE minus the plies to the mate at both depths.
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

        for (const int d : {depth - 1, depth}) {
            const SearchResult ab = search(pos, d);
            const std::string context = c.fen + "\ndepth " + std::to_string(d);
            require(ab.score == score, "search: wrong mate score", context, ab);
            require(to_uci(ab.best_move) == c.best, "search: wrong first move", context, ab);
        }

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

// An independent search over the same tree as search: alpha-beta to `depth`, then quiescence
// search with the same rules. Out of check the side to move may stand pat on eval and plays only
// captures and queen promotions; in check it plays every move, cannot stand pat, and with no
// legal move is mated. The implementation differs on purpose: fail-hard instead of fail-soft,
// and its own move ordering (reference_order). Neither changes the score of the root searched
// with the full window, so it must equal the score of search. A move lost or duplicated by the
// ordering code of search, or a wrong bound in its quiescence search, makes the two differ.

// Most valuable victim first, then least valuable attacker, quiet moves last in generator
// order: std::stable_sort on a key, unlike the selection in search.cpp. Only for speed: the
// score does not depend on the order, but without it quiescence search in Kiwipete takes
// minutes.
static void reference_order(const Position &pos, MoveList &list) {
    const auto key = [&pos](Move m) {
        if (m.flag() == EN_PASSANT_CAPTURE)
            return 100 + 10 * PAWN - PAWN;
        if (m.is_capture())
            return 100 + 10 * pos.piece_at(m.to()) - pos.piece_at(m.from());
        return m.flag() == QUEEN_PROMOTION ? 100 + 10 * QUEEN - PAWN : 0;
    };
    std::stable_sort(list.moves, list.moves + list.size,
                     [&key](Move a, Move b) { return key(a) > key(b); });
}

static int reference_quiescence(const Position &pos, int alpha, int beta, int ply) {
    if (ply >= MAX_PLY)
        return std::clamp(eval(pos), alpha, beta);

    const bool in_check = pos.in_check(pos.side_to_move);
    if (!in_check) {
        const int stand_pat = eval(pos);
        if (stand_pat >= beta)
            return beta;
        alpha = std::max(alpha, stand_pat);
    }

    MoveList list;
    if (in_check)
        generate_moves(pos, list);
    else
        generate_captures(pos, list);
    reference_order(pos, list);

    bool any_legal = false;
    for (Move m : list) {
        Position next = pos;
        if (!next.make_move(m))
            continue;
        any_legal = true;
        const int score = -reference_quiescence(next, -beta, -alpha, ply + 1);
        if (score >= beta)
            return beta;
        alpha = std::max(alpha, score);
    }

    if (in_check && !any_legal)
        return std::clamp(-MATE + ply, alpha, beta);
    return alpha;
}

static int reference_alphabeta(const Position &pos, int alpha, int beta, int depth, int ply) {
    if (depth <= 0)
        return reference_quiescence(pos, alpha, beta, ply);

    MoveList list;
    generate_moves(pos, list);
    reference_order(pos, list);

    bool any_legal = false;
    for (Move m : list) {
        Position next = pos;
        if (!next.make_move(m))
            continue;
        any_legal = true;
        const int score = -reference_alphabeta(next, -beta, -alpha, depth - 1, ply + 1);
        if (score >= beta)
            return beta;
        alpha = std::max(alpha, score);
    }

    if (!any_legal)
        return std::clamp(pos.in_check(pos.side_to_move) ? -MATE + ply : 0, alpha, beta);
    return alpha;
}

// The score of the position searched to `depth` with the full window.
static int reference_search(const Position &pos, int depth) {
    return reference_alphabeta(pos, -INF, INF, depth, 0);
}

// search returns the score of reference_search, and its move is one that reaches that score:
// its child, searched one ply shallower, scores the same from the other side. The move itself
// may differ from the first best move in generator order, as move ordering breaks ties.
static void test_against_reference() {
    for (const Tree &t : TREES) {
        const Position pos = position_from(t.fen);
        for (int depth = 1; depth <= 3; ++depth) {
            const int expected = reference_search(pos, depth);
            const SearchResult ab = search(pos, depth);
            const std::string context = std::string(t.fen) + "\ndepth " + std::to_string(depth) +
                                        ", reference_search score " + std::to_string(expected);
            require(ab.score == expected, "search: score differs from reference_search", context,
                    ab);

            Position next = pos;
            require(next.make_move(ab.best_move), "search: illegal best move", context, ab);
            const int move_score = -reference_alphabeta(next, -INF, INF, depth - 1, 1);
            require(move_score == expected, "search: the best move does not reach the score",
                    context, ab);
        }
    }
}

// Positions where a search that stops at the depth limit goes wrong, each searched to depth 1:
// the root move is followed directly by quiescence search.
static void test_quiescence() {
    // A queen that takes a defended pawn is lost to the recapture. Without quiescence search
    // d1d5 wins a pawn at depth 1.
    {
        const std::string fen = "4k3/8/4p3/3p4/8/8/8/3QK3 w - - 0 1";
        const SearchResult r = search(position_from(fen), 1);
        require(r.best_move != Move(D1, D5, CAPTURE), "quiescence: queen takes a defended pawn",
                fen, r);
        require(r.score > 500, "quiescence: a queen up should score as one", fen, r);
    }

    // Material is scored where no capture is left: a queen up is far from a draw, for either
    // side, and far from a mate. An empty capture list is not a stalemate.
    for (const std::string fen :
         {"4k3/8/8/8/8/8/8/Q3K3 w - - 0 1", "q3k3/8/8/8/8/8/8/4K3 b - - 0 1"}) {
        for (int depth = 1; depth <= 3; ++depth) {
            const SearchResult r = search(position_from(fen), depth);
            require(r.score > 800 && r.score < MATE - MAX_PLY,
                    "quiescence: a queen up is neither a draw nor a mate", fen, r);
        }
    }

    // Back rank: Ra8 mates, and the mate is seen at depth 1, after the root move.
    {
        const std::string fen = "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1";
        const SearchResult r = search(position_from(fen), 1);
        require(r.best_move == Move(A1, A8, QUIET) && r.score == MATE - 1,
                "quiescence: missed a mate in 1 at depth 1", fen, r);
    }

    // The same with h7 empty: after Ra8+ the king escapes to h7, so a side in check with a
    // quiet way out is not mated.
    {
        const std::string fen = "6k1/5pp1/8/8/8/8/8/R5K1 w - - 0 1";
        const SearchResult r = search(position_from(fen), 1);
        require(r.score < MATE - MAX_PLY, "quiescence: a check with an escape scored as mate", fen,
                r);
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
            think(pos, {}, limits, stop, [&](const FinishedIteration &it) { seen.push_back(it); });

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
    const SearchResult r = think(pos, {}, SearchLimits{}, stop,
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
        const SearchResult r = think(pos, {}, limits, stop,
                                     [&](const FinishedIteration &it) { last_depth = it.depth; });
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
    const SearchResult r = think(position_from(fen), {}, SearchLimits{}, stop,
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
    test_against_reference();
    test_quiescence();
    test_think_depth_limit();
    test_think_stopped_at_once();
    test_think_time_limit();
    test_think_no_legal_moves();
    std::cout << "test_search: OK\n";
    return 0;
}
