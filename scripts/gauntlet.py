#!/usr/bin/env python3
"""Estimate the engine's Elo against Stockfish limited to known strengths, and record it.

The script builds one git revision in Release, as match.py does, and plays it against Stockfish
with UCI_LimitStrength on, once for every level given with --levels (UCI_Elo, 1320 to 3190).
Every level is a fixed match of --games games with the opening book, each opening twice with
colours swapped, and appends one row to results/gauntlet.csv: the result, the Elo difference
fastchess measured with its error margin, and the estimate, which is the level plus that
difference. At the end it prints the estimate of the levels combined, weighted by the inverse
square of their error margins. Only levels scored between 25 % and 75 % take part: further out
the margin grows fast and the level says little.

Stockfish's UCI_Elo is calibrated at 120+1 and anchored to CCRL 40/4. At 8+0.08, the default
here as in match.py, the estimate holds for that time control only; --tc 120+1 measures where
the scale was calibrated. Both sides play with one thread and the same hash (--hash, 16 MB by
default), and the row records the Stockfish version, since the scale belongs to a version.

Usage:
  python scripts/gauntlet.py REVISION --levels L [L ...] [--games 200] [--tc 8+0.08]
                             [--hash 16] [--notes TEXT]

The settings come from match.local.json, as for match.py, with one more key, the path of
Stockfish:
  "stockfish": "C:/Users/me/dev/tools/stockfish/stockfish.exe"

Exit code: 0 when every level was played and every game ended on the board, 1 when a game ended
by time forfeit, illegal move or crash (the row is still written, with the count), 2 when a
level could not be played or the run was interrupted (rows of the levels finished before it
stay written).
"""

import argparse
import csv
import datetime
import math
import re
import subprocess
import sys
from pathlib import Path

import match
from match import MatchError

ROOT = match.ROOT
GAUNTLET_CSV = ROOT / "results" / "gauntlet.csv"

GAMES = 200
HASH_MB = 16
# The range of UCI_Elo in Stockfish 16 and later.
MIN_LEVEL, MAX_LEVEL = 1320, 3190
# Levels scored outside this range (in %) are recorded but left out of the combined estimate.
COMBINE_RANGE = (25.0, 75.0)

CSV_FIELDS = [
    "date", "engine", "commit", "opponent", "uci_elo", "tc", "book", "concurrency", "hash",
    "games", "wins", "losses", "draws", "score", "elo", "elo_error", "estimate", "ptnml",
    "failed_games", "notes",
]


def load_config(path):
    config = match.load_config(path)
    if not Path(config.get("stockfish", "")).is_file():
        raise MatchError(f'{path.name}: "stockfish" must name an existing file')
    return config


def stockfish_name(path):
    """The name Stockfish gives in the UCI handshake, such as "Stockfish 17.1"."""
    try:
        result = subprocess.run([str(path)], input="uci\nquit\n", capture_output=True,
                                text=True, errors="replace", timeout=10)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise MatchError(f"cannot run {path}: {error}")
    found = re.search(r"^id name (.+?)\s*$", result.stdout, re.M)
    if not found:
        raise MatchError(f"{path} did not give its name after uci")
    return found.group(1)


def fastchess_command(config, engine, name, level, tc, games, hash_mb, pgn):
    book = Path(config["book"])
    return [
        config["fastchess"],
        "-engine", f"cmd={engine}", f"name={name}", f"option.Hash={hash_mb}",
        "-engine", f"cmd={config['stockfish']}", f"name=sf{level}",
        "option.Threads=1", f"option.Hash={hash_mb}",
        "option.UCI_LimitStrength=true", f"option.UCI_Elo={level}",
        "-each", f"tc={tc}",
        "-openings", f"file={book}", f"format={book.suffix.lower()[1:]}", "order=random",
        # Each round plays one opening twice, with the colours swapped.
        "-rounds", str(games // 2), "-repeat",
        "-concurrency", str(config["concurrency"]),
        "-pgnout", f"file={pgn}",
    ]


def run_fastchess(command, pgn):
    """Runs fastchess, showing its output as it comes, and returns its result block."""
    lines = []
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding="utf-8", errors="replace") as process:
        try:
            for line in process.stdout:
                print(line, end="", flush=True)
                lines.append(line)
        except KeyboardInterrupt:
            process.terminate()
            raise
    if process.returncode != 0:
        raise MatchError(f"fastchess exited with code {process.returncode}; the games "
                         f"played so far are in {pgn.relative_to(ROOT)}")
    report = match.parse_report("".join(lines))
    if report is None:
        raise MatchError("fastchess printed no result")
    return report


def finite(text):
    """parse_report writes numbers as text, with "inf" and "nan" for one-sided results."""
    try:
        return math.isfinite(float(text))
    except ValueError:
        return False


def level_row(revision, commit, opponent, level, tc, config, hash_mb, report, failed, notes):
    points = report["wins"] + report["draws"] / 2
    score = 100 * points / report["games"] if report["games"] else 0.0
    estimate = level + float(report["elo"]) if finite(report["elo"]) else None
    return {
        "date": datetime.date.today().isoformat(),
        "engine": revision, "commit": commit[:7],
        "opponent": opponent, "uci_elo": level,
        "tc": tc, "book": Path(config["book"]).name,
        "concurrency": config["concurrency"], "hash": hash_mb,
        "games": report["games"], "wins": report["wins"], "losses": report["losses"],
        "draws": report["draws"], "score": f"{score:.2f}",
        "elo": report["elo"], "elo_error": report["elo_error"],
        "estimate": f"{estimate:.0f}" if estimate is not None else "",
        "ptnml": report["ptnml"], "failed_games": failed, "notes": notes,
    }


