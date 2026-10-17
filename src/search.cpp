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

    // Every real score is above -INF, so -INF here means no legal move. With pseudo-legal
    // moves that is known only after trying them all. Adding `ply` makes a nearer mate
    // score higher, so the engine prefers the shortest one.
    if (max == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            return -MATE + ply;
        } else {
            return 0;
        }
    }

    return max;
}

int alphabeta(const Position &pos, int alpha, int beta, int depth, int ply) {
    nodes++;
    if (depth <= 0) {
        return eval(pos);
    }

    int best = -INF;
    MoveList list;
    generate_moves(pos, list);

    for (Move m : list) {
        Position next = pos;
        if (!next.make_move(m)) {
            continue;
        }

        // The child scores from the other side: the bounds change sign and swap places.
        int score = -alphabeta(next, -beta, -alpha, depth - 1, ply + 1);

        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
            }
        }

        // The opponent already has a better choice higher in the tree and will avoid this
        // position, so one refutation is enough.
        if (score >= beta) {
            return score;
        }
    }

    if (best == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            return -MATE + ply;
        } else {
            return 0;
        }
    }

    return best;
}
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

    // No legal move at the root: checkmate or stalemate, with ply 0.
    if (result.score == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            result.score = -MATE;
        } else {
            result.score = 0;
        }
    }

    return result;
}

SearchResult search(const Position &pos, int depth) {
    assert(depth >= 1);
    nodes = 1;
    SearchResult result = {Move(), -INF, 0};

    MoveList list;
    generate_moves(pos, list);

    int alpha = -INF;
    int beta = INF;

    for (Move m : list) {
        Position next = pos;
        if (!next.make_move(m)) {
            continue;
        }

        int score = -alphabeta(next, -beta, -alpha, depth - 1, 1);
        if (score > result.score) {
            result.score = score;
            result.best_move = m;
            alpha = score;
        }
    }

    result.nodes = nodes;

    // No legal move at the root: checkmate or stalemate, with ply 0.
    if (result.score == -INF) {
        if (pos.in_check(pos.side_to_move)) {
            result.score = -MATE;
        } else {
            result.score = 0;
        }
    }

    return result;
}