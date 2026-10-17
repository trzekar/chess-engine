#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "position.h"
#include <chrono>
#include <cstdint>

namespace {
// Reading the clock costs tens of nanoseconds, a node about 140, so the clock is read only
// once every 2048 nodes: about 0.3 ms between checks at 7 million nodes per second.
constexpr uint64_t CHECK_TIME_INTERVAL = 2048;

struct SearchState {
    uint64_t nodes = 0;
    std::atomic<bool> *stop_flag = nullptr;
    std::chrono::steady_clock::time_point end_time = {};
    bool is_time_limited = false;
};

SearchState state;

// The stop flag of a search without limits: nobody ever sets it.
std::atomic<bool> never_stop{false};

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

    if (depth <= 0) {
        return eval(pos);
    }

    int best = -INF;
    MoveList list;
    generate_moves(pos, list);

    for (Move m : list) {
        Position next = pos;
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
    state.nodes++;
    SearchResult result = {Move(), -INF, 0};

    MoveList list;
    generate_moves(pos, list);

    int alpha = -INF;
    int beta = INF;

    for (Move m : list) {
        Position next = pos;
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
    return *search_root(pos, depth);
}

SearchResult think(const Position &pos, const SearchLimits &limits, std::atomic<bool> &stop_flag,
                   const IterationCallback &on_iteration) {
    assert(limits.depth >= 1);
    const auto start = std::chrono::steady_clock::now();

    state = SearchState{};
    // Depth 1 runs with no time limit and a flag nobody sets, so it always completes and think
    // always has a move to return.
    state.stop_flag = &never_stop;

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