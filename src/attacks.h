#pragma once
#include "types.h"

extern Bitboard KING_ATTACKS[64];
extern Bitboard PAWN_ATTACKS[COLOR_NB][64];
extern Bitboard KNIGHT_ATTACKS[64];

void init_attacks();

Bitboard bishop_attacks(Square s, Bitboard occ);
Bitboard rook_attacks(Square s, Bitboard occ);
Bitboard queen_attacks(Square s, Bitboard occ);
