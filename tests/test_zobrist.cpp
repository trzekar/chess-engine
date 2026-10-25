#include "attacks.h"
#include "movegen.h"
#include "position.h"
#include "zobrist.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <random>
#include <set>
#include <string>

// Checks the keys, compute_hash, and the incremental update of the hash in make_move and
// make_null_move against compute_hash.

static Position from_fen(const char *fen) {
    Position pos;
    assert(parse_fen(fen, pos));
    return pos;
}

// All 793 keys are non-zero and distinct: a zero key would leave its feature out of the hash,
// and two equal keys would make two features cancel each other.
static void test_keys() {
    std::set<uint64_t> seen;
    auto add = [&seen](uint64_t key) {
        assert(key != 0);
        assert(seen.insert(key).second);
    };
    for (const auto &by_color : ZOBRIST.piece)
        for (const auto &by_type : by_color)
            for (uint64_t key : by_type)
                add(key);
    for (uint64_t key : ZOBRIST.castling)
        add(key);
    for (uint64_t key : ZOBRIST.ep_file)
        add(key);
    add(ZOBRIST.side);
    assert(seen.size() == 2 * 6 * 64 + 16 + 8 + 1);
}

// The keys are computed at compile time with a fixed generator and seed, so every compiler and
// machine gives the same hashes. A pinned value catches a change of the generator, the seed or
// the order of the keys. If one of them changes on purpose, recompute the value; never copy it
// from a failing run without knowing why it changed.
static void test_pinned_value() {
    const Position start = from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    if (start.hash != 0x4CC0DD96EFB9BC12ULL) {
        // iostream rather than printf: MinGW's printf rejects %llx under -Wformat=2 -Werror.
        std::cerr << "start position hash: 0x" << std::hex << std::uppercase << start.hash << '\n';
        assert(false);
    }
}

