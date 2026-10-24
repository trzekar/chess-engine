#!/usr/bin/env python3
"""Plot the cumulative Elo gain over the first version, from results/matches.csv.

Every match in matches.csv plays a new version against the one before it. The plot stacks the
measured gains into steps, one step per accepted change, with the 95 % interval as a band: the
matches are independent, so their intervals add in quadrature. Only a match whose SPRT accepted
H1 makes a step. The A/A check of a version against itself, a fixed match, and a change the SPRT
rejected or left undecided add nothing, since no change went in. When two accepted matches
start from the same old version, the later one counts and the script says so.

The gains are self-play Elo, each against the previous version. Self-play exaggerates the
differences between versions of one engine, so the total is no rating; the gauntlet against
Stockfish (scripts/gauntlet.py) is the outside scale.

Usage:
  python scripts/plot_elo.py [--csv results/matches.csv] [--out docs/img/elo_progress.png]

It writes two images: the one named by --out, for a light page, and the same name with -dark
before the extension, for GitHub's dark theme. It also prints the steps, so that the numbers in
the README can be checked against them.

Needs matplotlib (requirements.txt). Exit code: 0 when both images were written, 2 when the CSV
cannot be read or holds no accepted match, or matplotlib is missing.
"""

import argparse
import csv
import importlib.util
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MATCHES_CSV = ROOT / "results" / "matches.csv"
OUT_PNG = ROOT / "docs" / "img" / "elo_progress.png"

# The label under each step, keyed by the `new` column of matches.csv (the branch name). A name
# missing here is shown as it is.
LABELS = {
    "qsearch": "Quiescence\nsearch",
    "zobrist": "Zobrist,\ndraws",
    "tt": "Transposition\ntable",
    "ordering": "Killers,\nhistory",
    "nmp": "Null move\npruning",
    "pvs": "PVS",
    "lmr": "LMR",
}

# Colours from the reference palette of the dataviz guidelines: the first categorical slot for
# the one series, hairline grid and recessive axes, text in ink colours rather than the series
# colour. Dark mode has its own steps, chosen for the dark surface, not an inverted copy.
THEMES = {
    "light": {"surface": "#fcfcfb", "ink": "#0b0b0b", "ink2": "#52514e", "muted": "#898781",
              "grid": "#e1e0d9", "axis": "#c3c2b7", "series": "#2a78d6", "band": 0.16},
    "dark": {"surface": "#1a1a19", "ink": "#f0efec", "ink2": "#c3c2b7", "muted": "#898781",
             "grid": "#2c2c2a", "axis": "#383835", "series": "#3987e5", "band": 0.26},
}


SANS = ["Segoe UI", "Helvetica Neue", "Helvetica", "Arial", "DejaVu Sans"]


class PlotError(Exception):
    """Nothing to plot: a missing or unreadable CSV, or no accepted match in it."""


