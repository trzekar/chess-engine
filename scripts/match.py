#!/usr/bin/env python3
"""Play a match between two versions of the engine and record the result.

Each version is a git revision: a tag, a branch or a commit. Uncommitted changes belong to no
version. The script builds each revision in Release from a clean copy of its sources into
build/match/<commit>/, plays the first against the second with fastchess and appends one row
to results/matches.csv. The PGN goes to results/pgn/ (ignored by git).

By default the match is an SPRT with elo0 = 0, elo1 = 10, alpha = beta = 0.05 in logistic
Elo: it stops once the result is statistically clear, or after 10 000 games without a
verdict. With --games N it plays exactly N games without SPRT, as the A/A check of a version
against itself does. The time control is 8+0.08; --tc changes it for an experiment, and the
row records it.

Usage:
  python scripts/match.py NEW OLD [--games N] [--tc 8+0.08] [--notes TEXT]

The settings of the machine live in match.local.json in the repository root, which is not
committed:
  {"fastchess": "C:/Users/me/dev/tools/fastchess/fastchess.exe",
   "book": "C:/Users/me/dev/books/UHO_Lichess_4852_v1.epd",
   "concurrency": 7,
   "cmake_generator": "MinGW Makefiles"}
"concurrency" is the number of physical cores minus one; "cmake_generator" is needed on
Windows with MinGW only.

Exit code: 0 when the match finished and every game ended on the board, 1 when a game ended
by time forfeit, illegal move or crash (the row is still written, with the count), 2 when the
match could not be played or was interrupted (no row is written).
"""

import argparse
import csv
import datetime
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

import check_games

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "match.local.json"
MATCHES_CSV = ROOT / "results" / "matches.csv"
PGN_DIR = ROOT / "results" / "pgn"
BUILD_DIR = ROOT / "build" / "match"
EXE = ".exe" if os.name == "nt" else ""

TC = "8+0.08"
ELO0, ELO1, ALPHA, BETA = 0, 10, 0.05, 0.05
MAX_GAMES = 10_000

CSV_FIELDS = [
    "date", "new", "new_commit", "old", "old_commit", "tc", "book", "concurrency", "test",
    "games", "wins", "losses", "draws", "elo", "elo_error", "nelo", "los", "llr", "ptnml",
    "verdict", "failed_games", "notes",
]


class MatchError(Exception):
    """A match that cannot be played: bad settings, a failed build, no result from fastchess."""


def load_config(path):
    try:
        with open(path, encoding="utf-8") as f:
            config = json.load(f)
    except FileNotFoundError:
        raise MatchError(f"missing {path.name}; see the top of scripts/match.py for its format")
    except json.JSONDecodeError as error:
        raise MatchError(f"{path.name} is not valid JSON: {error}")

    for key in ("fastchess", "book"):
        if not Path(config.get(key, "")).is_file():
            raise MatchError(f'{path.name}: "{key}" must name an existing file')
    if not isinstance(config.get("concurrency"), int) or config["concurrency"] < 1:
        raise MatchError(f'{path.name}: "concurrency" must be a whole number of at least 1')
    if Path(config["book"]).suffix.lower() not in (".epd", ".pgn"):
        raise MatchError(f'{path.name}: "book" must be an .epd or a .pgn file')
    return config


def git(*args):
    result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True)
    if result.returncode != 0:
        message = result.stderr.decode(errors="replace").strip()
        raise MatchError(f"git {' '.join(args)} failed: {message}")
    return result.stdout


def resolve(revision):
    """The full commit hash of a tag, branch or commit."""
    try:
        return git("rev-parse", "--verify", "--quiet", revision + "^{commit}").decode().strip()
    except MatchError:
        raise MatchError(f"unknown revision: {revision}")


