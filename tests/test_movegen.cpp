#include "attacks.h"
#include "movegen.h"
#include <cassert>
#include <cstdio>
#include <set>

// Expected counts come from an independent generator (python-chess), whose perft matches
// chessprogramming.org/Perft_Results. The moves are pseudo-legal: moves that leave the own
// king in check are included, and so is castling onto an attacked square, which make_move
// rejects later.

struct Expected {
    const char *fen;
    int total;         // all generated moves
    int pieces;        // knight, bishop, rook, queen and king moves, castling excluded
    int captures;      // moves with the capture bit, en passant and capture promotions included
    int double_pushes; // DOUBLE_PUSH flag
    int castles;       // KING_CASTLE or QUEEN_CASTLE flag
};

static const Expected POSITIONS[] = {
    // Start position.
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 20, 4, 0, 8, 0},
    // Kiwipete: both castles, pawns on c2, f2 and h2 blocked.
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 48, 38, 8, 2, 2},
    // Kiwipete with black to move: pawn direction and black castling.
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1", 43, 33, 7, 1, 2},
    // Perft position 3: a pinned pawn and a king step into check stay in the list.
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 16, 11, 1, 2, 0},
    // Perft position 4: white is in check, which the generator ignores.
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 38, 30, 3, 3, 0},
    // Perft position 5: capture promotions d7xc8.
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 44, 30, 6, 4, 1},
    // Promotions for white: four pushes and four captures.
    {"r3k3/1P6/8/8/8/8/8/4K3 w q - 0 1", 13, 5, 4, 0, 0},
    // Promotions for black.
    {"4k3/8/8/8/8/8/1p6/R3K3 b Q - 0 1", 13, 5, 4, 0, 0},
    // En passant e5xf6.
    {"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3", 31, 15, 1, 7, 0},
    // En passant c4xd3.
    {"rnbqkbnr/pp1ppppp/8/8/2pPP3/8/PPP2PPP/RNBQKBNR b KQkq d3 0 3", 23, 7, 1, 7, 0},
    // Both castles allowed.
    {"4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", 26, 24, 0, 0, 2},
    // f1 attacked: no kingside castling through it.
    {"4kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1", 25, 24, 0, 0, 1},
    // g1 attacked: kingside castling is generated, make_move rejects it.
    {"4k1r1/8/8/8/8/8/8/R3K2R w KQ - 0 1", 26, 24, 0, 0, 2},
    // b1 attacked: queenside castling is still allowed, only the rook crosses b1.
    {"1r2k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", 26, 24, 0, 0, 2},
    // In check: no castling out of check.
    {"4r1k1/8/8/8/8/8/8/R3K2R w KQ - 0 1", 24, 24, 0, 0, 0},
};

static bool is_castle(Move m) { return m.flag() == KING_CASTLE || m.flag() == QUEEN_CASTLE; }

// On a mismatch, prints the FEN and the whole move list before failing, so the missing or
// extra move can be found by eye or compared with Stockfish's "go perft 1".
static void expect(const char *fen, const char *what, int got, int expected, const MoveList &list) {
    if (got == expected)
        return;
    std::printf("%s\n%s: got %d, expected %d\nmoves:", fen, what, got, expected);
    for (Move m : list)
        std::printf(" %s", to_uci(m).c_str());
    std::printf("\n");
    assert(false);
}

static void test_counts() {
    for (const Expected &e : POSITIONS) {
        Position pos;
        assert(parse_fen(e.fen, pos));
        MoveList list;
        generate_moves(pos, list);

        int pieces = 0, captures = 0, double_pushes = 0, castles = 0;
        for (Move m : list) {
            pieces += pos.piece_at(m.from()) != PAWN && !is_castle(m);
            captures += m.is_capture();
            double_pushes += m.flag() == DOUBLE_PUSH;
            castles += is_castle(m);
        }

        expect(e.fen, "total", list.size, e.total, list);
        expect(e.fen, "pieces", pieces, e.pieces, list);
        expect(e.fen, "captures", captures, e.captures, list);
        expect(e.fen, "double pushes", double_pushes, e.double_pushes, list);
        expect(e.fen, "castles", castles, e.castles, list);
    }
}

static void test_properties() {
    for (const Expected &e : POSITIONS) {
        Position pos;
        assert(parse_fen(e.fen, pos));
        MoveList list;
        generate_moves(pos, list);

        const Color us = pos.side_to_move;
        std::set<uint16_t> seen;
        for (Move m : list) {
            // No move is generated twice.
            assert(seen.insert(m.data).second);
            // The moving piece belongs to the side to move.
            assert(pos.occupancy[us] & square_bb(m.from()));
            // No move lands on an own piece.
            assert(!(pos.occupancy[us] & square_bb(m.to())));
            // The capture bit is set exactly when an enemy piece stands on the target square.
            // En passant is the exception: its target square is empty.
            const bool enemy_on_target = pos.occupancy[~us] & square_bb(m.to());
            if (m.flag() == EN_PASSANT_CAPTURE)
                assert(m.to() == pos.ep_square && !enemy_on_target);
            else
                assert(m.is_capture() == enemy_on_target);
        }
    }
}

static void test_to_uci() {
    assert(to_uci(Move(E2, E4, DOUBLE_PUSH)) == "e2e4");
    assert(to_uci(Move(E1, G1, KING_CASTLE)) == "e1g1");
    assert(to_uci(Move(E7, E8, QUEEN_PROMOTION)) == "e7e8q");
    assert(to_uci(Move(A7, B8, KNIGHT_PROMOTION_CAPTURE)) == "a7b8n");
    assert(to_uci(Move()) == "0000");
}

int main() {
    init_attacks();
    test_counts();
    test_properties();
    test_to_uci();
    std::puts("test_movegen: OK");
    return 0;
}