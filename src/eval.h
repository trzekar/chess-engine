#pragma once
#include "types.h"
#include "position.h"

// Fills the piece-square tables. Call once at startup, before the first eval.
void init_eval();

int eval(const Position &pos);