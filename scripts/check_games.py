#!/usr/bin/env python3
"""Check that every game in a fastchess PGN ended on the board.

fastchess writes why each game ended in its Termination tag: "normal" (mate, stalemate,
repetition, the fifty-move rule, insufficient material) or "adjudication" are results of play.
"time forfeit", "illegal move", "abandoned" (a crash, a disconnect or a stalled engine) and
"unterminated" are engine failures. The script prints how many games ended each way, lists the
failed games and fails if there is any.

Usage:
  python3 scripts/check_games.py selfplay.pgn [--games 100]

Exit code: 0 when every game ended on the board, 1 when a game failed or the number of games
differs from --games, 2 when the file cannot be read or holds no games.
"""

import argparse
import collections
import re
import sys

TAG = re.compile(r'^\[(\w+) "(.*)"\]\s*$')
GOOD_TERMINATIONS = {"normal", "adjudication"}


def read_games(path):
    """Returns the tag pairs of every game, one dict per game."""
    games = []
    with open(path, encoding="utf-8") as pgn:
        for line in pgn:
            match = TAG.match(line)
            if not match:
                continue
            name, value = match.groups()
            # Every game starts with its Event tag.
            if name == "Event":
                games.append({})
            if games:
                games[-1][name] = value
    return games


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("pgn", help="PGN written by fastchess -pgnout")
    parser.add_argument("--games", type=int, help="expected number of games")
    args = parser.parse_args()

    try:
        games = read_games(args.pgn)
    except OSError as error:
        print(f"cannot read {args.pgn}: {error}", file=sys.stderr)
        return 2
    if not games:
        print(f"no games in {args.pgn}", file=sys.stderr)
        return 2

    terminations = collections.Counter(g.get("Termination", "(none)") for g in games)
    results = collections.Counter(g.get("Result", "(none)") for g in games)
    failed = [
        g
        for g in games
        if g.get("Termination") not in GOOD_TERMINATIONS or g.get("Result", "*") == "*"
    ]

    print(f"{len(games)} games")
    for name, count in terminations.most_common():
        print(f"  {count:5d}  {name}")
    print("results: " + ", ".join(f"{r} {n}" for r, n in sorted(results.items())))

    ok = True
    if failed:
        ok = False
        print(f"\n{len(failed)} games did not end on the board:")
        for g in failed:
            print(
                f"  round {g.get('Round', '?')}: {g.get('White', '?')} vs {g.get('Black', '?')}, "
                f"{g.get('Result', '?')}, {g.get('Termination', '(no Termination tag)')}"
            )
    if args.games is not None and len(games) != args.games:
        ok = False
        print(f"\nexpected {args.games} games, found {len(games)}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())