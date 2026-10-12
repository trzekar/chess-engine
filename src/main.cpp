#include "bitboard.h"
#include "position.h"
#include "attacks.h"
#include "types.h"
#include <iostream>

int main() {
    init_attacks();
    print_bitboard(KNIGHT_ATTACKS[F7]);
    print_bitboard(rook_attacks(A1, square_bb(A4)));

    return 0;
}