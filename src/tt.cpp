#include "tt.h"
#include "search.h"
#include <bit>
#include <cstdlib>

TranspositionTable tt(DEFAULT_HASH_MB);

namespace {
// Mate scores are stored relative to the node and re-anchored at the ply they are read from,
// so a mate stays correct when the position comes back at another ply. Anything farther than
// MAX_PLY from ±MATE is an evaluation and passes unchanged.
int score_from_tt(int tt_score, int ply) {
    if (MATE - std::abs(tt_score) > MAX_PLY) {
        return tt_score;
    }

    if (tt_score > 0) {
        return tt_score - ply;
    }
    return tt_score + ply;
}
int score_to_tt(int score, int ply) {
    if (MATE - std::abs(score) > MAX_PLY) {
        return score;
    }

    if (score > 0) {
        return score + ply;
    }
    return score - ply;
}
} // namespace

void TranspositionTable::resize(size_t mb) {
    size_t new_size = mb * 1024 * 1024 / sizeof(TTEntry);
    size_t table_size = std::bit_floor(new_size);
    // bit_floor(0) == 0: with mb = 0 the mask wraps around, harmless, as probe and store
    // check empty() first.
    mask = (table_size - 1);
    table.resize(table_size);
    clear();
}

void TranspositionTable::clear() {
    for (size_t i = 0; i < table.size(); i++) {
        table[i] = TTEntry();
    }
}

bool TranspositionTable::probe(uint64_t key, int ply, TTEntry &out) const {
    if (table.empty())
        return false;
    TTEntry entry = table[key & mask];
    if (key != entry.key)
        return false;

    out = entry;
    out.score = static_cast<int16_t>(score_from_tt(out.score, ply));
    return true;
}

void TranspositionTable::store(uint64_t key, Move m, int score, int depth, Bound bound, int ply) {
    if (table.empty())
        return;
    int new_score = score_to_tt(score, ply);
    table[key & mask] =
        TTEntry{key, m, static_cast<int16_t>(new_score), static_cast<uint8_t>(depth), bound};
}
