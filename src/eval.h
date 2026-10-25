#pragma once
#include "types.h"
#include "position.h"

// Fills the piece-square tables. Call once at startup, before the first eval.
void init_eval();

// Static evaluation in centipawns from the side to move's point of view. init_eval() must
// have run.
int eval(const Position &pos);