def combine(rows):
    """The inverse-variance weighted mean of the estimates of rows scored within COMBINE_RANGE.

    Returns (estimate, error, levels used), or None without a usable row. The error margins of
    fastchess are 95 % intervals; weighting by 1 / margin^2 gives the same weights as by
    1 / sigma^2, and the combined margin is again a 95 % interval.
    """
    low, high = COMBINE_RANGE
    used = [r for r in rows
            if low <= float(r["score"]) <= high and r["estimate"] != ""
            and finite(r["elo_error"]) and float(r["elo_error"]) > 0]
    if not used:
        return None
    weights = [1 / float(r["elo_error"]) ** 2 for r in used]
    estimate = sum(w * float(r["estimate"]) for w, r in zip(weights, used)) / sum(weights)
    return estimate, 1 / math.sqrt(sum(weights)), [r["uci_elo"] for r in used]


def append_row(path, row):
    """Appends `row` to the CSV, writing the header first into a new or empty file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    new_file = not path.is_file() or path.stat().st_size == 0
    if not new_file:
        with open(path, encoding="utf-8", newline="") as f:
            header = next(csv.reader(f), [])
        if header != CSV_FIELDS:
            raise MatchError(f"{path} has other columns than this script writes; not touching it")
    with open(path, "a", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS, lineterminator="\n")
        if new_file:
            writer.writeheader()
        writer.writerow(row)


def check_levels(levels):
    for level in levels:
        if not MIN_LEVEL <= level <= MAX_LEVEL:
            raise MatchError(f"level {level} is outside UCI_Elo's range, "
                             f"{MIN_LEVEL} to {MAX_LEVEL}")
    if len(set(levels)) != len(levels):
        raise MatchError("--levels names a level twice")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("revision", help="the version to measure: a tag, branch or commit")
    parser.add_argument("--levels", type=int, nargs="+", required=True,
                        help=f"UCI_Elo levels of Stockfish, {MIN_LEVEL} to {MAX_LEVEL}")
    parser.add_argument("--games", type=int, default=GAMES,
                        help=f"games per level (an even number), {GAMES} by default")
    parser.add_argument("--tc", default=match.TC, help=f"time control, {match.TC} by default")
    parser.add_argument("--hash", type=int, default=HASH_MB,
                        help=f"hash of both sides in MB, {HASH_MB} by default")
    parser.add_argument("--notes", default="", help="free text for the notes column")
    args = parser.parse_args()

    rows = []
    failed_total = 0
    try:
        check_levels(args.levels)
        if args.games < 2 or args.games % 2:
            raise MatchError("--games must be an even number of at least 2")
        if args.hash < 1:
            raise MatchError("--hash must be at least 1")
        config = load_config(match.CONFIG)
        commit = match.resolve(args.revision)
        if match.git("status", "--porcelain", "--untracked-files=no").strip():
            print("note: uncommitted changes are not part of the version")
        engine = match.build(args.revision, commit, config)
        opponent = stockfish_name(config["stockfish"])

        match.PGN_DIR.mkdir(parents=True, exist_ok=True)
        for level in args.levels:
            print(f"\n{args.revision} vs {opponent} at UCI_Elo {level}, {args.games} games")
            stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
            safe = re.sub(r"[^A-Za-z0-9._-]", "_", f"{args.revision}_vs_sf{level}")
            pgn = match.PGN_DIR / f"{stamp}_{safe}.pgn"
            command = fastchess_command(config, engine, args.revision, level, args.tc,
                                        args.games, args.hash, pgn)
            report = run_fastchess(command, pgn)
            failed = match.failed_games(pgn) if pgn.is_file() else 0
            failed_total += failed
            row = level_row(args.revision, commit, opponent, level, args.tc, config, args.hash,
                            report, failed, args.notes)
            append_row(GAUNTLET_CSV, row)
            rows.append(row)
            print(f"recorded: UCI_Elo {level}, score {row['score']} %, "
                  f"Elo {row['elo']} +/- {row['elo_error']}, estimate {row['estimate'] or '-'}")
    except MatchError as error:
        print(f"gauntlet.py: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("gauntlet.py: interrupted; the level in progress was not recorded",
              file=sys.stderr)
        return 2

    print(f"\n{args.revision} at {args.tc}, rows appended to {GAUNTLET_CSV.relative_to(ROOT)}:")
    for row in rows:
        print(f"  UCI_Elo {row['uci_elo']}: {row['score']} %, estimate {row['estimate'] or '-'}"
              f" +/- {row['elo_error']}")
    combined = combine(rows)
    if combined:
        estimate, error, used = combined
        print(f"combined from levels {', '.join(map(str, used))}: {estimate:.0f} +/- {error:.0f}")
    else:
        print(f"no level scored between {COMBINE_RANGE[0]:g} % and {COMBINE_RANGE[1]:g} %: "
              "add a level closer to the engine")
    if failed_total:
        print(f"{failed_total} games did not end on the board: run check_games.py on their PGN",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