def build(revision, commit, config):
    """Builds the engine of `commit` in Release and returns the path of the binary.

    The directory is named after the commit, and a commit never changes, so an existing binary
    is reused: a tag is built once, however many matches it plays.
    """
    directory = BUILD_DIR / commit[:12]
    binary = directory / ("engine" + EXE)
    if binary.is_file():
        print(f"{revision}: using {binary.relative_to(ROOT)}")
        return binary

    print(f"{revision}: building {commit[:12]} in Release")
    source = directory / "src"
    if directory.exists():
        shutil.rmtree(directory)
    source.mkdir(parents=True)
    # git archive gives the committed files only: nothing from the working tree gets in.
    with tarfile.open(fileobj=io.BytesIO(git("archive", "--format=tar", commit))) as archive:
        if hasattr(tarfile, "data_filter"):
            archive.extractall(source, filter="data")
        else:
            archive.extractall(source)

    configure = ["cmake", "-S", str(source), "-B", str(directory / "build"),
                 "-DCMAKE_BUILD_TYPE=Release"]
    if config.get("cmake_generator"):
        configure += ["-G", config["cmake_generator"]]
    for command in (configure,
                    ["cmake", "--build", str(directory / "build"), "--target", "engine",
                     "--parallel"]):
        result = subprocess.run(command, capture_output=True, text=True, errors="replace")
        if result.returncode != 0:
            raise MatchError(f"building {revision} failed:\n{result.stdout}{result.stderr}")

    built = directory / "build" / ("engine" + EXE)
    if not built.is_file():
        raise MatchError(f"building {revision} produced no {built.name}")
    shutil.copy2(built, binary)
    return binary


def engine_names(new, old):
    """fastchess needs two different names; a version against itself gets a suffix."""
    return (new, old) if new != old else (new, old + "-2")


def fastchess_command(config, new_engine, old_engine, names, tc, games, pgn):
    """games is None for an SPRT, or the exact number of games to play."""
    rounds = (games if games is not None else MAX_GAMES) // 2
    book = Path(config["book"])
    command = [
        config["fastchess"],
        "-engine", f"cmd={new_engine}", f"name={names[0]}",
        "-engine", f"cmd={old_engine}", f"name={names[1]}",
        "-each", f"tc={tc}",
        "-openings", f"file={book}", f"format={book.suffix.lower()[1:]}", "order=random",
        # Each round plays one opening twice, with the colours swapped.
        "-rounds", str(rounds), "-repeat",
        "-concurrency", str(config["concurrency"]),
        "-pgnout", f"file={pgn}",
    ]
    if games is None:
        command += ["-sprt", f"elo0={ELO0}", f"elo1={ELO1}", f"alpha={ALPHA}", f"beta={BETA}",
                    "model=logistic"]
    return command


def parse_report(output):
    """Reads the last result block fastchess printed. Returns a dict, or None without one.

    A block looks like this; the LLR line appears in an SPRT only:
      Results of new vs old (8+0.08, NULL, NULL, UHO_Lichess_4852_v1.epd):
      Elo: 34.86 +/- 43.74, nElo: 122.84 +/- 152.27
      LOS: 94.31 %, DrawRatio: 80.00 %, PairsRatio: inf
      Games: 20, Wins: 8, Losses: 6, Draws: 6, Points: 11.0 (55.00 %)
      Ptnml(0-2): [0, 0, 8, 2, 0], WL/DD Ratio: 3.00
      LLR: 2.95 (100.2%) (-2.94, 2.94) [0.00, 10.00]
    and a finished SPRT ends with "SPRT ([0.00, 10.00]) completed - H1 was accepted".
    """
    start = output.rfind("Results of ")
    if start < 0:
        return None
    block = output[start:]

    games = re.search(r"^Games: (\d+), Wins: (\d+), Losses: (\d+), Draws: (\d+)", block, re.M)
    elo = re.search(r"^Elo: (\S+) \+/- (\S+), nElo: (\S+) \+/-", block, re.M)
    if not games or not elo:
        return None
    los = re.search(r"^LOS: (\S+) %", block, re.M)
    ptnml = re.search(r"^Ptnml\(0-2\): \[([^\]]*)\]", block, re.M)
    llr = re.search(r"^LLR: (\S+) ", block, re.M)
    verdict = re.search(r"SPRT .* completed - (H0|H1) was accepted", block)

    def number(text):
        # fastchess prints inf and -nan for a match without a lost or a won game, and -0.00
        # for an even one.
        value = float(text)
        if value != value:
            return "nan"
        return f"{value:.2f}" if value != 0 else "0.00"

    return {
        "games": int(games.group(1)),
        "wins": int(games.group(2)),
        "losses": int(games.group(3)),
        "draws": int(games.group(4)),
        "elo": number(elo.group(1)),
        "elo_error": number(elo.group(2)),
        "nelo": number(elo.group(3)),
        "los": number(los.group(1)) if los else "",
        "ptnml": " ".join(ptnml.group(1).replace(",", " ").split()) if ptnml else "",
        "llr": number(llr.group(1)) if llr else "",
        "verdict": verdict.group(1) if verdict else ("none" if llr else ""),
    }


