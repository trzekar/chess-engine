#include "attacks.h"
#include "movegen.h"
#include <cassert>
#include <cstdio>
#include <string>

// Expected FENs come from an independent implementation (python-chess 1.10.0). They record the
// en passant square after every double push, as the FEN standard does. Stockfish's "d" command
// shows it only when an en passant capture is possible, so its FENs can differ in that field.

struct Case {
    const char *fen;
    const char *move;  // UCI notation
    const char *after; // FEN after the move, or nullptr when make_move must reject it
};

static const Case CASES[] = {
    // 1. Quiet piece move: the side to move changes, the half-move counter grows.
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "g1f3",
     "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1"},
    // 2. Double push: sets the en passant square behind the pawn.
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "e2e4",
     "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"},
    // 3. Black moves: the en passant square is cleared and the move number grows.
    {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", "g8f6",
     "rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2"},
    // 4. Capture: resets the half-move counter.
    {"rnbqkbnr/ppp1pppp/8/3p4/4P3/2N5/PPPP1PPP/R1BQKBNR w KQkq - 3 4", "e4d5",
     "rnbqkbnr/ppp1pppp/8/3P4/8/2N5/PPPP1PPP/R1BQKBNR b KQkq - 0 4"},
    // 5. En passant by White: the captured pawn stands on f5, not on f6.
    {"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", "e5f6",
     "rnbqkbnr/ppp1p1pp/5P2/3p4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3"},
    // 6. En passant by Black: the captured pawn stands on d4, not on d3.
    {"rnbqkbnr/pp1ppppp/8/8/2pPP3/8/PPP2PPP/RNBQKBNR b KQkq d3 0 3", "c4d3",
     "rnbqkbnr/pp1ppppp/8/8/4P3/3p4/PPP2PPP/RNBQKBNR w KQkq - 0 4"},
    // 7. White castles kingside: the rook jumps h1 -> f1, White loses both rights.
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1", "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1"},
    // 8. White castles queenside: the rook jumps a1 -> d1.
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1c1", "r3k2r/8/8/8/8/8/8/2KR3R b kq - 1 1"},
    // 9. Black castles kingside: the rook jumps h8 -> f8.
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8g8", "r4rk1/8/8/8/8/8/8/R3K2R w KQ - 1 2"},
    // 10. Black castles queenside: the rook jumps a8 -> d8.
    {"r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8", "2kr3r/8/8/8/8/8/8/R3K2R w KQ - 1 2"},
    // 11. A king move loses both rights of its side.
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1e2", "r3k2r/8/8/8/8/8/4K3/R6R b kq - 1 1"},
    // 12. A rook move loses the right on its side only.
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "a1b1", "r3k2r/8/8/8/8/8/8/1R2K2R b Kkq - 1 1"},
    // 13. Rook takes rook in the corner: the mask of `from` clears Q, the mask of `to` clears q.
    {"r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "a1a8", "R3k2r/8/8/8/8/8/8/4K2R b Kk - 0 1"},
    // 14. Promotion: the pawn becomes a queen.
    {"4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7b8q", "1Q2k3/8/8/8/8/8/8/4K3 b - - 0 1"},
    // 15. Underpromotion with a capture in the corner: Black loses q.
    {"r3k3/1P6/8/8/8/8/8/4K3 w q - 0 1", "b7a8n", "N3k3/8/8/8/8/8/8/4K3 b - - 0 1"},
    // 16. Black promotes with a capture in the corner: White loses Q.
    {"4k3/8/8/8/8/8/1p6/R3K3 b Q - 0 1", "b2a1q", "4k3/8/8/8/8/8/8/q3K3 w - - 0 2"},
    // 17. A pinned bishop leaves the e-file.
    {"4k3/4r3/8/8/8/8/4B3/4K3 w - - 0 1", "e2d3", nullptr},
    // 18. The king steps onto a square the rook attacks.
    {"4k3/8/8/8/8/8/3r4/4K3 w - - 0 1", "e1e2", nullptr},
    // 19. Castling onto an attacked square: generated, then rejected here.
    {"4k1r1/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1g1", nullptr},
    // 20. En passant takes two pawns off the fifth rank and exposes the king to the rook.
    {"8/8/8/K2pP2r/8/8/8/7k w - d6 0 1", "e5d6", nullptr},
};

// The standard perft positions (chessprogramming.org/Perft_Results). Together they contain
// every kind of move, so walking their trees checks make_move on many more moves than CASES.
static const char *const TREE_FENS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
};

// make_move only ever sees moves from generate_moves, so every test move must be generated.
static Move find_move(const Position &pos, const char *uci) {
    MoveList list;
    generate_moves(pos, list);
    for (Move m : list)
        if (to_uci(m) == uci)
            return m;
    std::fprintf(stderr, "%s\nmove %s is not generated\n", to_fen(pos).c_str(), uci);
    assert(false);
    return Move();
}

