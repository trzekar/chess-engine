#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "position.h"
#include "tt.h"
#include "types.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace {
// The clock is read every 2048 nodes only: a fraction of a millisecond between checks in
// Release, far below the 50 ms margin of timeman.h.
constexpr uint64_t CHECK_TIME_INTERVAL = 2048;

// Move ordering bands, highest first, wide apart so that no band can reach into the next:
//   the move from the transposition table       TT_MOVE_SCORE
//   captures and the quiet queen promotion      CAPTURE_SCORE + MVV-LVA (0 to 45)
//   the two killers of the ply                  KILLER_1_SCORE, KILLER_2_SCORE
//   other quiet moves                           their history, below MAX_HISTORY_SCORE
// Without the capture band KxP would score 0, the same as a quiet move.
constexpr int TT_MOVE_SCORE = 2'000'000;
constexpr int CAPTURE_SCORE = 1'000'000;
constexpr int KILLER_1_SCORE = 900'000;
constexpr int KILLER_2_SCORE = 800'000;

// History grows by depth * depth at every quiet cutoff. When an entry reaches this, the side's
// whole table is halved: the order between its moves stays, old information fades, and the
// values stay far below the killers.
constexpr int MAX_HISTORY_SCORE = 100'000;

// Passed where killers do not apply: at the root, which has no siblings, and in quiescence.
constexpr Move NO_KILLERS[2] = {};

// Extra reduction of the null move search, on top of the usual ply. 2 is the classic value;
// R = 3 or a depth-dependent R has not been tested yet.
constexpr int NULL_MOVE_REDUCTION = 2;

// With fewer plies left a pass saves little. Also keeps the depth-3 searches the tests
// compare with reference_search free of passes.
constexpr int MIN_NULL_MOVE_DEPTH = 3;

// Late move reductions: a quiet move late in the list is first searched this many plies
// shallower, and again at full depth only if it beats alpha.
constexpr int LATE_MOVE_REDUCTION = 1;

// The first legal move that may be reduced, counted from 1.
constexpr int FIRST_REDUCED_MOVE = 4;

// No move is reduced with fewer plies left, so searches to depth 3, which the tests compare
// with reference_search, are never reduced.
constexpr int MIN_REDUCTION_DEPTH = 3;

struct SearchState {
    uint64_t nodes = 0;
    std::atomic<bool> *stop_flag = nullptr;
    std::chrono::steady_clock::time_point end_time = {};
    bool is_time_limited = false;
    // Hashes of the game before the root, then one per ply of the current line: the root at
    // root_index, a node at root_index + ply. A node overwrites its own index on entry, so
    // every index below it belongs to one of its ancestors.
    std::vector<uint64_t> hash_stack = {};
    int root_index = 0;
    // Two quiet moves per ply that caused a cutoff, the newer first.
    // MAX_PLY + 1 rows are enough: nothing extends the search, so ply stays below the depth.
    Move killers[MAX_PLY + 1][2] = {};
    // Every quiet cutoff also counts in the history of its side, weighted by the
    // depth left: a cutoff high in the tree saves far more than one near the leaves.
    int history[COLOR_NB][64][64] = {};
};

SearchState state;

// int, not size_t: the repetition scan counts down and steps below 0 before it stops.
uint64_t &stack_at(int i) {
    assert(i >= 0);
    return state.hash_stack[static_cast<size_t>(i)];
}

// The stop flag of a search without limits: nobody ever sets it.
std::atomic<bool> never_stop{false};

// Bands as above. The en passant victim is a pawn although `to` is empty, and a quiet queen
// promotion is ordered like a pawn taking a queen.
int score_move(const Position &pos, Move m, Move tt_move, const Move killers[2]) {
    if (m == tt_move) {
        return TT_MOVE_SCORE;
    }

    if (m == killers[0]) {
        return KILLER_1_SCORE;
    } else if (m == killers[1]) {
        return KILLER_2_SCORE;
    }

    if (m.is_capture()) {
        const PieceType victim = m.flag() == EN_PASSANT_CAPTURE ? PAWN : pos.piece_at(m.to());
        const PieceType attacker = pos.piece_at(m.from());
        return CAPTURE_SCORE + 10 * victim + (KING - attacker);
    }

    if (m.flag() == QUEEN_PROMOTION) {
        return CAPTURE_SCORE + 10 * QUEEN + (KING - PAWN);
    }

    return state.history[pos.side_to_move][m.from()][m.to()];
}

// `tt_move` is Move() when there is none.
void score_moves(const Position &pos, const MoveList &list, Move tt_move, int scores[],
                 const Move killers[2]) {
    for (int i = 0; i < list.size; i++) {
        scores[i] = score_move(pos, list.moves[i], tt_move, killers);
    }
}

