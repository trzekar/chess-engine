#include "attacks.h"
#include "eval.h"
#include "movegen.h"
#include <cassert>
#include <cstdio>

// The standard perft positions (chessprogramming.org/Perft_Results). Walking their trees gives
// thousands of positions with castling, en passant, promotions and unbalanced material.
static const char *const TREE_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
};

// Expected scores are computed by hand from the PeSTO tables in eval.cpp, never copied from
// the engine's output. Each case isolates one part of eval: in the comments, "mg" and "eg" are
// piece value plus table entry, and the phase is 1 per minor piece, 2 per rook, 4 per queen.
struct Case {
    const char *fen;
    int score; // from the point of view of the side to move
};

static const Case KNOWN[] = {
    // 1. Phase 0, pure endgame. The kings on e1 and e8 read the same entry and cancel.
    //    Pawn e2: eg 94 + 13 = 107.
    {"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", 107},
    // 2. The same position with Black to move: the score changes sign.
    {"4k3/8/8/8/8/8/4P3/4K3 b - - 0 1", -107},
    // 3. Phase 24, pure middlegame. Black is missing the pawn on a7: mg 82 - 35 = 47.
    {"rnbqkbnr/1ppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 47},
    // 4. Phase 8, between the two. The queens and kings cancel, pawn e2: mg 82 - 15 = 67,
    //    eg 107. (8 * 67 + 16 * 107) / 24 = 93.67, and integer division gives 93.
    {"3qk3/8/8/8/8/8/4P3/3QK3 w - - 0 1", 93},
    // 5. Phase 28 after a promotion, clamped to 24. A queen instead of the pawn on a2:
    //    mg 990 - 47 = 943. Without the clamp the endgame gets weight -4 and the score is 965.
    {"rnbqkbnr/pppppppp/8/8/8/8/QPPPPPPP/RNBQKBNR w KQkq - 0 1", 943},
};

static int eval_fen(const char *fen) {
    Position pos;
    assert(parse_fen(fen, pos));
    return eval(pos);
}

// The same position with the colors swapped: every piece changes color and moves to the same
// file on the opposite rank (a2 <-> a7). The side to move, the castling rights and the en
// passant square swap with it, so the mirror of a legal position is legal.
static Position mirror(const Position &pos) {
    Position m;
    for (int sq = 0; sq < 64; ++sq) {
        const Square s = Square(sq);
        const PieceType pt = pos.piece_at(s);
        if (pt == NO_PIECE_TYPE)
            continue;
        const Color c = (pos.occupancy[WHITE] & square_bb(s)) ? WHITE : BLACK;
        m.put_piece(~c, pt, Square(sq ^ 56));
    }
    m.side_to_move = ~pos.side_to_move;
    // White's rights are the low two bits, Black's the high two.
    m.castling_rights = uint8_t(((pos.castling_rights & 3) << 2) | (pos.castling_rights >> 2));
    m.ep_square = pos.ep_square == NO_SQUARE ? NO_SQUARE : Square(pos.ep_square ^ 56);
    m.half_move_counter = pos.half_move_counter;
    m.move_number = pos.move_number;
    return m;
}

// A wrong mirror would make the symmetry checks below pass or fail for the wrong reason.
static void test_mirror_helper() {
    Position pos;
    assert(parse_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", pos));
    assert(to_fen(mirror(pos)) == "rnbqkbnr/pppp1ppp/8/4p3/8/8/PPPPPPPP/RNBQKBNR w KQkq e6 0 1");

    assert(parse_fen("r3k2r/8/8/8/8/8/8/R3K2R b Kq - 12 34", pos));
    assert(to_fen(mirror(pos)) == "r3k2r/8/8/8/8/8/8/R3K2R w Qk - 12 34");

    for (const char *fen : TREE_FENS) {
        assert(parse_fen(fen, pos));
        assert(to_fen(mirror(mirror(pos))) == fen);
    }
}

static void test_start_position() {
    assert(eval_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") == 0);
    assert(eval_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1") == 0);
}

static void test_known_values() {
    for (const Case &c : KNOWN) {
        const int got = eval_fen(c.fen);
        if (got != c.score) {
            std::fprintf(stderr, "%s\neval %d, expected %d\n", c.fen, got, c.score);
            assert(false);
        }
    }
}

// Orientation checks that hold for any sensible tables, so they survive retuning. They fail
// when a table is read upside down or mirrored left to right, and when init_eval was not
// called: then every score is 0, which the symmetry checks below would still accept.
static void test_orientation() {
    // A white pawn about to promote is worth more than one on its starting square.
    assert(eval_fen("4k3/4P3/8/8/8/8/8/4K3 w - - 0 1") >
           eval_fen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));

    // In the middlegame the king is safer on g1 after castling than on e1.
    assert(eval_fen("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQ1RK1 w kq - 0 1") >
           eval_fen("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 1"));
}

// Two rules that hold in every position. The mirror scores the same: eval knows nothing about
// colors except who is to move. Passing the move to the other side negates the score: eval
// sees no tempo, and integer division rounds toward zero, so -x / 24 == -(x / 24). The en
// passant square may not match the new side to move, which eval ignores.
static void check_symmetry(const Position &pos) {
    const int score = eval(pos);
    const int mirrored = eval(mirror(pos));

    Position other_side = pos;
    other_side.side_to_move = ~pos.side_to_move;
    const int flipped = eval(other_side);

    if (mirrored != score || flipped != -score) {
        std::fprintf(stderr, "%s\neval %d, mirrored %d, other side to move %d\n",
                     to_fen(pos).c_str(), score, mirrored, flipped);
        assert(false);
    }
}

static int check_tree(const Position &pos, int depth) {
    check_symmetry(pos);
    if (depth == 0)
        return 1;

    int positions = 1;
    MoveList list;
    generate_moves(pos, list);
    for (Move m : list) {
        Position next = pos;
        if (next.make_move(m))
            positions += check_tree(next, depth - 1);
    }
    return positions;
}

static void test_symmetry() {
    int positions = 0;
    for (const char *fen : TREE_FENS) {
        Position pos;
        assert(parse_fen(fen, pos));
        positions += check_tree(pos, 2);
    }
    // Step 2.1 asks for at least 100 positions; two plies give several thousand.
    assert(positions >= 100);
}

int main() {
    init_attacks();
    init_eval();
    test_mirror_helper();
    test_start_position();
    test_known_values();
    test_orientation();
    test_symmetry();
    std::puts("test_eval: OK");
    return 0;
}