// The bitboards per piece, `occupancy` per color and `piece_on` per square must describe the
// same board. Both kings must still be on it.
static bool is_consistent(const Position &pos) {
    if (pos.occupancy[WHITE] & pos.occupancy[BLACK])
        return false;

    for (int c = 0; c < COLOR_NB; ++c) {
        Bitboard all = 0;
        for (int pt = 0; pt < PIECE_TYPE_NB; ++pt) {
            if (all & pos.pieces[c][pt])
                return false;
            all |= pos.pieces[c][pt];
        }
        if (all != pos.occupancy[c] || popcount(pos.pieces[c][KING]) != 1)
            return false;
    }

    for (int sq = 0; sq < 64; ++sq) {
        const Square s = Square(sq);
        const PieceType pt = pos.piece_at(s);
        if (pt == NO_PIECE_TYPE) {
            if (pos.occupied() & square_bb(s))
                return false;
            continue;
        }
        const Color c = (pos.occupancy[WHITE] & square_bb(s)) ? WHITE : BLACK;
        if (!(pos.pieces[c][pt] & square_bb(s)))
            return false;
    }
    return true;
}

// On a failure, prints the position and the move before failing, so the case can be set up
// in Stockfish ("position fen <fen> moves <move>", then "d") or on a board. Messages go to
// stderr: it is unbuffered, so they survive the abort even when ctest pipes the output.
static void require(bool ok, const char *what, const Position &pos, Move m) {
    if (ok)
        return;
    std::fprintf(stderr, "%s\nmove %s: %s\n", to_fen(pos).c_str(), to_uci(m).c_str(), what);
    assert(false);
}

static void test_cases() {
    for (const Case &c : CASES) {
        Position pos;
        assert(parse_fen(c.fen, pos));
        const Move m = find_move(pos, c.move);

        Position next = pos;
        const bool legal = next.make_move(m);

        // Copy-make: the move is played on the copy, the original stays as it was.
        require(to_fen(pos) == c.fen, "the original position changed", pos, m);

        const std::string got = legal ? to_fen(next) : "rejected";
        const std::string expected = c.after ? c.after : "rejected";
        if (got != expected) {
            std::fprintf(stderr, "%s\nmove %s: got %s, expected %s\n", c.fen, c.move, got.c_str(),
                         expected.c_str());
            assert(false);
        }
        if (legal)
            require(is_consistent(next), "board representations disagree", pos, m);
    }
}

// Rules that hold after every accepted move, whatever the move. Each is checked against
// `pos` and the move alone, without repeating how make_move gets there.
static void check_move(const Position &pos, Move m, const Position &next) {
    const Color us = pos.side_to_move;
    const Color them = ~us;

    require(is_consistent(next), "board representations disagree", pos, m);
    require(next.side_to_move == them, "the side to move did not change", pos, m);
    require(!next.is_attacked(lsb(next.pieces[us][KING]), them), "own king left in check", pos, m);

    // Castling rights are only ever lost.
    require((next.castling_rights & ~pos.castling_rights) == 0, "a castling right appeared", pos,
            m);

    // Only a double push leaves an en passant square: the one the pawn skipped.
    const Square ep = m.flag() == DOUBLE_PUSH ? Square((m.from() + m.to()) / 2) : NO_SQUARE;
    require(next.ep_square == ep, "wrong en passant square", pos, m);

    // The fifty-move counter restarts on captures and pawn moves.
    const bool reset = m.is_capture() || pos.piece_at(m.from()) == PAWN;
    require(next.half_move_counter == (reset ? 0 : pos.half_move_counter + 1),
            "wrong half-move counter", pos, m);

    // The move number grows after Black's move.
    require(next.move_number == pos.move_number + (us == BLACK), "wrong move number", pos, m);

    // A capture removes exactly one enemy piece. Own pieces are never added or removed:
    // a promotion swaps the pawn for a piece, castling moves two pieces.
    require(popcount(next.occupancy[them]) == popcount(pos.occupancy[them]) - m.is_capture(),
            "wrong number of enemy pieces", pos, m);
    require(popcount(next.occupancy[us]) == popcount(pos.occupancy[us]),
            "wrong number of own pieces", pos, m);
}

static void check_tree(const Position &pos, int depth) {
    MoveList list;
    generate_moves(pos, list);
    for (Move m : list) {
        Position next = pos;
        if (!next.make_move(m))
            continue;
        check_move(pos, m, next);
        if (depth > 1)
            check_tree(next, depth - 1);
    }
}

static void test_move_rules() {
    for (const char *fen : TREE_FENS) {
        Position pos;
        assert(parse_fen(fen, pos));
        check_tree(pos, 3);
    }
}

int main() {
    init_attacks();
    test_cases();
    test_move_rules();
    std::puts("test_make_move: OK");
    return 0;
}