// One step of selection sort: the best remaining move goes to index i, its score with it.
// After a cutoff the rest is never sorted, which beats sorting the whole list up front.
void pick_next(MoveList &list, int scores[], int i) {
    int max = scores[i];
    int best_ind = i;

    for (int k = i + 1; k < list.size; k++) {
        if (scores[k] > max) {
            max = scores[k];
            best_ind = k;
        }
    }

    std::swap(scores[i], scores[best_ind]);
    std::swap(list.moves[i], list.moves[best_ind]);
}

// Quiescence search: past the horizon, plays captures and queen promotions until the position
// is quiet, so eval never scores the middle of an exchange. In check there is no stand pat and
// every evasion is searched; out of check a stalemate goes unnoticed.
int quiescence(const Position &pos, int alpha, int beta, int ply) {
    state.nodes++;
    if (state.nodes % CHECK_TIME_INTERVAL == 0 && state.is_time_limited) {
        if (std::chrono::steady_clock::now() > state.end_time) {
            *state.stop_flag = true;
        }
    }

    // Stopped: the caller ignores this value.
    if (*state.stop_flag) {
        return 0;
    }

    // Captures run out on their own, check-evasion chains need not. Also keeps -MATE + ply
    // inside the range the TT treats as a mate.
    if (ply >= MAX_PLY) {
        return eval(pos);
    }

    int scores[256];
    int best = -INF;

    MoveList list;

    const bool in_check = pos.in_check(pos.side_to_move);

    if (!in_check) {
        // Stand pat: the side to move need not capture, and some quiet move usually keeps at
        // least the static score, so eval is a lower bound.
        best = eval(pos);
        if (best > alpha) {
            alpha = best;
        }
        if (best >= beta) {
            return best;
        }
        generate_captures(pos, list);
    } else {
        generate_moves(pos, list);
    }

    score_moves(pos, list, Move(), scores, NO_KILLERS);

    for (int i = 0; i < list.size; i++) {
        Position next = pos;
        pick_next(list, scores, i);
        Move m = list.moves[i];

        if (!next.make_move(m)) {
            continue;
        }

        int score = -quiescence(next, -beta, -alpha, ply + 1);

        if (*state.stop_flag) {
            return 0;
        }

        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
            }
        }

        if (score >= beta) {
            return score;
        }
    }

    // In check every move was tried and none was legal.
    if (best == -INF && in_check) {
        return -MATE + ply;
    }

    return best;
}

int negamax(const Position &pos, int depth, int ply) {
    state.nodes++;

    if (depth <= 0) {
        return eval(pos);
    }

    int max = -INF;

    MoveList list;
    generate_moves(pos, list);

    for (Move m : list) {
        Position next = pos;
        if (!next.make_move(m)) {
            continue;
        }

        int score = -negamax(next, depth - 1, ply + 1);
        if (score > max) {
            max = score;
        }
    }

    // No legal move. `+ ply` scores a nearer mate higher, so the shortest one is preferred.
    if (max == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            return -MATE + ply;
        } else {
            return 0;
        }
    }

    return max;
}

