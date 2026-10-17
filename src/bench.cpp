#include "bench.h"
#include "position.h"
#include "search.h"
#include <chrono>
#include <cstddef>
#include <ostream>

uint64_t bench(int depth, std::ostream &out) {
    uint64_t total = 0;
    const auto start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < BENCH_POSITIONS.size(); ++i) {
        Position pos;
        if (!parse_fen(BENCH_POSITIONS[i], pos)) {
            out << "invalid bench position: " << BENCH_POSITIONS[i] << '\n';
            return 0;
        }
        // Every position starts from a clean search: search() resets its counters. Once the
        // transposition table exists (step 4.3), clear it here as well, or each count would
        // depend on the positions searched before it.
        const uint64_t nodes = search(pos, depth).nodes;
        total += nodes;
        out << "position " << (i + 1 < 10 ? " " : "") << i + 1 << "  nodes " << nodes << '\n';
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    // A run shorter than a microsecond reports 0 nodes per second instead of dividing by zero.
    const uint64_t nps = us > 0 ? total * 1'000'000 / uint64_t(us) : 0;

    out << "depth " << depth << '\n'
        << "nodes " << total << '\n'
        << "time  " << us / 1000 << " ms\n"
        << "nps   " << nps << '\n';
    return total;
}