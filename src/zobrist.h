#pragma once
#include "bitboard.h"
#include "position.h"
#include "types.h"
#include <cstdint>

// Zobrist hashing: one random 64-bit key per feature of a position, and the hash
// of a position is the XOR of the keys of the features it has.
struct ZobristKeys {
    uint64_t piece[COLOR_NB][PIECE_TYPE_NB][64];
    uint64_t castling[16];
    uint64_t ep_file[8];
    // Black to move.
    uint64_t side;
};

namespace zobrist_detail {
// SplitMix64 (Steele, Lea and Flood, 2014): a small generator with well-mixed output. Written
// out here rather than std::mt19937_64 so the keys can be computed at compile time, and are the
// same with every compiler: MinGW at home, GCC and Clang in CI, the bot's server.
constexpr uint64_t splitmix64(uint64_t &state) {
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

constexpr ZobristKeys make_keys() {
    // Any fixed seed works; changing it changes every hash, and with it the pinned value in
    // tests/test_zobrist.cpp.
    uint64_t state = 0x5EED5EED5EED5EEDULL;
    ZobristKeys keys{};
    for (auto &by_color : keys.piece)
        for (auto &by_type : by_color)
            for (uint64_t &key : by_type)
                key = splitmix64(state);
    for (uint64_t &key : keys.castling)
        key = splitmix64(state);
    for (uint64_t &key : keys.ep_file)
        key = splitmix64(state);
    keys.side = splitmix64(state);
    return keys;
}
} // namespace zobrist_detail

// Computed by the compiler: there is nothing to initialise at startup.
inline constexpr ZobristKeys ZOBRIST = zobrist_detail::make_keys();

// The hash of `pos` computed from scratch, ignoring pos.hash. parse_fen sets pos.hash with it;
// make_move keeps it up to date incrementally, and the tests compare the two.
inline uint64_t compute_hash(const Position &pos) {
    uint64_t hash = 0;
    for (Color c : {WHITE, BLACK}) {
        for (int pt = PAWN; pt <= KING; pt++) {
            Bitboard bb = pos.pieces[c][pt];
            while (bb)
                hash ^= ZOBRIST.piece[c][pt][pop_lsb(bb)];
        }
    }
    hash ^= ZOBRIST.castling[pos.castling_rights];
    if (pos.ep_square != NO_SQUARE)
        hash ^= ZOBRIST.ep_file[file_of(pos.ep_square)];
    if (pos.side_to_move == BLACK)
        hash ^= ZOBRIST.side;
    return hash;
}