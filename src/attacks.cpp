#include "attacks.h"
#include "bitboard.h"

Bitboard KNIGHT_ATTACKS[64];
Bitboard KING_ATTACKS[64];
Bitboard PAWN_ATTACKS[COLOR_NB][64];

namespace {

constexpr Bitboard NOT_A = ~FILE_A;
constexpr Bitboard NOT_AB = ~(FILE_A | FILE_B);
constexpr Bitboard NOT_H = ~FILE_H;
constexpr Bitboard NOT_GH = ~(FILE_G | FILE_H);

Bitboard king_mask(Square s) {
    Bitboard b = square_bb(s);
    Bitboard attacks = 0;

    attacks |= (b << 7) & NOT_H; // up & left
    attacks |= (b << 8);         // up
    attacks |= (b << 9) & NOT_A; // up & right
    attacks |= (b >> 1) & NOT_H; // left
    attacks |= (b << 1) & NOT_A; // right
    attacks |= (b >> 9) & NOT_H; // down & left
    attacks |= (b >> 8);         // down
    attacks |= (b >> 7) & NOT_A; // down & right

    return attacks;
}

Bitboard knight_mask(Square s) {
    Bitboard b = square_bb(s);
    Bitboard attacks = 0;

    attacks |= (b << 17) & NOT_A;  // 2 up, 1 right
    attacks |= (b << 15) & NOT_H;  // 2 up, 1 left
    attacks |= (b << 10) & NOT_AB; // 1 up, 2 right
    attacks |= (b << 6) & NOT_GH;  // 1 up, 2 left
    attacks |= (b >> 6) & NOT_AB;  // 1 down, 2 right
    attacks |= (b >> 10) & NOT_GH; // 1 down, 2 left
    attacks |= (b >> 15) & NOT_A;  // 2 down, 1 right
    attacks |= (b >> 17) & NOT_H;  // 2 down, 1 left

    return attacks;
}

Bitboard pawn_mask(Square s, Color c) {
    Bitboard b = square_bb(s);
    Bitboard attacks = 0;

    if (c == WHITE) {
        attacks |= (b << 7) & NOT_H; // up & left
        attacks |= (b << 9) & NOT_A; // up & right
    } else {
        attacks |= (b >> 9) & NOT_H; // down & left
        attacks |= (b >> 7) & NOT_A; // down & right
    }

    return attacks;
}

Bitboard slide(Square s, Bitboard occ, int df, int dr) {
    Bitboard attacks = 0;
    int f = file_of(s) + df;
    int r = rank_of(s) + dr;
    while (r <= 7 && r >= 0 && f <= 7 && f >= 0) {
        Bitboard b = square_bb(make_square(f, r));
        attacks |= b;
        if (b & occ)
            break;
        r += dr;
        f += df;
    }
    return attacks;
}

}

void init_attacks() {
    for (int i = 0; i < 64; ++i) {
        Square s = Square(i);
        KNIGHT_ATTACKS[s] = knight_mask(s);
        KING_ATTACKS[s] = king_mask(s);
        PAWN_ATTACKS[WHITE][s] = pawn_mask(s, WHITE);
        PAWN_ATTACKS[BLACK][s] = pawn_mask(s, BLACK);
    }
}

Bitboard bishop_attacks(Square s, Bitboard occ) {
    return slide(s, occ, 1, 1) | slide(s, occ, 1, -1) | slide(s, occ, -1, 1) |
           slide(s, occ, -1, -1);
}

Bitboard rook_attacks(Square s, Bitboard occ) {
    return slide(s, occ, 1, 0) | slide(s, occ, -1, 0) | slide(s, occ, 0, 1) | slide(s, occ, 0, -1);
}

Bitboard queen_attacks(Square s, Bitboard occ) {
    return bishop_attacks(s, occ) | rook_attacks(s, occ);
}