// Fail-soft alpha-beta. `depth` is the number of plies left, `ply` the distance from the root
// (mate scores, killers, hash stack). `null_allowed` is false right after a null move.
int alphabeta(const Position &pos, int alpha, int beta, int depth, int ply, bool null_allowed) {
    const int current = state.root_index + ply;
    const int oldest = std::max(0, current - pos.half_move_counter);
    const int alpha_orig = alpha;
    stack_at(current) = pos.hash;
    const Color us = pos.side_to_move;

    // One repetition, on this line or in the game, already scores as a draw: if repeating was
    // good once, it is good again. Only every second ply can match, at the earliest 4 plies
    // back, and nothing before the last capture or pawn move.
    if (ply > 0) {
        for (int i = current - 4; i >= oldest; i -= 2) {
            if (pos.hash == stack_at(i)) {
                return 0;
            }
        }
    }

    // The fifty-move rule: 100 plies without a capture or pawn move are a draw. A checkmate
    // given on the move that reaches 100 still wins, so a side in check gets the draw only
    // when it has a legal move. Rare enough that the loop costs nothing.
    if (pos.half_move_counter >= 100) {
        if (!pos.in_check(pos.side_to_move)) {
            return 0;
        }

        MoveList list;
        generate_moves(pos, list);
        for (Move m : list) {
            Position next = pos;
            if (next.make_move(m)) {
                return 0;
            }
        }

        return -MATE + ply;
    }

    if (depth <= 0) {
        return quiescence(pos, alpha, beta, ply);
    }

    state.nodes++;
    if (state.nodes % CHECK_TIME_INTERVAL == 0 && state.is_time_limited) {
        if (std::chrono::steady_clock::now() > state.end_time) {
            *state.stop_flag = true;
        }
    }

    // The value is never used: every caller sees the flag and returns too.
    if (*state.stop_flag) {
        return 0;
    }

    // Cut only with an entry searched at least this deep, and with a bound only on its side of
    // the window. A shallower entry still puts its move first in the ordering below.
    TTEntry entry;

    if (tt.probe(pos.hash, ply, entry) && entry.depth >= depth) {
        if (entry.bound == Bound::EXACT) {
            return entry.score;
        }
        if (entry.bound == Bound::LOWER && entry.score >= beta) {
            return entry.score;
        }
        if (entry.bound == Bound::UPPER && entry.score <= alpha) {
            return entry.score;
        }
    }

    const bool in_check = pos.in_check(us);
    const bool has_material_other_than_pawns = pos.pieces[us][KNIGHT] | pos.pieces[us][BISHOP] |
                                               pos.pieces[us][ROOK] | pos.pieces[us][QUEEN];

    // Null move pruning: give the opponent a free move. If a reduced null-window search still
    // fails high, a real move would too. Not in check, not with king and pawns only (zugzwang)
    // and not twice in a row. A mate after a pass proves nothing, so it is clamped to beta.
    // No TT store: the entry would carry no move.
    if (null_allowed && !in_check && has_material_other_than_pawns &&
        depth >= MIN_NULL_MOVE_DEPTH) {
        Position next = pos;
        next.make_null_move();

        int score =
            -alphabeta(next, -beta, -beta + 1, depth - 1 - NULL_MOVE_REDUCTION, ply + 1, false);

        if (*state.stop_flag) {
            return 0;
        }

        if (score >= beta) {
            if (score >= MATE - MAX_PLY) {
                return beta;
            }
            return score;
        }
    }

    int scores[256];
    int best = -INF;
    Move best_move;
    MoveList list;
    int legal_moves = 0;

    generate_moves(pos, list);
    score_moves(pos, list, entry.move, scores, state.killers[ply]);

    for (int i = 0; i < list.size; i++) {
        Position next = pos;
        pick_next(list, scores, i);
        Move m = list.moves[i];

        if (!next.make_move(m)) {
            continue;
        }

        legal_moves++;
        int score;

        // PVS: the first move is likely the best and gets the full window.
        if (legal_moves == 1) {
            score = -alphabeta(next, -beta, -alpha, depth - 1, ply + 1, true);
        } else {
            // The rest get a null-window test against alpha, late quiet moves reduced first
            // (LMR). A move that beats alpha is searched again at full depth, then full width.
            const bool can_be_reduced = depth >= MIN_REDUCTION_DEPTH &&
                                        legal_moves >= FIRST_REDUCED_MOVE && !m.is_capture() &&
                                        !m.is_promotion() && !in_check &&
                                        !next.in_check(next.side_to_move);

            if (can_be_reduced) {
                score = -alphabeta(next, -alpha - 1, -alpha, depth - 1 - LATE_MOVE_REDUCTION,
                                   ply + 1, true);
            }
            if (!can_be_reduced || score > alpha) {
                score = -alphabeta(next, -alpha - 1, -alpha, depth - 1, ply + 1, true);
                if (score > alpha && score < beta) {
                    score = -alphabeta(next, -beta, -alpha, depth - 1, ply + 1, true);
                }
            }
        }

        // Stopped: the child's score is meaningless.
        if (*state.stop_flag) {
            return 0;
        }

        if (score > best) {
            best = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
            }
        }

        // Fail high: the opponent will avoid this position, one refutation is enough.
        if (score >= beta) {
            // A quiet move that refutes this position will likely refute its siblings too: it
            // becomes the newer killer of this ply, and the newer one before it the older.
            if (!m.is_capture() && !m.is_promotion()) {
                if (m != state.killers[ply][0]) {
                    state.killers[ply][1] = state.killers[ply][0];
                    state.killers[ply][0] = m;
                }

                state.history[us][m.from()][m.to()] += depth * depth;
                if (state.history[us][m.from()][m.to()] >= MAX_HISTORY_SCORE) {
                    for (int from = 0; from < 64; from++) {
                        for (int to = 0; to < 64; to++) {
                            state.history[us][from][to] /= 2;
                        }
                    }
                }
            }
            tt.store(pos.hash, m, score, depth, Bound::LOWER, ply);
            return score;
        }
    }

    if (best == -INF) {
        if (in_check) {
            return -MATE + ply;
        } else {
            return 0;
        }
    }

    if (best <= alpha_orig) {
        tt.store(pos.hash, best_move, best, depth, Bound::UPPER, ply);
    } else {
        tt.store(pos.hash, best_move, best, depth, Bound::EXACT, ply);
    }
    return best;
}

