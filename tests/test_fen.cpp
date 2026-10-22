#include "position.h"
#include "zobrist.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <type_traits>

static_assert(sizeof(Position) <= 200, "copy-make copies a Position at every node");
static_assert(std::is_trivially_copyable_v<Position>, "copy-make relies on a plain byte copy");

static const char *const ROUNDTRIP_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",
    "r3k2r/8/8/8/8/8/8/R3K2R b Kq - 12 34",
    "8/8/8/8/8/5k2/8/5K1R w - - 99 150",
};

static const char *const INVALID_FENS[] = {
    "",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1",
    "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/7/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNX w KQkq - 0 1",
    "rnbq1bnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQ - 0 1",
    "rnbqkbnr/pppppppp/8/8/4K3/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "P3k3/8/8/8/8/8/8/4K3 w - - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkx - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KK - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w qK - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN1 w K - 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e4 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e6 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq d3 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - x 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 256 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 65536",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 extra",
};

static void check_consistency(const Position &pos) {
    assert((pos.occupancy[WHITE] & pos.occupancy[BLACK]) == 0);

    for (int c = 0; c < COLOR_NB; ++c) {
        Bitboard all = 0;
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
            assert((all & pos.pieces[c][pt]) == 0);
            all |= pos.pieces[c][pt];
        }
        assert(all == pos.occupancy[c]);
    }

    for (int sq = 0; sq < 64; ++sq) {
        const Square s = Square(sq);
        const PieceType pt = pos.piece_at(s);
        if (pt == NO_PIECE_TYPE) {
            assert((pos.occupied() & square_bb(s)) == 0);
        } else {
            const Bitboard both = pos.pieces[WHITE][pt] | pos.pieces[BLACK][pt];
            assert((both & square_bb(s)) != 0);
        }
    }
}

static void test_roundtrip() {
    for (const char *fen : ROUNDTRIP_FENS) {
        Position pos;
        assert(parse_fen(fen, pos));
        check_consistency(pos);
        assert(to_fen(pos) == fen);
        // parse_fen stores the hash computed from scratch; tests/test_zobrist.cpp checks it.
        assert(pos.hash == compute_hash(pos));
    }
}

static void test_start_position() {
    Position pos;
    assert(parse_fen(START_FEN, pos));

    assert(pos.piece_at(A1) == ROOK);
    assert(pos.piece_at(B1) == KNIGHT);
    assert(pos.piece_at(C1) == BISHOP);
    assert(pos.piece_at(D1) == QUEEN);
    assert(pos.piece_at(E1) == KING);
    assert(pos.piece_at(D8) == QUEEN);
    assert(pos.piece_at(E8) == KING);
    assert(pos.piece_at(E4) == NO_PIECE_TYPE);

    assert(pos.pieces[WHITE][PAWN] == RANK_2);
    assert(pos.pieces[BLACK][PAWN] == RANK_7);
    assert(pos.pieces[WHITE][KING] == square_bb(E1));
    assert(pos.pieces[BLACK][KING] == square_bb(E8));
    assert(pos.pieces[WHITE][QUEEN] == square_bb(D1));
    assert(pos.pieces[BLACK][ROOK] == (square_bb(A8) | square_bb(H8)));
    assert(pos.occupancy[WHITE] == (RANK_1 | RANK_2));
    assert(pos.occupancy[BLACK] == (RANK_7 | RANK_8));

    assert(pos.side_to_move == WHITE);
    assert(pos.castling_rights ==
           (WHITE_KINGSIDE | WHITE_QUEENSIDE | BLACK_KINGSIDE | BLACK_QUEENSIDE));
    assert(pos.ep_square == NO_SQUARE);
    assert(pos.half_move_counter == 0);
    assert(pos.move_number == 1);
}

static void test_state_fields() {
    Position pos;

    assert(parse_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", pos));
    assert(pos.side_to_move == BLACK);
    assert(pos.ep_square == E3);
    assert(pos.piece_at(E4) == PAWN);
    assert(pos.piece_at(E2) == NO_PIECE_TYPE);

    assert(parse_fen("rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", pos));
    assert(pos.side_to_move == WHITE);
    assert(pos.ep_square == C6);
    assert(pos.move_number == 2);

    assert(parse_fen("r3k2r/8/8/8/8/8/8/R3K2R b Kq - 12 34", pos));
    assert(pos.castling_rights == (WHITE_KINGSIDE | BLACK_QUEENSIDE));
    assert(pos.ep_square == NO_SQUARE);
    assert(pos.half_move_counter == 12);
    assert(pos.move_number == 34);
}

static void test_four_fields() {
    Position pos;
    assert(parse_fen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", pos));
    assert(pos.half_move_counter == 0);
    assert(pos.move_number == 1);
    assert(to_fen(pos) == "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
}

static void test_invalid() {
    Position reference;
    assert(parse_fen(ROUNDTRIP_FENS[1], reference));
    const std::string before = to_fen(reference);

    for (const char *fen : INVALID_FENS) {
        Position pos = reference;
        assert(!parse_fen(fen, pos));
        assert(to_fen(pos) == before);
    }
}

int main() {
    test_roundtrip();
    test_start_position();
    test_state_fields();
    test_four_fields();
    test_invalid();
    std::puts("test_fen: OK");
    return 0;
}