#pragma once
#include "types.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// Transposition table: results of positions already searched, indexed by their Zobrist hash.
// A fixed-size cache, not a dictionary: a new entry always overwrites the one at its index.
// chessprogramming.org/Transposition_Table

// The size of the table at startup and the default of the UCI option Hash, in MB.
constexpr size_t DEFAULT_HASH_MB = 16;

// What the stored score says about the true score of the position.
enum class Bound : uint8_t {
    NONE,  // an empty entry
    EXACT,
    LOWER, // the search cut off: the true score is at least this
    UPPER, // no move raised alpha: the true score is at most this
};

struct TTEntry {
    // The full hash of the position: two positions with the same index differ here.
    uint64_t key = 0;
    // The best move, or the move that caused the cutoff. Move() when there is none.
    Move move = {};
    // From the point of view of the side to move. Mate scores count the plies from this
    // position, not from the root (see store).
    int16_t score = 0;
    uint8_t depth = 0;
    Bound bound = Bound::NONE;
};

// 14 bytes of fields padded to 16: four entries per cache line, none split across two.
static_assert(sizeof(TTEntry) == 16);

struct TranspositionTable {
    // An empty table: probe never finds anything and store does nothing.
    TranspositionTable() = default;
    explicit TranspositionTable(size_t mb) { resize(mb); }

    // Makes room for `mb` megabytes of entries, rounded down to a power of two so the index is
    // `key & mask`, and empties the table. 0 means no table at all.
    void resize(size_t mb);

    // Empties every entry, keeping the size.
    void clear();

    // False if the table holds no entry for `key`. A mate score in `out` is re-anchored to the
    // root, ready for a node at `ply`.
    bool probe(uint64_t key, int ply, TTEntry &out) const;

    // Stores a result found at `ply`, overwriting whatever was at the index.
    void store(uint64_t key, Move m, int score, int depth, Bound bound, int ply);

    std::vector<TTEntry> table = {};
    // table.size() - 1; unused while the table is empty.
    size_t mask = 0;
};

// The engine's one table. Resized by the UCI option Hash, cleared by ucinewgame and search().
extern TranspositionTable tt;
