#include <iostream>
#include "bitboard.h"
#include "position.h"
#include "types.h"

int main() {
    Position pos;
    pos.put_piece(WHITE, KING, A5);
    pos.put_piece(BLACK, KING, F3);
    pos.put_piece(WHITE, BISHOP, E5);
    pos.put_piece(BLACK, KNIGHT, H8);
    
    print_position(pos);

    return 0;

}