// parse_fen stores the hash computed from scratch.
static void test_parse_fen_sets_hash() {
    for (const char *fen : {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"}) {
        const Position pos = from_fen(fen);
        assert(pos.hash != 0);
        assert(pos.hash == compute_hash(pos));
    }
}

// Each feature changes the hash by exactly its key: the side to move, each castling right, the
// en passant file, a piece on a square. The half-move and move counters change nothing.
static void test_features() {
    const char *start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const uint64_t h = from_fen(start).hash;

    assert((h ^ from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1").hash) ==
           ZOBRIST.side);

    // KQkq is index 15; dropping one right moves to another index.
    assert((h ^ from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w Qkq - 0 1").hash) ==
           (ZOBRIST.castling[15] ^ ZOBRIST.castling[15 & ~WHITE_KINGSIDE]));
    assert((h ^ from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1").hash) ==
           (ZOBRIST.castling[15] ^ ZOBRIST.castling[0]));

    // The same position after 1. e4, with and without the en passant square in the FEN.
    const uint64_t with_ep =
        from_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1").hash;
    const uint64_t without_ep =
        from_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1").hash;
    assert((with_ep ^ without_ep) == ZOBRIST.ep_file[file_of(E3)]);

    // The pawn from e2 on e4.
    assert((without_ep ^ h) ==
           (ZOBRIST.piece[WHITE][PAWN][E2] ^ ZOBRIST.piece[WHITE][PAWN][E4] ^ ZOBRIST.side));

    // Counters are not part of the position.
    assert(from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 37 52").hash == h);
}

// Plays `moves` (UCI names) from `fen`; every move must be legal.
static Position play(const char *fen, std::initializer_list<const char *> moves) {
    Position pos = from_fen(fen);
    for (const char *uci : moves) {
        const Move m = parse_uci_move(pos, uci);
        assert(m != Move());
        assert(pos.make_move(m));
    }
    return pos;
}

// On a mismatch, prints where the walk went wrong before failing: the position before the move
// and the move. Its FEN and `engine divide 1` show which feature was XOR-ed wrongly.
static void expect_hash(const Position &before, Move m, const Position &after) {
    if (after.hash == compute_hash(after))
        return;
    std::cerr << "incremental hash differs from compute_hash\nbefore: " << to_fen(before)
              << "\nmove:   " << to_uci(m) << "\nafter:  " << to_fen(after) << '\n';
    assert(false);
}

// Random games: after every legal move the hash make_move keeps equals the hash computed from
// scratch. The start positions cover castling (both sides, and rights lost when a rook is
// captured on its corner), en passant and promotions with and without capture. A fixed seed
// makes every run play the same moves; std::mt19937_64 gives the same numbers on every
// compiler, and the move is picked with % rather than a distribution, which may differ.
static void test_random_walks() {
    static const char *const STARTS[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r3k2r/1P6/8/8/8/8/1p6/R3K2R w KQkq - 0 1",
    };
    std::mt19937_64 rng(20261022);
    int moves_checked = 0;
    for (const char *fen : STARTS) {
        Position pos = from_fen(fen);
        for (int ply = 0; ply < 2000; ++ply) {
            MoveList list;
            generate_moves(pos, list);
            Position legal[256];
            Move played[256];
            int count = 0;
            for (Move m : list) {
                Position next = pos;
                if (next.make_move(m)) {
                    legal[count] = next;
                    played[count] = m;
                    ++count;
                }
            }
            // Every legal move is checked, not only the one played: over 100 000 moves in all.
            for (int i = 0; i < count; ++i)
                expect_hash(pos, played[i], legal[i]);
            moves_checked += count;
            // Mate or stalemate: start the game again.
            if (count == 0) {
                pos = from_fen(fen);
                continue;
            }
            pos = legal[rng() % uint64_t(count)];
        }
    }
    assert(moves_checked > 100000);
}

// Two move orders reaching the same position give the same hash, and four knight moves that
// come back give the hash of the start: what repetition detection relies on.
static void test_transpositions() {
    const char *start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    assert(play(start, {"g1f3", "g8f6", "b1c3"}).hash ==
           play(start, {"b1c3", "g8f6", "g1f3"}).hash);
    // Neither order ends with a double push, so neither has an en passant square.
    assert(play(start, {"e2e4", "b8c6", "g1f3", "g8f6"}).hash ==
           play(start, {"g1f3", "g8f6", "e2e4", "b8c6"}).hash);
    assert(play(start, {"g1f3", "g8f6", "f3g1", "f6g8"}).hash == from_fen(start).hash);

    // The same squares after 1. e4 e5 2. Ke2 Ke7 3. Ke1 Ke8, but without castling rights: a
    // different position, so a different hash.
    assert(play(start, {"e2e4", "e7e5", "e1e2", "e8e7", "e2e1", "e7e8"}).hash !=
           play(start, {"e2e4", "e7e5"}).hash);

    // e4 then e5 directly, or each pawn in two steps: the same squares, but only the second
    // order has no en passant square at the end.
    const Position direct = play(start, {"e2e4", "g8f6", "g1f3", "f6g8", "f3g1", "e7e5"});
    const Position stepwise = play(start, {"e2e3", "e7e6", "e3e4", "e6e5"});
    assert(direct.hash != stepwise.hash);
    assert((direct.hash ^ stepwise.hash) == ZOBRIST.ep_file[file_of(E6)]);
}

// make_null_move: the side to move changes, the en passant square goes, the half-move counter
// restarts and the move number grows after Black; pieces and rights stay. The hash changes by
// exactly the side key and the en passant key. Two passes in a row give back the position
// without its en passant square. No position here is in check: the search never passes there.
static void test_null_move() {
    struct Case {
        const char *before;
        const char *after;
    };
    static const Case CASES[] = {
        // After 1. e4: Black passes, the en passant square goes and the move number grows.
        {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
         "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"},
        // White could take en passant on f6 and passes instead.
        {"rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
         "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3"},
        // Kiwipete: all four castling rights stay.
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1"},
        // Fine #70 with a running half-move counter: White passes, the counter restarts.
        {"8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 7 30", "8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 b - - 0 30"},
        // Two of the four castling rights; Black passes with a running counter.
        {"r3k2r/8/8/8/8/8/8/R3K2R b Kq - 12 40", "r3k2r/8/8/8/8/8/8/R3K2R w Kq - 0 41"},
    };
    for (const Case &c : CASES) {
        const Position before = from_fen(c.before);
        assert(!before.in_check(before.side_to_move));
        const uint64_t ep_key =
            before.ep_square == NO_SQUARE ? 0 : ZOBRIST.ep_file[file_of(before.ep_square)];

        Position after = before;
        after.make_null_move();
        if (to_fen(after) != c.after || after.hash != compute_hash(after)) {
            std::cerr << "make_null_move\nbefore:   " << c.before << "\nafter:    " << to_fen(after)
                      << "\nexpected: " << c.after << '\n';
            assert(false);
        }
        assert(after.hash == from_fen(c.after).hash);
        assert((before.hash ^ after.hash) == (ZOBRIST.side ^ ep_key));

        // A second pass in a row: the same pieces, rights and side to move as before, without
        // the en passant square.
        Position twice = after;
        twice.make_null_move();
        assert(twice.hash == compute_hash(twice));
        assert(twice.hash == (before.hash ^ ep_key));
        assert(twice.side_to_move == before.side_to_move);
        assert(twice.castling_rights == before.castling_rights);
        assert(twice.ep_square == NO_SQUARE);
        for (Color col : {WHITE, BLACK})
            for (int pt = PAWN; pt <= KING; pt++)
                assert(twice.pieces[col][pt] == before.pieces[col][pt]);
    }
}

int main() {
    init_attacks();
    test_keys();
    test_pinned_value();
    test_parse_fen_sets_hash();
    test_features();
    test_random_walks();
    test_transpositions();
    test_null_move();
    std::puts("test_zobrist: OK");
    return 0;
}