#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "position.h"
#include "types.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace {
// Reading the clock costs tens of nanoseconds, a node about 140, so the clock is read only
// once every 2048 nodes: about 0.3 ms between checks at 7 million nodes per second.
constexpr uint64_t CHECK_TIME_INTERVAL = 2048;

// Fixed bonus that puts every capture and queen promotion ahead of every quiet move. Without
// it KxP would score 0, the same as a quiet move.
constexpr int CAPTURE_BONUS = 1000;

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
};

SearchState state;

// The stack is indexed with int: the scan for repetitions walks down from current - 4, which as
// an unsigned type would wrap around instead of going below zero. Every index used is at least 0.
uint64_t &stack_at(int i) {
    assert(i >= 0);
    return state.hash_stack[static_cast<size_t>(i)];
}

// The stop flag of a search without limits: nobody ever sets it.
std::atomic<bool> never_stop{false};

// Move ordering score, higher is searched first. Captures follow MVV-LVA: the most valuable
// victim first, and for the same victim the least valuable attacker. The en passant victim is
// a pawn, though the target square is empty. A quiet queen promotion scores like a pawn taking
// a queen. Everything else scores 0 and keeps the generator order.
int score_move(const Position &pos, Move m) {
    if (m.is_capture()) {
        const PieceType victim = m.flag() == EN_PASSANT_CAPTURE ? PAWN : pos.piece_at(m.to());
        const PieceType attacker = pos.piece_at(m.from());
        return CAPTURE_BONUS + 10 * victim + (KING - attacker);
    }

    if (m.flag() == QUEEN_PROMOTION) {
        return CAPTURE_BONUS + 10 * QUEEN + (KING - PAWN);
    }

    return 0;
}

// Selection sort, one step at a time: moves the highest-scoring move among i..size-1 to
// index i, keeping each score next to its move. After a cutoff the rest of the list is
// never sorted, which is cheaper than sorting it all up front.
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

// Quiescence search: past the depth limit, plays only captures and queen promotions until the
// position is quiet, so eval never scores a position in the middle of an exchange (the horizon
// effect). It needs no depth: every capture removes a piece, so the chains end on their own.
// In check there is no stand pat, as no quiet move may hold the static score: every move is
// searched, and with none legal the side is mated. Out of check only captures are searched,
// so a stalemate cannot be told apart from a quiet position and is never scored here.
int quiescence(const Position &pos, int alpha, int beta, int ply) {
    state.nodes++;
    if (state.nodes % CHECK_TIME_INTERVAL == 0 && state.is_time_limited) {
        if (std::chrono::steady_clock::now() > state.end_time) {
            *state.stop_flag = true;
        }
    }

    // As in alphabeta: an interrupted child's value is meaningless.
    if (*state.stop_flag) {
        return 0;
    }

    // Only a guard: capture chains end long before MAX_PLY, but chains of checks could run on,
    // and tables indexed by ply must not overflow.
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

    for (int i = 0; i < list.size; i++) {
        scores[i] = score_move(pos, list.moves[i]);
    }

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

    // Every real score is above -INF, so -INF here means no legal move. With pseudo-legal
    // moves that is known only after trying them all. Adding `ply` makes a nearer mate
    // score higher, so the engine prefers the shortest one.
    if (max == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            return -MATE + ply;
        } else {
            return 0;
        }
    }

    return max;
}

int alphabeta(const Position &pos, int alpha, int beta, int depth, int ply) {
    const int current = state.root_index + ply;
    const int oldest = std::max(0, current - pos.half_move_counter);
    stack_at(current) = pos.hash;

    // A repetition of a position on this line or in the game is a draw: the side that could
    // have avoided it did not, and will not next time either. Only positions with the same
    // side to move can match, so every second ply; none can repeat within 2 plies, and none
    // before the last capture or pawn move, which half_move_counter counts back to.
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

    // scores[i] belongs to list.moves[i]; pick_next swaps them together.
    int scores[256];
    int best = -INF;
    MoveList list;

    generate_moves(pos, list);

    // Best moves first: the earlier alpha-beta meets the move that refutes this position,
    // the more of the list it skips.
    for (int i = 0; i < list.size; i++) {
        scores[i] = score_move(pos, list.moves[i]);
    }

    for (int i = 0; i < list.size; i++) {
        Position next = pos;
        pick_next(list, scores, i);
        Move m = list.moves[i];

        if (!next.make_move(m)) {
            continue;
        }

        // The child scores from the other side: the bounds change sign and swap places.
        int score = -alphabeta(next, -beta, -alpha, depth - 1, ply + 1);

        // An interrupted child returns a meaningless value: leave before it touches best or
        // alpha.
        if (*state.stop_flag) {
            return 0;
        }

        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
            }
        }

        // The opponent already has a better choice higher in the tree and will avoid this
        // position, so one refutation is enough.
        if (score >= beta) {
            return score;
        }
    }

    if (best == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            return -MATE + ply;
        } else {
            return 0;
        }
    }

    return best;
}

// One iteration: every root move searched to `depth`, the best one kept. Returns nothing when the
// stop flag cut the iteration short: some moves are then unsearched or searched only in part, so
// neither the move nor the score may be used.
std::optional<SearchResult> search_root(const Position &pos, int depth) {
    stack_at(state.root_index) = pos.hash;
    state.nodes++;
    SearchResult result = {Move(), -INF, 0};

    MoveList list;
    generate_moves(pos, list);

    // scores[i] belongs to list.moves[i]; pick_next swaps them together.
    int scores[256];
    int alpha = -INF;
    int beta = INF;

    // Best moves first: the earlier alpha-beta meets the move that refutes this position,
    // the more of the list it skips.
    for (int i = 0; i < list.size; i++) {
        scores[i] = score_move(pos, list.moves[i]);
    }

    for (int i = 0; i < list.size; i++) {
        Position next = pos;
        pick_next(list, scores, i);
        Move m = list.moves[i];

        if (!next.make_move(m)) {
            continue;
        }

        int score = -alphabeta(next, -beta, -alpha, depth - 1, 1);

        // Checked before `score` is used: an interrupted child returns a meaningless value.
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
    // A fixed-depth search: no time limit and a flag nobody sets, so the iteration always
    // completes and the optional always holds a result.
    state = SearchState{};
    state.stop_flag = &never_stop;
    state.hash_stack.resize(MAX_PLY + 1);
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
        // An interrupted iteration is discarded: the last completed one stays the answer.
        if (!iteration) {
            break;
        }

        // A deeper result replaces a shallower one.
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