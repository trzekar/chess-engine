# CLAUDE.md

Guidance for Claude Code when working in this repository.

## Project

A UCI chess engine written from scratch in C++20. Priorities, in order:

1. Correctness, verified by perft.
2. Measured strength: every search feature is validated by self-play.
3. Code the author can explain line by line.

## Build

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release

cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
```

Run the tests after building:

```sh
ctest --test-dir build/debug --output-on-failure
```

Run a single test by name, for example after a change to move generation:

```sh
ctest --test-dir build/debug -R test_movegen --output-on-failure
```

Run the deep perft suite (Release only, about half a minute) before committing a change to
move generation or `make_move`. CI runs it in the Release jobs:

```sh
./build/release/test_perft tests/perft_positions.epd --deep
```

Count perft nodes with the engine binary. Without a FEN it starts from the initial
position; pass the FEN as one quoted argument. Take speed numbers from Release only.

```sh
./build/release/engine perft 5
./build/release/engine perft 4 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
```

Both `perft` and `divide` accept a move list after the word `moves`, as in the UCI `position`
command. The moves are played from the given FEN (or the start position) before counting; an
unknown or illegal move is an error.

```sh
./build/release/engine perft 3 moves e2e4 e7e5
./build/release/engine divide 3 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1" moves e1g1
```

`divide` prints the node count under every legal move and the total, in the same format as
Stockfish's `go perft`, so the two outputs can be compared line by line.

Find where the engine's perft differs from Stockfish's. The script runs `divide` and
`go perft`, descends into the cheapest differing branch and stops at the missing or extra
move. Exit code 0: the counts match, 1: a difference was found, 2: a program failed.

```sh
python3 scripts/perft_diff.py --engine build/release/engine --stockfish <path to stockfish> --depth 4 --fen "<FEN>" [--moves e2e4 ...]
```

Search a position to a fixed depth with the engine binary. `search` is alpha-beta, `negamax`
the reference without pruning. Both take the same FEN and move arguments as `perft` and print
the best move (`0000` without legal moves), the score from the side to move's point of view,
the node count and the speed. Record node counts in `results/nodes.csv`, from Release only.

```sh
./build/release/engine search 5
./build/release/engine negamax 5
```

Measure the search with the bench: the twenty fixed positions of `src/bench.h` searched to
depth 5 (`BENCH_DEPTH`). It prints the node count of every position, the total, the time and
the speed. The total is the search's fingerprint: it is the same on every machine and in every
run. Record it in `results/bench.csv`, from Release only.

```sh
./build/release/engine bench
```

Without arguments the engine speaks UCI on stdin and stdout, as GUIs, fastchess and lichess-bot
expect. Check the protocol with fastchess's compliance test after any change to `src/uci.cpp`:

```sh
fastchess --compliance ./build/release/engine
```

Play self-play games to catch crashes, illegal moves and time losses. Each opening from the
book is played twice with colours swapped; without a book the engine repeats one game. Take a
book from github.com/official-stockfish/books and unzip it (a `.pgn` book takes
`format=pgn`). Set `-concurrency` to the number of physical cores minus one. Then count how the
games ended; every one must end on the board:

```sh
fastchess -engine cmd=build/release/engine name=a -engine cmd=build/release/engine name=b \
  -each tc=8+0.08 -openings file=<book.epd> format=epd order=random \
  -rounds 50 -repeat -concurrency 3 -pgnout file=selfplay.pgn