// One iteration at the root. Returns nullopt when stopped midway; think then keeps the result
// of the previous depth.
std::optional<SearchResult> search_root(const Position &pos, int depth) {
    stack_at(state.root_index) = pos.hash;
    state.nodes++;
    SearchResult result = {Move(), -INF, 0};

    MoveList list;
    generate_moves(pos, list);

    int scores[256];
    int alpha = -INF;
    int beta = INF;
    int legal_moves = 0;

    TTEntry entry;
    // Only for the move: the root is always searched.
    tt.probe(pos.hash, 0, entry);

    score_moves(pos, list, entry.move, scores, NO_KILLERS);

    for (int i = 0; i < list.size; i++) {
        Position next = pos;
        pick_next(list, scores, i);
        Move m = list.moves[i];

        if (!next.make_move(m)) {
            continue;
        }

        legal_moves++;
        int score;

        // PVS, as in alphabeta.
        if (legal_moves == 1) {
            score = -alphabeta(next, -beta, -alpha, depth - 1, 1, true);
        } else {
            // Null-window test, full window again if it beats alpha. No reductions at the root.
            score = -alphabeta(next, -alpha - 1, -alpha, depth - 1, 1, true);
            if (score > alpha) {
                score = -alphabeta(next, -beta, -alpha, depth - 1, 1, true);
            }
        }

        // Stopped: the child's score is meaningless.
        if (*state.stop_flag) {
            return std::nullopt;
        }

        if (score > result.score) {
            result.score = score;
            result.best_move = m;
            alpha = score;
        }
    }

    result.nodes = state.nodes;

    // No legal move at the root: checkmate or stalemate, with ply 0.
    if (result.score == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            result.score = -MATE;
        } else {
            result.score = 0;
        }
    }

    tt.store(pos.hash, result.best_move, result.score, depth, Bound::EXACT, 0);

    return result;
}
} // namespace

SearchResult negamax_root(const Position &pos, int depth) {
    assert(depth >= 1);
    state.nodes = 1;
    SearchResult result = {Move(), -INF, 0};

    MoveList list;
    generate_moves(pos, list);

    for (Move m : list) {
        Position next = pos;
        if (!next.make_move(m)) {
            continue;
        }

        int score = -negamax(next, depth - 1, 1);
        if (score > result.score) {
            result.score = score;
            result.best_move = m;
        }
    }

    result.nodes = state.nodes;

    // No legal move at the root: checkmate or stalemate, with ply 0.
    if (result.score == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            result.score = -MATE;
        } else {
            result.score = 0;
        }
    }

    return result;
}

SearchResult search(const Position &pos, int depth) {
    assert(depth >= 1);
    state = SearchState{};
    // never_stop: the iteration always completes, so the optional is never empty.
    state.stop_flag = &never_stop;
    state.hash_stack.resize(MAX_PLY + 1);
    // A fresh table makes the result depend on `pos` and `depth` alone (tests, bench);
    // think keeps it between the moves of a game.
    tt.clear();
    return *search_root(pos, depth);
}

SearchResult think(const Position &pos, const std::vector<uint64_t> &history,
                   const SearchLimits &limits, std::atomic<bool> &stop_flag,
                   const IterationCallback &on_iteration) {
    assert(limits.depth >= 1);
    const auto start = std::chrono::steady_clock::now();

    state = SearchState{};
    // Depth 1 runs with no time limit and a flag nobody sets, so it always completes and think
    // always has a move to return.
    state.stop_flag = &never_stop;
    state.hash_stack = history;
    state.hash_stack.resize(history.size() + MAX_PLY + 1);
    state.root_index = static_cast<int>(history.size());

    SearchResult result;

    for (int depth = 1; depth <= limits.depth; depth++) {
        // From depth 2 on, the caller's flag and the clock can stop the search.
        if (depth == 2) {
            state.stop_flag = &stop_flag;
            if (limits.time) {
                state.is_time_limited = true;
                state.end_time = start + *limits.time;
            }
        }

        std::optional<SearchResult> iteration = search_root(pos, depth);
        if (!iteration) {
            break;
        }

        result = *iteration;

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        on_iteration(FinishedIteration{depth, result, elapsed});

        // Without a legal move every deeper iteration returns the same checkmate or stalemate.
        if (result.best_move == Move()) {
            break;
        }
    }

    return result;
}