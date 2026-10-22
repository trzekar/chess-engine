#!/usr/bin/env python3
"""Tests for match.py that need neither fastchess nor a build: reading fastchess's result
block, the fastchess command line, the CSV file and the settings file.

The result blocks below come from real fastchess 1.8.2 runs, except where a comment says
otherwise: an SPRT that accepted H1, one that accepted H0, one that ran out of games, and
fixed-length matches with an even and a one-sided result.

Usage:
  python3 scripts/test_match.py
"""

import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match  # noqa: E402

SPRT_H1 = """\
Finished game 208 (weak vs strong): 0-1 {Black mates}
--------------------------------------------------
Results of strong vs weak (2+0.02, NULL, NULL, UHO_Lichess_4852_v1.epd):
Elo: 437.64 +/- 64.82, nElo: 809.30 +/- 47.22
LOS: 100.00 %, DrawRatio: 0.00 %, PairsRatio: 103.00
Games: 208, Wins: 178, Losses: 1, Draws: 29, Points: 192.5 (92.55 %)
Ptnml(0-2): [0, 1, 0, 28, 75], WL/DD Ratio: -nan
LLR: 2.95 (100.2%) (-2.94, 2.94) [0.00, 10.00]
--------------------------------------------------
SPRT ([0.00, 10.00]) completed - H1 was accepted
Finished match
Total Time: 00:02:50 (hours:minutes:seconds)
"""

SPRT_H0 = """\
--------------------------------------------------
Results of weak vs strong (1+0.01, NULL, NULL, UHO_Lichess_4852_v1.epd):
Elo: -374.18 +/- 54.42, nElo: -665.57 +/- 47.91
LOS: 0.00 %, DrawRatio: 4.95 %, PairsRatio: 0.00
Games: 202, Wins: 3, Losses: 163, Draws: 36, Points: 21.0 (10.40 %)
Ptnml(0-2): [64, 32, 5, 0, 0], WL/DD Ratio: 1.50
LLR: -2.95 (-100.1%) (-2.94, 2.94) [0.00, 10.00]
--------------------------------------------------
SPRT ([0.00, 10.00]) completed - H0 was accepted
Finished match
"""

# The SPRT reached the game limit without crossing a bound (the second block, from a real run).
# The first block is made up: an earlier report is always in the output, and only the last one
# counts.
SPRT_NO_VERDICT = """\
--------------------------------------------------
Results of a vs b (1+0.01, NULL, NULL, UHO_Lichess_4852_v1.epd):
Elo: 100.00 +/- 300.00, nElo: 200.00 +/- 400.00
LOS: 75.00 %, DrawRatio: 50.00 %, PairsRatio: 2.00
Games: 2, Wins: 1, Losses: 0, Draws: 1, Points: 1.5 (75.00 %)
Ptnml(0-2): [0, 0, 0, 1, 0], WL/DD Ratio: 0.00
LLR: 0.10 (3.4%) (-2.94, 2.94) [0.00, 10.00]
--------------------------------------------------
Finished game 6 (b vs a): 1/2-1/2 {Draw by 3-fold repetition}
--------------------------------------------------
Results of a vs b (1+0.01, NULL, NULL, UHO_Lichess_4852_v1.epd):
Elo: -0.00 +/- 173.65, nElo: -0.00 +/- 278.00
LOS: 50.00 %, DrawRatio: 33.33 %, PairsRatio: 1.00
Games: 6, Wins: 1, Losses: 1, Draws: 4, Points: 3.0 (50.00 %)
Ptnml(0-2): [0, 1, 1, 1, 0], WL/DD Ratio: 0.00
LLR: -0.01 (-0.3%) (-2.94, 2.94) [0.00, 10.00]
--------------------------------------------------
Finished match
"""

