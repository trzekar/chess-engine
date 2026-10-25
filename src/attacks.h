#pragma once
#include "types.h"

// Filled by init_attacks(), which must run once before any move generation.
extern Bitboard KING_ATTACKS[64];
extern Bitboard PAWN_ATTACKS[COLOR_NB][64];
extern Bitboard KNIGHT_ATTACKS[64];

void init_attacks();

Bitboard bishop_attacks(Square s, Bitboard occ);
Bitboard rook_attacks(Square s, Bitboard occ);
Bitboard queen_attacks(Square s, Bitboard occ);