python3 scripts/check_games.py selfplay.pgn --games 100
```

`check_games.py` reads the Termination tag fastchess writes: `normal` and `adjudication` pass,
`time forfeit`, `illegal move`, `abandoned` (crash, disconnect, stall) and `unterminated` fail.
Exit code 0: all games ended on the board, 1: a game failed or the count differs, 2: the file
cannot be read.

Measure strength with `match.py`. It builds two git revisions (tags, branches or commits;
uncommitted changes belong to neither) in Release from clean copies of their sources, plays
them with fastchess at 8+0.08 with the opening book, each opening twice with colours swapped,
and appends one row to `results/matches.csv`. By default the match is an SPRT with
elo0=0, elo1=10, alpha=beta=0.05 in logistic Elo, capped at 10 000 games; `--games N` plays
exactly N games without SPRT. The PGN goes to `results/pgn/`.

```sh
python3 scripts/match.py v0.2 v0.1
python3 scripts/match.py v0.1 v0.1 --games 200
```

The machine's settings live in `match.local.json` in the repository root, which is never
committed: the paths of fastchess and the book, the number of games played at once (physical
cores minus one) and, on Windows with MinGW, the CMake generator. The format is at the top of
`scripts/match.py`. Exit code 0: the match finished and every game ended on the board, 1: a
game ended by time forfeit, illegal move or crash (the row records how many), 2: the match
could not be played or was interrupted, and no row was written.

- On Windows with MinGW, add `-G "MinGW Makefiles"` to the configure step.
- Release (`-O3 -march=native`) is the only build used for perft speed, bench
  and matches. Never report timings from a debug build.
- Debug enables ASan and UBSan everywhere except MinGW, which does not ship
  them. CI on Linux covers that gap.
- All test targets are built with `-UNDEBUG`, so `assert` stays active in
  every build type.
- Warnings are errors. Fix the cause. Do not silence a warning with a cast,
  a pragma or by dropping a flag unless asked.

When you add a target, a test or a script, document its command here in the
same change.

## Authorship boundaries

The author writes the core engine logic by hand:

- search: alpha-beta, quiescence, pruning, reductions, move ordering
- the transposition table
- making a move on the board
- Zobrist hashing
- time management: `src/timeman.h`

In that code, wherever it lives, do not write, rewrite or fix anything unless
the request explicitly asks for it. Review only: cite the line, explain what
is wrong and why, then stop. No patches and no corrected snippets.

Everything else is yours to write: build system, CI, FEN and UCI parsing,
tests, scripts and tooling.

README prose belongs to the author. Limit yourself to language corrections
and to checking that every number matches the recorded results.

## Working style

- Before editing, state the plan in at most three sentences.
- Keep each change to one function or one file, small enough to review in
  ten minutes. Split larger work and stop between the parts.
- Match the naming and style of the surrounding code. No drive-by refactors
  and no reformatting of lines you did not need to touch.
- Prefer the plain solution over the clever one. When a trick is needed for
  speed, say why in a comment.
- If a request is ambiguous, ask before writing code.

## Conventions

- C++20 with compiler extensions off. Engine code uses the standard library
  only, with no third-party dependencies.
- Engine sources live in `src/`, tests in `tests/`, tooling in `scripts/`.
- Scripts are Python 3 and use the standard library. Propose a dependency
  before adding one.
- Generated output (build directories, PGNs, logs) is never committed.
- Data copied from elsewhere keeps a comment naming its source, like the PeSTO tables in
  `src/eval.cpp`, and the README credits it.
- `init_attacks()` and `init_eval()` fill lookup tables and must run once before use: in
  `main` and at the top of every test that needs them. A missing `init_eval` crashes
  nothing, every score is simply 0.
- The search runs on its own thread and never prints: `think` reports each completed depth
  through its callback. Every line the engine writes goes through `Output::send` in
  `src/uci.cpp`, which holds a mutex and flushes, so lines from the two threads never mix and
  a GUI never waits for a buffered `bestmove`.
- `results/` holds the measurements: `nodes.csv`, `bench.csv` and `matches.csv`. Rows are only
  ever appended, never edited, failed attempts included. `match.py` writes `matches.csv`; the
  other two are filled by hand from Release runs.
- The bench total is the search's fingerprint. A change meant to leave the search as it was (a
  refactor, a speed-up, comments) must leave the total unchanged and needs no match. A change
  that alters the total changes how the engine plays and needs a match. Changing
  `BENCH_DEPTH` or the positions starts a new series: rerun the bench of the earlier tags with
  the new setting.

## Verification

- `test_perft` is the oracle for move generation and `make_move`. It compares perft with
  the published counts (chessprogramming.org/Perft_Results) in the seven positions of
  `tests/perft_positions.epd`: every depth up to 5 million nodes in ctest, up to 200 million
  with `--deep`. It also checks that `divide` prints the `go perft` format. Run the deep
  suite after any change that touches move generation or board state. On a mismatch, find
  the move with `engine divide` and Stockfish's `go perft`.
- Never edit an expected node count to make a test pass.
- A claim that a change makes the engine stronger needs an SPRT from `match.py` that
  accepted H1, recorded in `results/matches.csv`. Node counts alone are not evidence of
  strength.
- Do not report work as done unless you built it and ran the relevant tests.
  If you could not run them, say so.
- `test_attacks` is the oracle for attack generation: it compares the
  tables, the sliding attacks and `is_attacked` with a square-by-square
  reference. When `attacks.cpp` changes, the
  test must pass unchanged.
- `test_movegen` checks `generate_moves` in 15 positions: move counts per category
  against an independent generator, plus properties (no duplicates, no move onto an
  own piece, capture flags). It also checks that `parse_uci_move` turns the UCI
  name of every generated move back into the same move. It fails with the whole move list at depth 1,
  so read it first when perft breaks. Never edit an expected count to make it pass.
- `test_make_move` checks `make_move` in two ways. 20 hand-picked moves, one per rule
  (castling, en passant, promotions, castling rights, counters, four illegal moves),
  compare the FEN after the move with an independent implementation (python-chess).
  A walk three plies deep through the six perft positions checks the rules that hold
  after every move: the board representations agree, the side to move changes,
  castling rights only shrink, and the en passant square and both counters follow
  the move. Never edit an expected FEN to make it pass.
- Expected FENs record the en passant square after every double push, as the FEN
  standard does. Stockfish's `d` command shows it only when an en passant capture is
  possible, so a FEN copied from Stockfish can differ in that field.
- `test_eval` checks `eval` in three ways. Five known scores, computed by hand from the
  PeSTO tables, pin the pure endgame (phase 0), the pure middlegame (phase 24), the blend
  between them, the clamp after a promotion and the sign for the side to move. Two
  orientation checks (a pawn on e7 beats one on e2, a castled king beats one on e1) hold for
  any sensible tables and catch a table read upside down or a missing `init_eval`. A walk two
  plies deep through the six perft positions (about 6600 positions) checks that every
  position scores the same as its color-swapped mirror and that passing the move negates the
  score. When the tables change, recompute the known scores by hand from the tables; never
  copy them from the engine's output.
- `test_search` checks `search` and `negamax_root` in four ways. The ten positions of
  `tests/mate_positions.epd` (five mates in 2, five in 3, four with Black to move) are solved
  by `search` at depth 2N with score `MATE - (2N - 1)` and the only winning first move; the
  mates in 2 also by `negamax_root`. A checkmated root scores `-MATE` and a stalemated one 0,
  both with `Move()` and one node. `negamax_root` visits exactly the sum of perft(0..3) in the
  perft positions. There `search` returns the same score and move as `negamax_root` at depths
  1 to 3, with the same node count at depth 1 and fewer from depth 2. Every mate was verified
  with Stockfish and by a full-width check that no other first move mates as fast: never edit
  a move or a mate distance to make the test pass. The comparison with `negamax_root` holds
  while alpha-beta cutoffs are the only pruning; quiescence search, null move and reductions
  end it, and the test must be changed in the same commit. It also checks `think`:
  with only a depth limit it returns the score and move of `search`
  at that depth, its node count is the sum over its iterations, and the callback sees every
  depth once in order; a stop flag set before the call still gives the depth-1 move; time
  budgets of 0, 50 and 200 ms are kept and end with the flag set; a checkmated root gives
  `Move()` after one iteration.
- `test_uci` drives `uci_loop` as a GUI does, with commands sent while the engine thinks and
  every output line timestamped: the `uci` handshake, `info` lines for every depth, `go
  movetime` and `go wtime` kept within their budget, `isready` answered and `bestmove`
  withheld during `go infinite` until `stop`, `quit` during a search, `score mate 2`,
  `mate 1` and `mate -1`, and a bad FEN or an illegal move leaving the position as it was.
  Timing checks allow 400 ms of slack for the sanitizers; they catch a search that ignores
  the clock, not one a few milliseconds late.
- `test_bench` checks the bench: every position parses, writes back to the same FEN and has a
  legal move; the total equals the sum of `search` over the positions; two runs print the same
  counts. It runs at depth 3 to stay fast under the sanitizers.
- `test_match` checks `match.py` without fastchess and without a build: reading the result
  block of real fastchess output (SPRT with H1 and with H0 accepted, SPRT without a verdict,
  fixed matches, inf and nan), the fastchess command line, the CSV file and the settings file.
  When fastchess changes its output, add a block copied from a real run to the test.
- Before a tag, 100 self-play games at 8+0.08 must pass `check_games.py` (see Build).

## Lichess bot

The bot runs lichess-bot, a separate project cloned next to this repository (never into it),
with `deploy/config.yml`:

```sh
cd ../lichess-bot
python lichess-bot.py --config ../chess-engine/deploy/config.yml
```

It plays the binary in `build/bot/`, a copy of a tagged Release build, never `build/release`
itself. Every move comes from the engine: no book, no online moves, no tablebases. It
accepts blitz only and challenges other bots after 15 idle minutes. The token comes from the
`LICHESS_BOT_TOKEN` environment variable only (see Secrets).

On the server (Ubuntu with systemd; `chess-engine` and `lichess-bot`, with its venv, side by
side in the home directory) the bot runs as the systemd service `lichess-bot`. Deploy a tag
there, as the bot's user:

```sh
~/chess-engine/deploy/deploy.sh v0.2
journalctl -u lichess-bot -f
```

`deploy.sh` pulls `main`, builds the tag in Release on the server, checks the binary (a UCI
session must end in a bestmove; when the tag has the bench, its total must match the last
`Bench:` line in the tag's history), installs it as `build/bot/engine`, installs the unit from
`deploy/lichess-bot.service` and restarts the bot, which first lets the running game end.
Deploying the previous tag again rolls back. Exit code 0: deployed, 1: a check failed and
nothing changed, 2: bad usage or setup.

## Git

- Commit only when asked. One logical change per commit.
- Tags mark measured versions and must stay reproducible. Never move or
  delete a tag, and never force-push.
- A commit that changes the bench total ends its message with a line `Bench: <total>`, the
  total of `engine bench` after the change, as in Stockfish.

## Secrets

The Lichess API token lives only on the machine that runs the bot: in the
`LICHESS_BOT_TOKEN` environment variable, which the service loads from
`/etc/lichess-bot.env` (owned by root, mode 600), typed in by hand on the server.
Never ask for it, print it, or put it in any file yourself, including configs,
scripts and logs.