# The 100-game self-play of v0.1 at 8+0.08 on 2026-10-17.
FIXED_EVEN = """\
--------------------------------------------------
Results of a vs b (8+0.08, NULL, NULL, UHO_Lichess_4852_v1.epd):
Elo: -0.00 +/- 19.28, nElo: -0.00 +/- 68.10
LOS: 50.00 %, DrawRatio: 90.00 %, PairsRatio: 1.50
Games: 100, Wins: 33, Losses: 33, Draws: 34, Points: 50.0 (50.00 %)
Ptnml(0-2): [1, 1, 45, 3, 0], WL/DD Ratio: 2.00
--------------------------------------------------
Finished match
"""

FIXED_ALL_WINS = """\
--------------------------------------------------
Results of s vs w (1+0.01, NULL, NULL, UHO_Lichess_4852_v1.epd):
Elo: inf +/- -nan, nElo: inf +/- -nan
LOS: 100.00 %, DrawRatio: 0.00 %, PairsRatio: inf
Games: 4, Wins: 4, Losses: 0, Draws: 0, Points: 4.0 (100.00 %)
Ptnml(0-2): [0, 0, 0, 0, 2], WL/DD Ratio: -nan
--------------------------------------------------
Finished match
"""

CONFIG = {"fastchess": "/opt/fastchess", "book": "/books/UHO_Lichess_4852_v1.epd",
          "concurrency": 7}


class ParseReport(unittest.TestCase):
    def test_sprt_h1(self):
        r = match.parse_report(SPRT_H1)
        self.assertEqual((r["games"], r["wins"], r["losses"], r["draws"]), (208, 178, 1, 29))
        self.assertEqual((r["elo"], r["elo_error"], r["nelo"]), ("437.64", "64.82", "809.30"))
        self.assertEqual((r["los"], r["llr"], r["verdict"]), ("100.00", "2.95", "H1"))
        self.assertEqual(r["ptnml"], "0 1 0 28 75")

    def test_sprt_h0(self):
        r = match.parse_report(SPRT_H0)
        self.assertEqual((r["elo"], r["llr"], r["verdict"]), ("-374.18", "-2.95", "H0"))

    def test_sprt_without_verdict_takes_the_last_block(self):
        r = match.parse_report(SPRT_NO_VERDICT)
        self.assertEqual(r["games"], 6)
        self.assertEqual((r["elo"], r["llr"], r["verdict"]), ("0.00", "-0.01", "none"))

    def test_fixed_match_has_no_llr_and_no_verdict(self):
        r = match.parse_report(FIXED_EVEN)
        self.assertEqual((r["games"], r["elo"], r["elo_error"]), (100, "0.00", "19.28"))
        self.assertEqual((r["llr"], r["verdict"]), ("", ""))
        self.assertEqual(r["ptnml"], "1 1 45 3 0")

    def test_infinite_and_undefined_values(self):
        r = match.parse_report(FIXED_ALL_WINS)
        self.assertEqual((r["elo"], r["elo_error"], r["nelo"]), ("inf", "nan", "inf"))

    def test_no_result_block(self):
        self.assertIsNone(match.parse_report("Started game 1 of 2 (a vs b)\n"))
        self.assertIsNone(match.parse_report(""))


