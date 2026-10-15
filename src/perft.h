#pragma once
#include "position.h"
#include "movegen.h"

// Counts the positions reached after exactly `depth` legal moves. Compared with known
// totals (chessprogramming.org/Perft_Results), it checks generate_moves and make_move.
uint64_t perft(const Position &pos, int depth);