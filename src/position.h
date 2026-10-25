#pragma once
#include "bitboard.h"
#include "types.h"
#include <string>
#include <string_view>

struct Position {
    Bitboard pieces[COLOR_NB][PIECE_TYPE_NB] = {};
    Bitboard occupancy[COLOR_NB] = {};
    // Type only; the color is in occupancy.
    PieceType piece_on[64];
    Color side_to_move = WHITE;
    uint8_t castling_rights = NO_CASTLING;
    Square ep_square = NO_SQUARE;
    uint8_t half_move_counter = 0;
    uint16_t move_number = 1;
    // Zobrist hash (zobrist.h): set by parse_fen, kept up to date by make_move and make_null_move.
    uint64_t hash = 0;

    Position() {
        for (PieceType &p : piece_on) {
            p = NO_PIECE_TYPE;
        }
    }

    void put_piece(Color c, PieceType pt, Square s) {
        assert(piece_on[s] == NO_PIECE_TYPE);
        pieces[c][pt] |= square_bb(s);
        occupancy[c] |= square_bb(s);
        piece_on[s] = pt;
    }

    void remove_piece(Color c, PieceType pt, Square s) {
        assert(piece_on[s] == pt);
        assert(pieces[c][pt] & square_bb(s));
        pieces[c][pt] &= ~square_bb(s);
        occupancy[c] &= ~square_bb(s);
        piece_on[s] = NO_PIECE_TYPE;
    }

    PieceType piece_at(Square s) const { return piece_on[s]; }

    Bitboard occupied() const { return (occupancy[WHITE] | occupancy[BLACK]); }

    bool is_attacked(Square s, Color by) const;

    bool in_check(Color c) const;

    // Plays a pseudo-legal `m`; false if it leaves the own king in check. The position is
    // changed either way, so play on a copy.
    bool make_move(Move m);
    // Pseudo-move for null move pruning.
    void make_null_move();
};

constexpr std::string_view START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Accepts 4 or 6 fields. On failure returns false and leaves `pos` unchanged.
bool parse_fen(std::string_view fen, Position &pos);
std::string to_fen(const Position &pos);
void print_position(const Position &pos);