class Command(unittest.TestCase):
    def test_sprt(self):
        cmd = match.fastchess_command(CONFIG, Path("/b/new"), Path("/b/old"), ("v0.2", "v0.1"),
                                      "8+0.08", None, Path("/r/x.pgn"))
        self.assertEqual(cmd[0], "/opt/fastchess")
        self.assertIn("-sprt", cmd)
        sprt = cmd[cmd.index("-sprt") + 1:]
        self.assertEqual(sprt, ["elo0=0", "elo1=10", "alpha=0.05", "beta=0.05",
                                "model=logistic"])
        # 10 000 games at most: 5 000 rounds of two games.
        self.assertEqual(cmd[cmd.index("-rounds") + 1], "5000")
        self.assertIn("-repeat", cmd)
        self.assertEqual(cmd[cmd.index("-concurrency") + 1], "7")
        self.assertIn("format=epd", cmd)
        self.assertIn("order=random", cmd)
        self.assertIn("tc=8+0.08", cmd)
        self.assertIn("name=v0.2", cmd)
        self.assertIn("name=v0.1", cmd)

    def test_sprt_bounds(self):
        # A non-regression test: the change must not lose more than 10 Elo.
        cmd = match.fastchess_command(CONFIG, Path("/b/new"), Path("/b/old"), ("v0.3", "v0.2"),
                                      "8+0.08", None, Path("/r/x.pgn"), -10.0, 0.0)
        sprt = cmd[cmd.index("-sprt") + 1:]
        self.assertEqual(sprt[:2], ["elo0=-10", "elo1=0"])
        self.assertEqual(cmd[cmd.index("-rounds") + 1], "5000")

    def test_fixed(self):
        cmd = match.fastchess_command(CONFIG, Path("/b/e"), Path("/b/e"), ("v0.1", "v0.1-2"),
                                      "8+0.08", 200, Path("/r/x.pgn"))
        self.assertNotIn("-sprt", cmd)
        self.assertEqual(cmd[cmd.index("-rounds") + 1], "100")

    def test_pgn_book(self):
        config = dict(CONFIG, book="/books/8moves_v3.pgn")
        cmd = match.fastchess_command(config, Path("a"), Path("b"), ("a", "b"), "8+0.08", 2,
                                      Path("x.pgn"))
        self.assertIn("format=pgn", cmd)

    def test_names(self):
        self.assertEqual(match.engine_names("v0.2", "v0.1"), ("v0.2", "v0.1"))
        self.assertEqual(match.engine_names("v0.1", "v0.1"), ("v0.1", "v0.1-2"))


class Files(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.path = Path(self.dir.name)

    def tearDown(self):
        self.dir.cleanup()

    def row(self, **fields):
        row = {name: "" for name in match.CSV_FIELDS}
        row.update(fields)
        return row

    def test_csv_header_once_then_rows(self):
        path = self.path / "results" / "matches.csv"
        match.append_row(path, self.row(new="v0.2", old="v0.1", games=208))
        match.append_row(path, self.row(new="v0.3", old="v0.2", games=400))
        with open(path, encoding="utf-8", newline="") as f:
            rows = list(csv.reader(f))
        self.assertEqual(rows[0], match.CSV_FIELDS)
        self.assertEqual([r[1] for r in rows[1:]], ["v0.2", "v0.3"])
        self.assertEqual(len(rows), 3)

    def test_csv_with_other_columns_is_left_alone(self):
        path = self.path / "matches.csv"
        path.write_text("date,result\n2026-10-17,1-0\n", encoding="utf-8")
        with self.assertRaises(match.MatchError):
            match.append_row(path, self.row(new="v0.2"))
        self.assertEqual(path.read_text(encoding="utf-8"), "date,result\n2026-10-17,1-0\n")

    def test_failed_games_follow_check_games(self):
        pgn = self.path / "m.pgn"
        pgn.write_text(
            '[Event "x"]\n[Result "1-0"]\n[Termination "normal"]\n\n1. e4 1-0\n\n'
            '[Event "x"]\n[Result "0-1"]\n[Termination "time forfeit"]\n\n1. e4 0-1\n\n'
            '[Event "x"]\n[Result "*"]\n[Termination "unterminated"]\n\n1. e4 *\n',
            encoding="utf-8")
        self.assertEqual(match.failed_games(pgn), 2)

    def write_config(self, config):
        path = self.path / "match.local.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        return path

    def test_config(self):
        fastchess = self.path / "fastchess"
        book = self.path / "book.epd"
        fastchess.write_text("")
        book.write_text("")
        good = {"fastchess": str(fastchess), "book": str(book), "concurrency": 3}
        self.assertEqual(match.load_config(self.write_config(good))["concurrency"], 3)

        for bad in (dict(good, concurrency=0), dict(good, concurrency="3"),
                    dict(good, book=str(self.path / "missing.epd")),
                    {k: v for k, v in good.items() if k != "fastchess"}):
            with self.assertRaises(match.MatchError):
                match.load_config(self.write_config(bad))
        with self.assertRaises(match.MatchError):
            match.load_config(self.path / "absent.json")


if __name__ == "__main__":
    unittest.main()