def read_steps(path):
    """The accepted matches of the CSV in file order, one per old version.

    Returns (baseline, steps): the old version of the first step, and a list of dicts with the
    name, the gain and its 95 % margin.
    """
    try:
        with open(path, encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
    except OSError as error:
        raise PlotError(f"cannot read {path}: {error}")

    by_old = {}
    for row in rows:
        if row.get("verdict") != "H1" or row["new"] == row["old"]:
            continue
        if row["old"] in by_old:
            print(f"note: two accepted matches from {row['old']}; "
                  f"{row['new']} ({row['date']}) replaces {by_old[row['old']]['new']}")
        try:
            by_old[row["old"]] = {"new": row["new"], "old": row["old"], "date": row["date"],
                                  "elo": float(row["elo"]), "error": float(row["elo_error"]),
                                  "games": int(row["games"]),
                                  "text": f"{row['elo']} ± {row['elo_error']}"}
        except (KeyError, ValueError) as error:
            raise PlotError(f"{path}: bad row for {row.get('new')}: {error}")

    # dicts keep insertion order; a replaced key keeps its first position, which is the order
    # of the version it starts from.
    steps = list(by_old.values())
    if not steps:
        raise PlotError(f"{path} holds no match whose SPRT accepted H1")
    return steps[0]["old"], steps


def cumulate(steps):
    """Running totals of the gains and their margins, starting with 0 for the baseline."""
    totals, margins = [0.0], [0.0]
    variance = 0.0
    for step in steps:
        variance += step["error"] ** 2
        totals.append(totals[-1] + step["elo"])
        margins.append(math.sqrt(variance))
    return totals, margins


def plot(baseline, steps, totals, margins, out, theme):
    import matplotlib
    matplotlib.use("Agg")  # no window: the image goes straight to a file
    import matplotlib.pyplot as plt
    from matplotlib import font_manager

    c = THEMES[theme]
    # The system sans of the machine, else matplotlib's own. Only installed families are listed:
    # matplotlib warns about every missing one, on every piece of text.
    installed = {font.name for font in font_manager.fontManager.ttflist}
    plt.rcParams["font.family"] = [f for f in SANS if f in installed] or ["DejaVu Sans"]

    fig, ax = plt.subplots(figsize=(10, 5.2), dpi=160)
    fig.patch.set_facecolor(c["surface"])
    ax.set_facecolor(c["surface"])

    # Each version is a flat step centred on its position: with where="mid" the line changes
    # level halfway between two positions, and the extra points at both ends give the first and
    # the last version a full-width step too.
    n = len(steps)
    xs = [-0.5, *range(n + 1), n + 0.5]
    ys = [totals[0], *totals, totals[-1]]
    lows = [y - m for y, m in zip(ys, [margins[0], *margins, margins[-1]])]
    highs = [y + m for y, m in zip(ys, [margins[0], *margins, margins[-1]])]

    ax.fill_between(xs, lows, highs, step="mid", color=c["series"], alpha=c["band"], linewidth=0)
    ax.step(xs, ys, where="mid", color=c["series"], linewidth=2, solid_joinstyle="miter")

    # The gain of each step above its band: selective direct labels, in ink, not series colour.
    for i, step in enumerate(steps, start=1):
        ax.annotate(f"+{step['elo']:.0f}", (i, totals[i] + margins[i]),
                    xytext=(0, 5), textcoords="offset points", ha="center", va="bottom",
                    fontsize=10, color=c["ink"])

    ax.set_xlim(-0.5, n + 0.5)
    ax.set_ylim(min(0.0, min(lows)), max(highs) * 1.12)
    ax.set_xticks(range(n + 1))
    ax.set_xticklabels([baseline] + [LABELS.get(s["new"], s["new"]) for s in steps],
                       fontsize=9.5, color=c["ink2"])
    ax.tick_params(axis="x", length=0, pad=8)
    ax.tick_params(axis="y", colors=c["muted"], labelsize=9.5, length=0)
    ax.set_ylabel(f"Elo over {baseline}", color=c["ink2"], fontsize=10)

    ax.grid(axis="y", color=c["grid"], linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right", "left"):
        ax.spines[side].set_visible(False)
    ax.spines["bottom"].set_color(c["axis"])

    fig.text(0.012, 0.965, f"Cumulative self-play gain: +{totals[-1]:.0f} ± {margins[-1]:.0f} Elo",
             fontsize=14, fontweight="bold", color=c["ink"], ha="left", va="top")
    games = sum(s["games"] for s in steps)
    fig.text(0.012, 0.905,
             f"Each step is one change, accepted by an SPRT against the version before it "
             f"({games} games at 8+0.08). Band: 95 % interval.",
             fontsize=9.5, color=c["ink2"], ha="left", va="top")

    fig.subplots_adjust(left=0.07, right=0.985, top=0.83, bottom=0.14)
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, facecolor=c["surface"])
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--csv", type=Path, default=MATCHES_CSV, help="the matches to plot")
    parser.add_argument("--out", type=Path, default=OUT_PNG, help="the image for a light page")
    args = parser.parse_args()

    # Checked before any work, for a plain message instead of a traceback.
    if importlib.util.find_spec("matplotlib") is None:
        print("matplotlib is missing: pip install -r requirements.txt", file=sys.stderr)
        return 2

    try:
        baseline, steps = read_steps(args.csv)
    except PlotError as error:
        print(error, file=sys.stderr)
        return 2
    totals, margins = cumulate(steps)

    # The gain as the CSV holds it, unrounded, and the running total as the plot shows it.
    print(f"{'step':<10} {'old':<15} {'games':>6}  {'elo (csv)':<16} {'cumulative':>12}")
    for i, step in enumerate(steps, start=1):
        print(f"{step['new']:<10} {step['old']:<15} {step['games']:>6}  {step['text']:<16} "
              f"{totals[i]:>+6.0f} ± {margins[i]:.0f}")

    dark = args.out.with_name(f"{args.out.stem}-dark{args.out.suffix}")
    plot(baseline, steps, totals, margins, args.out, "light")
    plot(baseline, steps, totals, margins, dark, "dark")
    for path in (args.out, dark):
        shown = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
        print(f"wrote {shown}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
