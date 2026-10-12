#pragma once
#include "types.h"
#include "bitboard.h"
#include <string>
#include <string_view>

struct Position {
    Bitboard pieces[COLOR_NB][PIECE_TYPE_NB] = {};
    Bitboard occupancy[COLOR_NB] = {};
    PieceType piece_on[64];
    Color side_to_move = WHITE;
    uint8_t castling_rights = NO_CASTLING;
    Square ep_square = NO_SQUARE;
    uint8_t half_move_counter = 0;
    uint16_t move_number = 1;
    uint64_t hash = 0;

    Position() {
        for (PieceType &p : piece_on){
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

    Bitboard occupied() const {
        return (occupancy[WHITE] | occupancy[BLACK]); 
    }

    bool is_attacked(Square s, Color by) const;
};

constexpr std::string_view START_FEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

bool parse_fen(std::string_view fen, Position &pos);
std::string to_fen(const Position &pos);
void print_position(const Position &pos);