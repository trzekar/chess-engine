#include "attacks.h"
#include "position.h"
#include "zobrist.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <set>

// Checks the keys and compute_hash. make_move's incremental update is checked against
// compute_hash once it exists (step 4.2, block 5).

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

int main() {
    init_attacks();
    test_keys();
    test_pinned_value();
    test_parse_fen_sets_hash();
    test_features();
    std::puts("test_zobrist: OK");
    return 0;
}