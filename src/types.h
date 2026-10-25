#pragma once
#include <cassert>
#include <cstdint>

using Bitboard = uint64_t;

enum Color : int { WHITE, BLACK, COLOR_NB };

constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : uint8_t {
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
    PIECE_TYPE_NB,
    NO_PIECE_TYPE = PIECE_TYPE_NB
};

// a1 = 0, b1 = 1, ..., h8 = 63.
enum Square : int {
    A1,
    B1,
    C1,
    D1,
    E1,
    F1,
    G1,
    H1,
    A2,
    B2,
    C2,
    D2,
    E2,
    F2,
    G2,
    H2,
    A3,
    B3,
    C3,
    D3,
    E3,
    F3,
    G3,
    H3,
    A4,
    B4,
    C4,
    D4,
    E4,
    F4,
    G4,
    H4,
    A5,
    B5,
    C5,
    D5,
    E5,
    F5,
    G5,
    H5,
    A6,
    B6,
    C6,
    D6,
    E6,
    F6,
    G6,
    H6,
    A7,
    B7,
    C7,
    D7,
    E7,
    F7,
    G7,
    H7,
    A8,
    B8,
    C8,
    D8,
    E8,
    F8,
    G8,
    H8,
    NO_SQUARE
};

constexpr int file_of(Square s) { return s & 7; }

constexpr int rank_of(Square s) { return s >> 3; }

constexpr Square make_square(int file, int rank) { return Square(rank * 8 + file); }

// Bit 2: capture. Bit 3: promotion, with bits 0-1 giving the piece (N, B, R, Q).
// 6 and 7 are unused.
enum MoveFlag : uint8_t {
    QUIET = 0,
    DOUBLE_PUSH = 1,
    KING_CASTLE = 2,
    QUEEN_CASTLE = 3,
    CAPTURE = 4,
    EN_PASSANT_CAPTURE = 5,
    KNIGHT_PROMOTION = 8,
    BISHOP_PROMOTION = 9,
    ROOK_PROMOTION = 10,
    QUEEN_PROMOTION = 11,
    KNIGHT_PROMOTION_CAPTURE = 12,
    BISHOP_PROMOTION_CAPTURE = 13,
    ROOK_PROMOTION_CAPTURE = 14,
    QUEEN_PROMOTION_CAPTURE = 15,
};

// from in bits 0-5, to in bits 6-11, flag in bits 12-15. Move() (a1a1) means no move.
struct Move {
    uint16_t data = 0;

    constexpr Move() = default;

    constexpr Move(Square from, Square to, MoveFlag flag)
        : data(uint16_t(from | (to << 6) | (flag << 12))) {}

    constexpr Square from() const { return Square(data & 0b111111); }
    constexpr Square to() const { return Square((data >> 6) & 0b111111); }
    constexpr MoveFlag flag() const { return MoveFlag(data >> 12); }
    constexpr bool is_capture() const { return bool(flag() & 0b100); }
    constexpr bool is_promotion() const { return bool(flag() & 0b1000); }
    constexpr PieceType promotion_type() const {
        assert(is_promotion());
        return PieceType(KNIGHT + (flag() & 0b11));
    }

    constexpr bool operator==(const Move &) const = default;
};

enum CastlingRight : uint8_t {
    NO_CASTLING = 0,
    WHITE_KINGSIDE = 1,
    WHITE_QUEENSIDE = 2,
    BLACK_KINGSIDE = 4,
    BLACK_QUEENSIDE = 8
};
