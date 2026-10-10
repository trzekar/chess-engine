#include "bitboard.h"
#include <cassert>
#include <cstdio>

static_assert(sizeof(Move) == 2, "Move must be 16 bits");

static_assert(A1 == 0 && H1 == 7 && A8 == 56 && H8 == 63 && NO_SQUARE == 64);
static_assert(PAWN == 0 && KING == 5 && PIECE_TYPE_NB == 6);
static_assert(KNIGHT + 1 == BISHOP && BISHOP + 1 == ROOK && ROOK + 1 == QUEEN,
              "promotion_type() relies on this order");

static_assert(~WHITE == BLACK && ~BLACK == WHITE);

static_assert(file_of(E4) == 4 && rank_of(E4) == 3);
static_assert(file_of(A1) == 0 && rank_of(A1) == 0);
static_assert(file_of(H8) == 7 && rank_of(H8) == 7);
static_assert(make_square(4, 3) == E4);

static_assert(square_bb(A1) == 1ULL);
static_assert(square_bb(H8) == 0x8000000000000000ULL);
static_assert(lsb(square_bb(E4)) == E4);
static_assert(lsb(square_bb(H8)) == H8);
static_assert(popcount(0) == 0 && popcount(RANK_1) == 8 && popcount(FILE_H) == 8);
static_assert(popcount(~0ULL) == 64);

static_assert((FILE_A & RANK_1) == square_bb(A1));
static_assert((FILE_H & RANK_8) == square_bb(H8));
static_assert((FILE_E & RANK_4) == square_bb(E4));
static_assert((FILE_A | FILE_B | FILE_C | FILE_D | FILE_E | FILE_F | FILE_G | FILE_H) == ~0ULL);
static_assert((RANK_1 | RANK_2 | RANK_3 | RANK_4 | RANK_5 | RANK_6 | RANK_7 | RANK_8) == ~0ULL);

static_assert(Move().data == 0);
static_assert(Move(E2, E4, DOUBLE_PUSH).from() == E2);
static_assert(Move(E2, E4, DOUBLE_PUSH).to() == E4);
static_assert(Move(E2, E4, DOUBLE_PUSH).flags() == DOUBLE_PUSH);
static_assert(Move(E2, E4, QUIET) == Move(E2, E4, QUIET));
static_assert(Move(E2, E4, QUIET) != Move(E2, E4, DOUBLE_PUSH));


static void test_square_roundtrip() {
    for (int sq = 0; sq < 64; ++sq) {
        Square s = Square(sq);
        assert(make_square(file_of(s), rank_of(s)) == s);
        assert(lsb(square_bb(s)) == s);
        assert(popcount(square_bb(s)) == 1);
    }
}

static void test_pop_lsb() {
    Bitboard bb = RANK_1;
    for (int expected = 0; expected < 8; ++expected)
        assert(pop_lsb(bb) == expected);
    assert(bb == 0);

    bb = FILE_A;
    for (int expected = 0; expected < 64; expected += 8)
        assert(pop_lsb(bb) == expected);
    assert(bb == 0);

    bb = ~0ULL;
    int count = 0;
    while (bb) {
        assert(pop_lsb(bb) == count);
        ++count;
    }
    assert(count == 64);

    bb = square_bb(H8);
    assert(pop_lsb(bb) == H8);
    assert(bb == 0);
}

static void test_move_encoding() {
    const MoveFlag all_flags[] = {
        QUIET, DOUBLE_PUSH, KING_CASTLE, QUEEN_CASTLE, CAPTURE, EN_PASSANT_CAPTURE,
        KNIGHT_PROMOTION, BISHOP_PROMOTION, ROOK_PROMOTION, QUEEN_PROMOTION,
        KNIGHT_PROMOTION_CAPTURE, BISHOP_PROMOTION_CAPTURE,
        ROOK_PROMOTION_CAPTURE, QUEEN_PROMOTION_CAPTURE,
    };

    for (int from = 0; from < 64; ++from)
        for (int to = 0; to < 64; ++to)
            for (MoveFlag flag : all_flags) {
                Move m(Square(from), Square(to), flag);
                assert(m.from() == from);
                assert(m.to() == to);
                assert(m.flags() == flag);
            }
}

static void test_move_predicates() {
    struct Case { MoveFlag flag; bool capture; bool promotion; };
    const Case cases[] = {
        {QUIET, false, false},
        {DOUBLE_PUSH, false, false},
        {KING_CASTLE, false, false},
        {QUEEN_CASTLE, false, false},
        {CAPTURE, true, false},
        {EN_PASSANT_CAPTURE, true, false},
        {KNIGHT_PROMOTION, false, true},
        {BISHOP_PROMOTION, false, true},
        {ROOK_PROMOTION, false, true},
        {QUEEN_PROMOTION, false, true},
        {KNIGHT_PROMOTION_CAPTURE, true, true},
        {BISHOP_PROMOTION_CAPTURE, true, true},
        {ROOK_PROMOTION_CAPTURE, true, true},
        {QUEEN_PROMOTION_CAPTURE, true, true},
    };
    for (const Case& c : cases) {
        Move m(E7, D8, c.flag);
        assert(m.is_capture() == c.capture);
        assert(m.is_promotion() == c.promotion);
    }

    assert(Move(E7, E8, KNIGHT_PROMOTION).promotion_type() == KNIGHT);
    assert(Move(E7, E8, BISHOP_PROMOTION).promotion_type() == BISHOP);
    assert(Move(E7, E8, ROOK_PROMOTION).promotion_type() == ROOK);
    assert(Move(E7, E8, QUEEN_PROMOTION).promotion_type() == QUEEN);
    assert(Move(E7, D8, KNIGHT_PROMOTION_CAPTURE).promotion_type() == KNIGHT);
    assert(Move(E7, D8, BISHOP_PROMOTION_CAPTURE).promotion_type() == BISHOP);
    assert(Move(E7, D8, ROOK_PROMOTION_CAPTURE).promotion_type() == ROOK);
    assert(Move(E7, D8, QUEEN_PROMOTION_CAPTURE).promotion_type() == QUEEN);
}

int main() {
    test_square_roundtrip();
    test_pop_lsb();
    test_move_encoding();
    test_move_predicates();
    std::puts("test_types: OK");
    return 0;
}