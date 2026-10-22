#include "search.h"
#include "tt.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

// Checks the transposition table on its own, before the search uses it: what probe finds after
// store, the mate score correction, the empty table and resizing.

static TTEntry probed(const TranspositionTable &t, uint64_t key, int ply) {
    TTEntry e;
    assert(t.probe(key, ply, e));
    return e;
}

// 1 MB holds 65 536 entries of 16 bytes; other sizes round down to a power of two.
static void test_sizes() {
    assert(TranspositionTable(1).table.size() == 65536);
    assert(TranspositionTable(16).table.size() == 1048576);
    assert(TranspositionTable(3).table.size() == 131072);
    assert(TranspositionTable().table.empty());
    assert(TranspositionTable(0).table.empty());
    assert(tt.table.size() == DEFAULT_HASH_MB * 1024 * 1024 / sizeof(TTEntry));
}

// What store writes, probe gives back, for the same key only.
static void test_store_and_probe() {
    TranspositionTable t(1);
    TTEntry e;
    assert(!t.probe(0x1234, 0, e));

    const Move m(E2, E4, DOUBLE_PUSH);
    t.store(0x1234, m, -57, 6, Bound::UPPER, 2);
    e = probed(t, 0x1234, 5);
    assert(e.key == 0x1234 && e.move == m && e.score == -57 && e.depth == 6 &&
           e.bound == Bound::UPPER);

    // The same index (the low 16 bits) with another key: not this position.
    assert(!t.probe(0x1234 + (uint64_t(1) << 40), 0, e));

    // A new entry at the index replaces the old one, whatever its depth.
    t.store(0x1234 + (uint64_t(1) << 40), Move(), 300, 1, Bound::LOWER, 0);
    assert(!t.probe(0x1234, 0, e));
    assert(probed(t, 0x1234 + (uint64_t(1) << 40), 0).score == 300);
}

// A mate found at ply 3 that falls at ply 5 from the root is 2 plies from its position; read at
// ply 7 it falls at ply 9. The same for a mate against the side to move, with the signs turned.
// Other scores, the largest evaluations included, pass unchanged.
static void test_mate_scores() {
    TranspositionTable t(1);
    t.store(1, Move(), MATE - 5, 3, Bound::EXACT, 3);
    assert(t.table[1].score == MATE - 2);
    assert(probed(t, 1, 7).score == MATE - 9);
    assert(probed(t, 1, 3).score == MATE - 5);

    t.store(2, Move(), -MATE + 5, 3, Bound::EXACT, 3);
    assert(t.table[2].score == -MATE + 2);
    assert(probed(t, 2, 7).score == -MATE + 9);

    for (const int score : {0, 1, -1, 2500, -2500, MATE - MAX_PLY - 1, -MATE + MAX_PLY + 1}) {
        t.store(3, Move(), score, 1, Bound::EXACT, 10);
        assert(probed(t, 3, 20).score == score);
    }
}

// Size 0 means no table: store does nothing and probe finds nothing.
static void test_empty_table() {
    TranspositionTable t;
    t.store(5, Move(), 10, 1, Bound::EXACT, 0);
    TTEntry e;
    assert(!t.probe(5, 0, e));
}

// clear and resize leave no entry behind.
static void test_clear_and_resize() {
    TranspositionTable t(1);
    t.store(9, Move(), 10, 1, Bound::EXACT, 0);
    t.clear();
    TTEntry e;
    assert(!t.probe(9, 0, e));
    assert(t.table.size() == 65536);

    t.store(9, Move(), 10, 1, Bound::EXACT, 0);
    t.resize(2);
    assert(t.table.size() == 131072);
    assert(!t.probe(9, 0, e));

    t.resize(0);
    assert(t.table.empty());
    assert(!t.probe(9, 0, e));
}

int main() {
    test_sizes();
    test_store_and_probe();
    test_mate_scores();
    test_empty_table();
    test_clear_and_resize();
    std::puts("test_tt: OK");
    return 0;
}
