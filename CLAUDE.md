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

- On Windows with MinGW, add `-G "MinGW Makefiles"` to the configure step.
- Release (`-O3 -march=native`) is the only build used for perft speed, bench
  and matches. Never report timings from a debug build.
- Debug enables ASan and UBSan everywhere except MinGW, which does not ship
  them. CI on Linux covers that gap.
- Test targets (`test_types`) are built with `-UNDEBUG`, so `assert` stays
  active in every build type.
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

## Verification

- Perft is the oracle for move generation. Run the perft tests after any
  change that touches move generation or board state.
- Never edit an expected node count to make a test pass.
- A claim that a change makes the engine stronger needs a self-play match
  result. Node counts alone are not evidence of strength.
- Do not report work as done unless you built it and ran the relevant tests.
  If you could not run them, say so.

## Git

- Commit only when asked. One logical change per commit.
- Tags mark measured versions and must stay reproducible. Never move or
  delete a tag, and never force-push.

## Secrets

The Lichess API token lives only in an environment variable on the machine
that runs the bot. Never ask for it, print it, or write it to any file,
including configs and logs.
