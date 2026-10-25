#pragma once
#include "position.h"
#include "types.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

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

    // Calls of alphabeta and quiescence, the root included; draws by repetition or the
    // fifty-move rule return before they are counted. In think, the total of all iterations.
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

// Plain negamax, no pruning and no quiescence: its node count is the sum of perft(0..depth).
// Too slow for play: a reference for tests and node counts. `depth` must be at least 1.
SearchResult negamax_root(const Position &pos, int depth);

// Alpha-beta with quiescence search to a fixed `depth`, from an empty transposition table,
// so the result depends on `pos` and `depth` alone. `depth` must be at least 1.
SearchResult search(const Position &pos, int depth);

// Called after every completed iteration, on the thread that runs think.
using IterationCallback = std::function<void(const FinishedIteration &)>;

// Iterative deepening until `limits.depth` is completed, the time runs out or `stop_flag` is
// set from any thread. Returns the last completed iteration; depth 1 always completes, so
// there is a move whenever one is legal. `history`: hashes of the game before `pos`, oldest
// first (the whole game is fine, the scan stops at the last capture or pawn move).
// think sets `stop_flag` itself on timeout, so clear it before each call.
SearchResult think(const Position &pos, const std::vector<uint64_t> &history,
                   const SearchLimits &limits, std::atomic<bool> &stop_flag,
                   const IterationCallback &on_iteration);