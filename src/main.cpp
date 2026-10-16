#include "attacks.h"
#include "bitboard.h"
#include "eval.h"
#include "movegen.h"
#include "perft.h"
#include "position.h"
#include "types.h"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

constexpr std::string_view USAGE = "usage: engine perft <depth> [fen] [moves m1 m2 ...]\n"
                                   "       engine divide <depth> [fen] [moves m1 m2 ...]\n";

// Reads "<depth> [fen] [moves m1 m2 ...]" from argv[2] onwards. The FEN may be one quoted
// argument or six separate ones; without it the start position is used. As in the UCI
// position command, the moves after the word "moves" are played before counting. On bad
// input prints the reason and returns false.
static bool parse_args(int argc, char **argv, int &depth, Position &pos) {
    const std::string_view depth_arg = argc > 2 ? argv[2] : "";
    const char *end = depth_arg.data() + depth_arg.size();
    const auto [ptr, ec] = std::from_chars(depth_arg.data(), end, depth);
    if (ec != std::errc() || ptr != end || depth < 1) {
        std::cerr << USAGE;
        return false;
    }

    // Everything up to the word "moves" belongs to the FEN.
    int i = 3;
    std::string fen;
    for (; i < argc && std::string_view(argv[i]) != "moves"; ++i) {
        if (!fen.empty())
            fen += ' ';
        fen += argv[i];
    }
    if (fen.empty())
        fen = START_FEN;

    if (!parse_fen(fen, pos)) {
        std::cerr << "invalid FEN: " << fen << '\n';
        return false;
    }

    // `i` stands on the word "moves" (or past the end), so the list starts one further.
    // Each move is tried on a copy: make_move changes the position even when it returns false,
    // and the error message should show the position the move was played from.
    for (++i; i < argc; ++i) {
        const Move m = parse_uci_move(pos, argv[i]);
        Position next = pos;
        if (m == Move() || !next.make_move(m)) {
            std::cerr << "illegal move: " << argv[i] << " in " << to_fen(pos) << '\n';
            return false;
        }
        pos = next;
    }
    return true;
}

// engine perft <depth> [fen] [moves ...]: counts the positions `depth` plies deep and reports
// the speed. Report speed only from a Release build.
static int run_perft(int argc, char **argv) {
    int depth = 0;
    Position pos;
    if (!parse_args(argc, argv, depth, pos))
        return 1;

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

// engine divide <depth> [fen] [moves ...]: the node count under every legal move and the
// total, in the format of Stockfish's "go perft", so the two can be compared line by line.
static int run_divide(int argc, char **argv) {
    int depth = 0;
    Position pos;
    if (!parse_args(argc, argv, depth, pos))
        return 1;
    divide(pos, depth, std::cout);
    return 0;
}

int main(int argc, char **argv) {
    init_attacks();
    init_eval(); 

    if (argc > 1) {
        const std::string_view command = argv[1];
        if (command == "perft")
            return run_perft(argc, argv);
        if (command == "divide")
            return run_divide(argc, argv);
        std::cerr << "unknown command: " << command << '\n' << USAGE;
        return 1;
    }

    return 0;
}