def failed_games(pgn):
    """Games that did not end on the board, by the rule of check_games.py."""
    return sum(1 for g in check_games.read_games(pgn)
               if g.get("Termination") not in check_games.GOOD_TERMINATIONS
               or g.get("Result", "*") == "*")


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


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("new", help="the version under test: a tag, branch or commit")
    parser.add_argument("old", help="the version it is compared with")
    parser.add_argument("--games", type=int,
                        help="play exactly this many games (an even number) without SPRT")
    parser.add_argument("--tc", default=TC, help=f"time control, {TC} unless experimenting")
    parser.add_argument("--notes", default="", help="free text for the notes column")
    args = parser.parse_args()

    try:
        if args.games is not None and (args.games < 2 or args.games % 2):
            raise MatchError("--games must be an even number of at least 2")
        config = load_config(CONFIG)
        commits = resolve(args.new), resolve(args.old)
        if git("status", "--porcelain", "--untracked-files=no").strip():
            print("note: uncommitted changes are not part of either version")

        new_engine = build(args.new, commits[0], config)
        old_engine = (new_engine if commits[1] == commits[0]
                      else build(args.old, commits[1], config))

        names = engine_names(args.new, args.old)
        stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S")
        safe = re.sub(r"[^A-Za-z0-9._-]", "_", f"{args.new}_vs_{args.old}")
        PGN_DIR.mkdir(parents=True, exist_ok=True)
        pgn = PGN_DIR / f"{stamp}_{safe}.pgn"
        command = fastchess_command(config, new_engine, old_engine, names, args.tc, args.games,
                                    pgn)

        # fastchess output is shown as it comes and kept for the result block at the end.
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
            # fastchess stops the match when an engine crashes; the PGN shows where.
            raise MatchError(f"fastchess exited with code {process.returncode}; the games "
                             f"played so far are in {pgn.relative_to(ROOT)}")
        report = parse_report("".join(lines))
        if report is None:
            raise MatchError("fastchess printed no result")
        failed = failed_games(pgn) if pgn.is_file() else 0

        row = {
            "date": datetime.date.today().isoformat(),
            "new": args.new, "new_commit": commits[0][:7],
            "old": args.old, "old_commit": commits[1][:7],
            "tc": args.tc, "book": Path(config["book"]).name,
            "concurrency": config["concurrency"],
            "test": "fixed" if args.games is not None else f"sprt {ELO0} {ELO1}",
            **report,
            "failed_games": failed, "notes": args.notes,
        }
        append_row(MATCHES_CSV, row)
    except MatchError as error:
        print(f"match.py: {error}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("match.py: interrupted; no row written", file=sys.stderr)
        return 2

    print(f"\nrecorded in {MATCHES_CSV.relative_to(ROOT)}: {args.new} vs {args.old}, "
          f"{row['games']} games, Elo {row['elo']} +/- {row['elo_error']}"
          + (f", SPRT {row['verdict']}" if args.games is None else ""))
    if failed:
        print(f"{failed} games did not end on the board: run check_games.py on {pgn}",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())