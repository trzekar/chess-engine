#include "attacks.h"
#include "position.h"
#include <cassert>
#include <cstdio>
#include <random>

// Reference implementation: walks the board on (file, rank) coordinates,
// with no shifts and no masks. It stays the oracle when attacks.cpp moves to
// magic bitboards in step 5.2.

struct Step {
    int df;
    int dr;
};

static Bitboard ref_step(Square s, Step st) {
    const int f = file_of(s) + st.df;
    const int r = rank_of(s) + st.dr;
    if (f < 0 || f > 7 || r < 0 || r > 7)
        return 0;
    return square_bb(make_square(f, r));
}

static Bitboard ref_ray(Square s, Bitboard occ, Step dir) {
    Bitboard result = 0;
    int f = file_of(s) + dir.df;
    int r = rank_of(s) + dir.dr;
    while (f >= 0 && f <= 7 && r >= 0 && r <= 7) {
        const Bitboard b = square_bb(make_square(f, r));
        result |= b;
        if (occ & b)
            break;
        f += dir.df;
        r += dir.dr;
    }
    return result;
}

static Bitboard ref_knight(Square s) {
    const Step steps[] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    Bitboard result = 0;
    for (Step st : steps)
        result |= ref_step(s, st);
    return result;
}

static Bitboard ref_king(Square s) {
    const Step steps[] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    Bitboard result = 0;
    for (Step st : steps)
        result |= ref_step(s, st);
    return result;
}

static Bitboard ref_pawn(Color c, Square s) {
    const int dr = c == WHITE ? 1 : -1;
    return ref_step(s, {-1, dr}) | ref_step(s, {1, dr});
}

static Bitboard ref_rook(Square s, Bitboard occ) {
    return ref_ray(s, occ, {1, 0}) | ref_ray(s, occ, {-1, 0}) | ref_ray(s, occ, {0, 1}) |
           ref_ray(s, occ, {0, -1});
}

static Bitboard ref_bishop(Square s, Bitboard occ) {
    return ref_ray(s, occ, {1, 1}) | ref_ray(s, occ, {1, -1}) | ref_ray(s, occ, {-1, 1}) |
           ref_ray(s, occ, {-1, -1});
}

// Squares attacked by the piece of type `pt` and colour `c` standing on `s`.
static Bitboard ref_attacks(Color c, PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
    case PAWN:
        return ref_pawn(c, s);
    case KNIGHT:
        return ref_knight(s);
    case BISHOP:
        return ref_bishop(s, occ);
    case ROOK:
        return ref_rook(s, occ);
    case QUEEN:
        return ref_rook(s, occ) | ref_bishop(s, occ);
    case KING:
        return ref_king(s);
    default:
        return 0;
    }
}

static void test_known_squares() {
    assert(KNIGHT_ATTACKS[A1] == (square_bb(B3) | square_bb(C2)));
    assert(KNIGHT_ATTACKS[H8] == (square_bb(F7) | square_bb(G6)));
    assert(popcount(KNIGHT_ATTACKS[D4]) == 8);

    assert(KING_ATTACKS[A1] == (square_bb(A2) | square_bb(B1) | square_bb(B2)));
    assert(popcount(KING_ATTACKS[E1]) == 5);
    assert(popcount(KING_ATTACKS[D4]) == 8);
    // A vertical step needs no rank mask: the king on e7 reaches e8.
    assert((KING_ATTACKS[E7] & square_bb(E8)) != 0);
    // Sideways steps must not wrap to the other edge of the board.
    assert((KING_ATTACKS[A4] & square_bb(H4)) == 0);
    assert((KING_ATTACKS[H4] & square_bb(A5)) == 0);

    assert(PAWN_ATTACKS[WHITE][E4] == (square_bb(D5) | square_bb(F5)));
    assert(PAWN_ATTACKS[BLACK][E4] == (square_bb(D3) | square_bb(F3)));
    assert(PAWN_ATTACKS[WHITE][A2] == square_bb(B3));
    assert(PAWN_ATTACKS[BLACK][H7] == square_bb(G6));
    assert(PAWN_ATTACKS[WHITE][H8] == 0);
    // is_attacked reads the white table on rank 1, where no white pawn stands.
    assert(PAWN_ATTACKS[WHITE][E1] == (square_bb(D2) | square_bb(F2)));

    // A blocker ends the ray and belongs to it, whatever its colour.
    const Bitboard rook_a1 = square_bb(A2) | square_bb(A3) | square_bb(A4) |
                             (RANK_1 & ~square_bb(A1));
    assert(rook_attacks(A1, square_bb(A4)) == rook_a1);

    const Bitboard start = RANK_1 | RANK_2 | RANK_7 | RANK_8;
    assert(bishop_attacks(C1, start) == (square_bb(B2) | square_bb(D2)));
    assert(rook_attacks(A1, start) == (square_bb(A2) | square_bb(B1)));
    assert(popcount(queen_attacks(D4, 0)) == 27);
}

