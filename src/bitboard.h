#pragma once
#include "types.h"
#include <bit>
#include <cassert>
#include <iostream>

constexpr Bitboard square_bb(Square s) { return (1ULL << s); }

constexpr int popcount(Bitboard bb) { return std::popcount(bb); }

constexpr Square lsb(Bitboard bb) {
    assert(bb != 0);
    return Square(std::countr_zero(bb));
}

constexpr Square pop_lsb(Bitboard &bb) {
    Square s = lsb(bb);
    bb = bb - (bb & -bb);
    return s;
}

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_B = FILE_A << 1;
constexpr Bitboard FILE_C = FILE_A << 2;
constexpr Bitboard FILE_D = FILE_A << 3;
constexpr Bitboard FILE_E = FILE_A << 4;
constexpr Bitboard FILE_F = FILE_A << 5;
constexpr Bitboard FILE_G = FILE_A << 6;
constexpr Bitboard FILE_H = FILE_A << 7;

constexpr Bitboard RANK_1 = 0b11111111ULL;
constexpr Bitboard RANK_2 = RANK_1 << (8 * 1);
constexpr Bitboard RANK_3 = RANK_1 << (8 * 2);
constexpr Bitboard RANK_4 = RANK_1 << (8 * 3);
constexpr Bitboard RANK_5 = RANK_1 << (8 * 4);
constexpr Bitboard RANK_6 = RANK_1 << (8 * 5);
constexpr Bitboard RANK_7 = RANK_1 << (8 * 6);
constexpr Bitboard RANK_8 = RANK_1 << (8 * 7);

inline void print_bitboard(Bitboard bb) {
    for (int rank = 7; rank >= 0; rank--) {
        std::cout << rank + 1 << " ";
        for (int file = 0; file <= 7; file++) {
            std::cout << ((square_bb(make_square(file, rank)) & bb) ? "X " : ". ");
        }
        std::cout << '\n';
    }
    
    std::cout << "  ";

    for (int i = 0; i <= 7; i++) {
        std::cout << char('a' + i) << " ";
    }

    std::cout << '\n';
}