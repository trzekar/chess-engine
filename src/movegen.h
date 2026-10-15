#pragma once
#include "position.h"
#include <string>

struct MoveList {
    // The maximum number of legal moves in any reachable position is 218 (Nenad Petrovic, 1964)
    Move moves[256];
    int size = 0;

    void add(Move m) {
        assert(size < 256);
        moves[size++] = m;
    }
    const Move *begin() const { return moves; }
    const Move *end() const { return moves + size; }
};

// Appends all pseudo-legal moves for the side to move to `list`. A move may leave the
// own king in check; make_move rejects those.
void generate_moves(const Position &pos, MoveList &list);

std::string to_uci(Move m);