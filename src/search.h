#pragma once
#include "types.h"
#include "position.h"
#include <cstdint>

// INF must be greater than any score
constexpr int INF = 32001;
constexpr int MATE = 32000;

struct SearchResult {
    // Equals Move() when there are no legal moves.
    Move best_move;

    // From the point of view of the side to move.
    int score = 0;
    uint64_t nodes = 0;
};

// Plain negamax without alpha-beta pruning: visits every node up to `depth`, so its node
// count equals the sum of perft(0..depth). Too slow for play; kept as the reference that
// `search` is tested against.
SearchResult negamax_root(const Position &pos, int depth);

// Alpha-beta search to a fixed `depth`. With the full window it returns the same score as
// negamax_root while visiting fewer nodes.
SearchResult search(const Position &pos, int depth);