#include "attacks.h"
#include "perft.h"
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Usage: test_perft <perft_positions.epd> [--deep]
//
// Compares perft with the known node counts in the EPD file. Every depth whose count fits
// under the node limit is checked, shallowest first, so a bug is reported at the smallest
// depth that shows it. The default limit keeps a sanitizer build to a few seconds; --deep is
// meant for Release builds and takes about half a minute.
//
// Output goes through iostream rather than printf: MinGW's printf does not accept %llu
// under -Wformat=2 -Werror.

constexpr uint64_t FAST_LIMIT = 5'000'000;
constexpr uint64_t DEEP_LIMIT = 200'000'000;

struct Count {
    int depth = 0;
    uint64_t nodes = 0;
};

struct Case {
    std::string fen;
    std::vector<Count> counts;
};

static std::string_view trim(std::string_view s) {
    const size_t first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

// Parses "<fen> ;D1 20 ;D2 400 ...". Returns false on a malformed line.
static bool parse_case(std::string_view line, Case &c) {
    size_t semi = line.find(';');
    if (semi == std::string_view::npos)
        return false;
    c.fen = std::string(trim(line.substr(0, semi)));

    while (semi != std::string_view::npos) {
        const size_t next = line.find(';', semi + 1);
        // substr clamps the length, so the last field (next == npos) runs to the end.
        const std::string_view field = trim(line.substr(semi + 1, next - semi - 1));
        const size_t space = field.find(' ');
        if (field.size() < 4 || field[0] != 'D' || space == std::string_view::npos)
            return false;

        Count count;
        const char *depth_end = field.data() + space;
        const auto [depth_ptr, depth_ec] = std::from_chars(field.data() + 1, depth_end, count.depth);
        const std::string_view nodes = trim(field.substr(space + 1));
        const char *nodes_end = nodes.data() + nodes.size();
        const auto [nodes_ptr, nodes_ec] = std::from_chars(nodes.data(), nodes_end, count.nodes);
        if (depth_ec != std::errc() || depth_ptr != depth_end || count.depth < 1 ||
            nodes_ec != std::errc() || nodes_ptr != nodes_end)
            return false;

        c.counts.push_back(count);
        semi = next;
    }
    return true;
}

// divide must print one "<move>: <nodes>" line per legal move, a blank line and
// "Nodes searched: <total>": the format of Stockfish's "go perft" that perft_diff.py reads.
static bool check_divide(const Position &pos, int depth, uint64_t expected) {
    std::ostringstream out;
    const uint64_t total = divide(pos, depth, out);

    std::istringstream in(out.str());
    std::string line;
    uint64_t sum = 0;
    while (std::getline(in, line) && !line.empty()) {
        // A UCI move has 4 characters, or 5 with a promotion letter.
        const size_t colon = line.find(": ");
        if (colon != 4 && colon != 5)
            return false;
        uint64_t nodes = 0;
        const char *end = line.data() + line.size();
        const auto [ptr, ec] = std::from_chars(line.data() + colon + 2, end, nodes);
        if (ec != std::errc() || ptr != end)
            return false;
        sum += nodes;
    }

    const bool summary_ok = std::getline(in, line) &&
                            line == "Nodes searched: " + std::to_string(expected);
    const bool nothing_after = !std::getline(in, line);
    return total == expected && sum == expected && summary_ok && nothing_after;
}

int main(int argc, char **argv) {
    const bool deep = argc == 3 && std::string_view(argv[2]) == "--deep";
    if (argc != 2 && !deep) {
        std::cerr << "usage: test_perft <perft_positions.epd> [--deep]\n";
        return 2;
    }
    const uint64_t limit = deep ? DEEP_LIMIT : FAST_LIMIT;

    std::ifstream file(argv[1]);
    if (!file) {
        std::cerr << "cannot open " << argv[1] << '\n';
        return 2;
    }

    init_attacks();

    int failures = 0;
    int checked = 0;
    int line_number = 0;
    std::string raw;
    while (std::getline(file, raw)) {
        ++line_number;
        const std::string_view line = trim(raw);
        if (line.empty() || line[0] == '#')
            continue;

        Case c;
        Position pos;
        if (!parse_case(line, c) || !parse_fen(c.fen, pos)) {
            std::cout << "FAIL line " << line_number << ": malformed\n  " << line << '\n';
            ++failures;
            continue;
        }

        for (const Count &count : c.counts) {
            if (count.nodes > limit)
                continue;

            const auto start = std::chrono::steady_clock::now();
            const uint64_t nodes = perft(pos, count.depth);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            ++checked;

            if (nodes != count.nodes) {
                std::cout << "FAIL line " << line_number << " depth " << count.depth << ": got "
                          << nodes << ", expected " << count.nodes << "\n  fen " << c.fen
                          << "\n  find the move with: engine divide " << count.depth << " \""
                          << c.fen << "\" and Stockfish's go perft\n";
                ++failures;
                // The deeper counts of this position contain the same error.
                break;
            }
            std::cout << "ok   line " << line_number << " depth " << count.depth << ": "
                      << nodes << " nodes, " << ms << " ms\n";

            if (count.depth == 2 && !check_divide(pos, 2, count.nodes)) {
                std::cout << "FAIL line " << line_number
                          << ": divide 2 output is not in the go perft format or its sum is wrong\n"
                          << "  fen " << c.fen << '\n';
                ++failures;
            }
        }
    }

    if (checked == 0) {
        std::cout << "FAIL: no counts under the node limit in " << argv[1] << '\n';
        return 1;
    }
    if (failures > 0) {
        std::cout << "test_perft: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "test_perft: OK (" << checked << " counts" << (deep ? ", deep" : "") << ")\n";
    return 0;
}