static void test_empty_board_totals() {
    int knight = 0, king = 0, white_pawn = 0, black_pawn = 0;
    int bishop = 0, rook = 0, queen = 0;

    for (int sq = 0; sq < 64; ++sq) {
        const Square s = Square(sq);
        knight += popcount(KNIGHT_ATTACKS[s]);
        king += popcount(KING_ATTACKS[s]);
        white_pawn += popcount(PAWN_ATTACKS[WHITE][s]);
        black_pawn += popcount(PAWN_ATTACKS[BLACK][s]);
        bishop += popcount(bishop_attacks(s, 0));
        rook += popcount(rook_attacks(s, 0));
        queen += popcount(queen_attacks(s, 0));
        assert(popcount(rook_attacks(s, 0)) == 14);
    }

    assert(knight == 336);
    assert(king == 420);
    assert(white_pawn == 98);
    assert(black_pawn == 98);
    assert(bishop == 560);
    assert(rook == 896);
    assert(queen == 1456);
}

static void test_tables_match_reference() {
    for (int sq = 0; sq < 64; ++sq) {
        const Square s = Square(sq);
        assert(KNIGHT_ATTACKS[s] == ref_knight(s));
        assert(KING_ATTACKS[s] == ref_king(s));
        assert(PAWN_ATTACKS[WHITE][s] == ref_pawn(WHITE, s));
        assert(PAWN_ATTACKS[BLACK][s] == ref_pawn(BLACK, s));
    }
}

static void test_sliders_match_reference() {
    std::mt19937_64 rng(2026);

    for (int i = 0; i < 1500; ++i) {
        // Alternate dense, medium and sparse boards.
        Bitboard occ = rng();
        if (i % 3 >= 1)
            occ &= rng();
        if (i % 3 == 2)
            occ &= rng();

        for (int sq = 0; sq < 64; ++sq) {
            const Square s = Square(sq);
            assert(bishop_attacks(s, occ) == ref_bishop(s, occ));
            assert(rook_attacks(s, occ) == ref_rook(s, occ));
            assert(queen_attacks(s, occ) == (ref_rook(s, occ) | ref_bishop(s, occ)));
        }
    }
}

static Square random_empty_square(const Position &pos, std::mt19937_64 &rng, bool pawn) {
    for (;;) {
        const Square s = Square(int(rng() % 64));
        if (pos.piece_at(s) != NO_PIECE_TYPE)
            continue;
        if (pawn && (rank_of(s) == 0 || rank_of(s) == 7))
            continue;
        return s;
    }
}

// is_attacked looks from the target square back to the attackers. The
// reference looks forward from every piece instead, so the two share no logic.
static void test_is_attacked_matches_reference() {
    std::mt19937_64 rng(42);

    for (int i = 0; i < 1000; ++i) {
        Position pos;
        pos.put_piece(WHITE, KING, random_empty_square(pos, rng, false));
        pos.put_piece(BLACK, KING, random_empty_square(pos, rng, false));

        const int extra = int(rng() % 15);
        for (int k = 0; k < extra; ++k) {
            const Color c = Color(int(rng() % 2));
            const PieceType pt = PieceType(int(rng() % 5));
            pos.put_piece(c, pt, random_empty_square(pos, rng, pt == PAWN));
        }

        const Bitboard occ = pos.occupied();
        Bitboard expected[COLOR_NB] = {0, 0};
        for (int sq = 0; sq < 64; ++sq) {
            const Square s = Square(sq);
            const PieceType pt = pos.piece_at(s);
            if (pt == NO_PIECE_TYPE)
                continue;
            const Color c = (pos.occupancy[WHITE] & square_bb(s)) ? WHITE : BLACK;
            expected[c] |= ref_attacks(c, pt, s, occ);
        }

        for (int sq = 0; sq < 64; ++sq) {
            const Square s = Square(sq);
            assert(pos.is_attacked(s, WHITE) == ((expected[WHITE] & square_bb(s)) != 0));
            assert(pos.is_attacked(s, BLACK) == ((expected[BLACK] & square_bb(s)) != 0));
        }
    }
}

static void test_is_attacked_positions() {
    Position pos;

    assert(parse_fen(START_FEN, pos));
    assert(pos.is_attacked(E3, WHITE));
    assert(pos.is_attacked(D3, WHITE));
    assert(!pos.is_attacked(E4, WHITE));
    assert(pos.is_attacked(F6, BLACK));
    assert(!pos.is_attacked(E5, BLACK));

    // Pawn direction: a white pawn on e4 attacks d5 and f5, not d3.
    assert(parse_fen("4k3/8/8/8/4P3/8/8/4K3 w - - 0 1", pos));
    assert(pos.is_attacked(D5, WHITE));
    assert(pos.is_attacked(F5, WHITE));
    assert(!pos.is_attacked(D3, WHITE));

    // A black pawn on d2 gives check to the white king on e1.
    assert(parse_fen("4k3/8/8/8/8/8/3p4/4K3 w - - 0 1", pos));
    assert(pos.is_attacked(E1, BLACK));

    // The rook on a1 defends a2 but cannot see past it.
    assert(parse_fen("4k3/8/8/8/8/8/P7/R3K3 w - - 0 1", pos));
    assert(pos.is_attacked(A2, WHITE));
    assert(!pos.is_attacked(A3, WHITE));
}

int main() {
    init_attacks();
    test_known_squares();
    test_empty_board_totals();
    test_tables_match_reference();
    test_sliders_match_reference();
    test_is_attacked_matches_reference();
    test_is_attacked_positions();
    std::puts("test_attacks: OK");
    return 0;
}