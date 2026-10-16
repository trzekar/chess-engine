#!/usr/bin/env python3
"""Find the first move where the engine's perft differs from Stockfish's.

Runs `divide` in the engine and `go perft` in Stockfish on the same position and compares
the counts move by move. While both list the same moves, it plays the differing move with
the smallest Stockfish count (the cheapest subtree to search) and goes one ply deeper. At
depth 1 every count is 1, so the descent always ends on a move that one program generates
and the other does not.

Usage:
  python3 scripts/perft_diff.py --engine build/release/engine --stockfish stockfish \
      --depth 4 [--fen "<FEN>"] [--moves e2e4 e7e5 ...]

Exit code: 0 when the counts match, 1 when a difference was found, 2 when a program could
not be run or printed something unexpected.
"""

import argparse
import re
import subprocess
import sys

MOVE_LINE = re.compile(r"^([a-h][1-8][a-h][1-8][nbrq]?): (\d+)$")
TOTAL_LINE = re.compile(r"^Nodes searched: (\d+)$")


class ToolError(Exception):
    """A program could not be run or its output could not be read."""


def run(command, stdin_text=None):
    try:
        result = subprocess.run(
            command, input=stdin_text, capture_output=True, text=True
        )
    except OSError as error:
        raise ToolError(f"cannot run {command[0]}: {error}") from error
    if result.returncode != 0:
        raise ToolError(
            f"{' '.join(command)} exited with {result.returncode}:\n"
            f"{result.stderr.strip()}"
        )
    return result.stdout


def parse_divide(output, who):
    """Reads "<move>: <count>" lines and the "Nodes searched" total into a dict and an int."""
    counts = {}
    total = None
    for line in output.splitlines():
        line = line.strip()
        if match := MOVE_LINE.match(line):
            counts[match.group(1)] = int(match.group(2))
        elif match := TOTAL_LINE.match(line):
            total = int(match.group(1))
    if total is None:
        raise ToolError(f"no 'Nodes searched' line in the output of {who}:\n{output}")
    if sum(counts.values()) != total:
        raise ToolError(
            f"{who}: the move counts add up to {sum(counts.values())}, "
            f"but the total says {total}"
        )
    return counts, total


def position_command(fen, moves):
    command = f"position fen {fen}" if fen else "position startpos"
    if moves:
        command += " moves " + " ".join(moves)
    return command


def engine_divide(engine, fen, moves, depth):
    command = [engine, "divide", str(depth)]
    if fen:
        command.append(fen)
    if moves:
        command += ["moves", *moves]
    return parse_divide(run(command), "the engine")


def stockfish_divide(stockfish, fen, moves, depth):
    script = f"{position_command(fen, moves)}\ngo perft {depth}\nquit\n"
    return parse_divide(run([stockfish], script), "Stockfish")


def stockfish_fen(stockfish, fen, moves):
    """The FEN of the position, from Stockfish's `d` command."""
    output = run([stockfish], f"{position_command(fen, moves)}\nd\nquit\n")
    for line in output.splitlines():
        if line.startswith("Fen: "):
            return line[len("Fen: ") :].strip()
    raise ToolError("no 'Fen:' line in the output of Stockfish's d command")


def find_difference(args):
    moves = list(args.moves)
    depth = args.depth
    while True:
        ours, our_total = engine_divide(args.engine, args.fen, moves, depth)
        theirs, their_total = stockfish_divide(args.stockfish, args.fen, moves, depth)
        path = " ".join(moves) or "(root)"
        print(
            f"depth {depth}  after {path}: engine {our_total}, Stockfish {their_total}"
        )

        missing = sorted(set(theirs) - set(ours))
        extra = sorted(set(ours) - set(theirs))
        if missing or extra:
            print()
            print("Found the difference.")
            print(f"  moves from the start: {' '.join(moves) or '(none)'}")
            print(f"  FEN: {stockfish_fen(args.stockfish, args.fen, moves)}")
            print(f"  missing in the engine: {' '.join(missing) or '-'}")
            print(f"  extra in the engine:   {' '.join(extra) or '-'}")
            return 1

        differing = [m for m in theirs if ours[m] != theirs[m]]
        if not differing:
            print("\nThe counts match.")
            return 0
        if depth == 1:
            # Same moves at depth 1 but a count other than 1: divide itself is wrong.
            raise ToolError(
                f"depth 1 counts differ for {' '.join(differing)}: "
                "check the output of divide"
            )

        move = min(differing, key=lambda m: theirs[m])
        print(
            f"  descending into {move}: engine {ours[move]}, Stockfish {theirs[move]}"
        )
        moves.append(move)
        depth -= 1


def main():
    parser = argparse.ArgumentParser(
        description="Find the first move where the engine's perft differs from Stockfish's."
    )
    parser.add_argument("--engine", required=True, help="path to the engine binary")
    parser.add_argument(
        "--stockfish", required=True, help="path to the Stockfish binary"
    )
    parser.add_argument(
        "--depth", required=True, type=int, help="perft depth, at least 1"
    )
    parser.add_argument(
        "--fen", help="start from this FEN instead of the initial position"
    )
    parser.add_argument(
        "--moves", nargs="*", default=[], help="UCI moves played from the FEN"
    )
    args = parser.parse_args()
    if args.depth < 1:
        parser.error("--depth must be at least 1")

    try:
        return find_difference(args)
    except ToolError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
