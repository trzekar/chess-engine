#include <iostream>
#include "bitboard.h"
#include "types.h"

int main() {
    print_bitboard(FILE_A);
    print_bitboard(RANK_5);
    print_bitboard(square_bb(E4));

    std::cout << popcount(FILE_D) << "\n";
    
    Move m = Move(A1, F5, DOUBLE_PUSH);

    std::cout << m.from() << "\n";
    std::cout << m.to() << "\n";
    std::cout << int(m.flags()) << "\n";

    Move m2 = Move(A7, A8, QUEEN_PROMOTION);

    std::cout << m2.is_capture() << " " << m2.is_promotion() << "\n";
    return 0;

}