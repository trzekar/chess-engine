#include "search.h"
#include "eval.h"
#include "movegen.h"
#include "position.h"

namespace {
uint64_t nodes = 0;

int negamax(const Position &pos, int depth, int ply) {
    nodes++;

    if (depth <= 0) {
        return eval(pos);
    }

    int max = -INF;

    MoveList list;
    generate_moves(pos, list);

    for (Move m : list) {
        Position next = pos;
        if (!next.make_move(m)) {
            continue;
        }

        int score = -negamax(next, depth - 1, ply + 1);
        if (score > max) {
            max = score;
        }
    }

    if (max == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            return -MATE + ply;
        } else {
            return 0;
        }
    }

    return max;
}

int alphabeta(const Position &pos, int alpha, int beta, int depth, int ply) {}
} // namespace

SearchResult negamax_root(const Position &pos, int depth) {
    assert(depth >= 1);
    nodes = 1;
    SearchResult result = {Move(), -INF, 0};

    MoveList list;
    generate_moves(pos, list);

    for (Move m : list) {
        Position next = pos;
        if (!next.make_move(m)) {
            continue;
        }

        int score = -negamax(next, depth - 1, 1);
        if (score > result.score) {
            result.score = score;
            result.best_move = m;
        }
    }

    result.nodes = nodes;

    if (result.score == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            result.score = -MATE;
        } else {
            result.score = 0;
        }
    }

    return result;
}