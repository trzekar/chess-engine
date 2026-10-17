#pragma once
#include "position.h"
#include "types.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

// Mate scores are MATE minus the plies to the mate, so they stay far above any evaluation
// and still fit in 16 bits for the transposition table. INF must exceed every score, mates
// included, so that -INF works as "no move searched yet".
constexpr int INF = 32001;
constexpr int MATE = 32000;

// Deepest ply the search reaches. Mate scores lie within MAX_PLY of MATE, which tells them
// apart from evaluations.
constexpr int MAX_PLY = 128;

struct SearchResult {
    // Equals Move() when there are no legal moves.
    Move best_move;

    // From the point of view of the side to move.
    int score = 0;

    // Every call of the search is a node, the root and the leaves included.
    uint64_t nodes = 0;
};

struct SearchLimits {
    int depth = MAX_PLY;
    // Empty means no time limit: the search stops only on `stop` or at `depth`.
    std::optional<std::chrono::milliseconds> time;
};

// What think reports after every completed depth; uci.cpp turns it into an `info` line.
struct FinishedIteration {
    int depth = 0;
    SearchResult result = {};
    // Since think started, not since this iteration started.
    std::chrono::milliseconds time = {};
};

// Plain negamax without alpha-beta pruning: visits every node up to `depth`, so its node
// count equals the sum of perft(0..depth). Too slow for play; kept as the reference that
// `search` is tested against. `depth` must be at least 1.
SearchResult negamax_root(const Position &pos, int depth);

// Alpha-beta search to a fixed `depth`. With the full window it returns the same score as
// negamax_root while visiting fewer nodes. `depth` must be at least 1.
SearchResult search(const Position &pos, int depth);

// Called after every completed iteration, on the thread that runs think.
using IterationCallback = std::function<void(const FinishedIteration &)>;

// Iterative deepening: searches to depth 1, 2, 3, ... until it completes `limits.depth`, the
// time in `limits.time` runs out or `stop_flag` is set. Returns the result of the last completed
// iteration; an interrupted one is discarded. Depth 1 always completes, so the result holds a
// move whenever the position has a legal one.
// `stop_flag` may be set from another thread at any time. think also sets it itself when the
// time runs out, so the caller clears it before each call.
SearchResult think(const Position &pos, const SearchLimits &limits, std::atomic<bool> &stop_flag,
                   const IterationCallback &on_iteration);