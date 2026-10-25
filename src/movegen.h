#pragma once
#include "position.h"
#include <string>

struct MoveList {
    // 218 is the most legal moves known in a reachable position (Petrović, 1964). The list
    // holds pseudo-legal moves, hence the margin and the assert.
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

// Appends the pseudo-legal moves of quiescence search: every capture, en passant and capture
// promotions included, plus the quiet queen promotion. A move may leave the own king in check.
void generate_captures(const Position &pos, MoveList &list);

std::string to_uci(Move m);

// Finds the generated move whose UCI name is `uci`, or returns Move() if there is none.
// The move is only pseudo-legal, the caller still has to check the result of make_move.
Move parse_uci_move(const Position &pos, std::string_view uci);