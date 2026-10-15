#include "perft.h"
#include "movegen.h"
#include "position.h"

// The list is pseudo-legal, so each move is tried on a copy and counted only if
// make_move accepts it.
uint64_t perft(const Position &pos, int depth) {
    if (depth == 0)
        return 1;
    uint64_t nodes = 0;
    MoveList list;
    generate_moves(pos, list);

    for (const Move m : list) {
        Position next = pos;
        if (next.make_move(m)) {
            nodes += perft(next, depth - 1);
        }
    }

    return nodes;
}