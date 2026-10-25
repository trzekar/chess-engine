#include "attacks.h"
#include "bench.h"
#include "eval.h"
#include "position.h"
#include "search.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Checks `bench`: every position is valid and playable, the total is the plain sum of
// search(pos, depth) over the positions, and two runs print the same counts. Depth 3 keeps the
// test fast under the sanitizers; nothing checked here depends on the depth.

static void fail(const std::string &what) {
    std::cerr << "test_bench: " << what << '\n';
    std::abort();
}

// Every line of bench's output except the time and the speed, which differ between runs.
static std::vector<std::string> stable_lines(const std::string &output) {
    std::vector<std::string> lines;
    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("time", 0) != 0 && line.rfind("nps", 0) != 0)
            lines.push_back(line);
    return lines;
}

// Each position parses, writes back to the same FEN and has a legal move, so every search in
// the bench has work to do.
static void test_positions() {
    for (const std::string_view fen : BENCH_POSITIONS) {
        Position pos;
        if (!parse_fen(fen, pos))
            fail("invalid FEN: " + std::string(fen));
        if (to_fen(pos) != fen)
            fail("FEN does not round-trip: " + std::string(fen) + " -> " + to_fen(pos));
        if (search(pos, 1).best_move == Move())
            fail("no legal move: " + std::string(fen));
    }
}

// The total is the sum of independent searches, and the output ends with that total.
static void test_total() {
    const int depth = 3;
    uint64_t expected = 0;
    for (const std::string_view fen : BENCH_POSITIONS) {
        Position pos;
        parse_fen(fen, pos);
        expected += search(pos, depth).nodes;
    }

    std::ostringstream out;
    const uint64_t total = bench(depth, out);
    if (total != expected)
        fail("total " + std::to_string(total) + ", the searches add up to " +
             std::to_string(expected));
    if (out.str().find("\nnodes " + std::to_string(total) + "\n") == std::string::npos)
        fail("the output does not end with the total:\n" + out.str());
    if (stable_lines(out.str()).size() != BENCH_POSITIONS.size() + 2)
        fail("expected one line per position, the depth and the total:\n" + out.str());
}

// Two runs give the same counts, line by line, and a deeper bench visits more nodes.
static void test_deterministic() {
    std::ostringstream first, second;
    bench(3, first);
    bench(3, second);
    if (stable_lines(first.str()) != stable_lines(second.str()))
        fail("two runs differ:\n" + first.str() + "---\n" + second.str());

    std::ostringstream shallow;
    if (bench(2, shallow) >= bench(3, first))
        fail("depth 2 visits at least as many nodes as depth 3");
}

int main() {
    init_attacks();
    init_eval();
    test_positions();
    test_total();
    test_deterministic();
    std::cout << "test_bench: OK\n";
    return 0;
}