#include "bitboard.h"
#include "position.h"
#include "attacks.h"
#include "types.h"
#include "perft.h"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

constexpr std::string_view USAGE = "usage: engine perft <depth> [fen]\n";

// engine perft <depth> [fen]: counts the positions `depth` plies deep and reports the speed.
// The FEN may be one quoted argument or six separate ones; without it the start position is
// used. Report speed only from a Release build.
static int run_perft(int argc, char **argv) {
    int depth = 0;
    const std::string_view depth_arg = argc > 2 ? argv[2] : "";
    const char *end = depth_arg.data() + depth_arg.size();
    const auto [ptr, ec] = std::from_chars(depth_arg.data(), end, depth);
    if (ec != std::errc() || ptr != end || depth < 1) {
        std::cerr << USAGE;
        return 1;
    }

    std::string fen(START_FEN);
    if (argc > 3) {
        fen = argv[3];
        for (int i = 4; i < argc; ++i) {
            fen += ' ';
            fen += argv[i];
        }
    }

    Position pos;
    if (!parse_fen(fen, pos)) {
        std::cerr << "invalid FEN: " << fen << '\n';
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    const uint64_t nodes = perft(pos, depth);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    // A run shorter than a microsecond reports 0 nodes per second instead of dividing by zero.
    const uint64_t nps = us > 0 ? nodes * 1'000'000 / uint64_t(us) : 0;

    std::cout << "fen   " << to_fen(pos) << '\n'
              << "depth " << depth << '\n'
              << "nodes " << nodes << '\n'
              << "time  " << us / 1000 << " ms\n"
              << "nps   " << nps << '\n';
    return 0;
}

int main(int argc, char **argv) {
    init_attacks();

    if (argc > 1) {
        if (std::string_view(argv[1]) == "perft")
            return run_perft(argc, argv);
        std::cerr << "unknown command: " << argv[1] << '\n' << USAGE;
        return 1;
    }


    return 0;
}