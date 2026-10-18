#!/usr/bin/env bash
# Deploys a tagged version of the engine to the bot server and restarts the bot.
#
# Usage: deploy/deploy.sh <tag>
#
# Run it on the server as the user the bot runs as, not as root: it calls sudo where it must.
# Expected layout, as in deploy/config.yml: this repository on its main branch and the
# lichess-bot checkout, with its venv, side by side; the token in /etc/lichess-bot.env.
#
# 1. Pulls main (config and scripts) and fetches the tags.
# 2. Builds the engine of the tag in Release from a clean copy of its sources, on this machine:
#    -march=native tunes it for this CPU, and a binary built elsewhere may not run here.
# 3. Checks the new binary: a short UCI session must end in a bestmove, and when the tag has
#    the bench command and its history a "Bench: N" line, the bench total must be N.
# 4. Installs it as build/bot/engine; the binary it replaces stays as build/bot/engine.previous.
# 5. Installs or updates the systemd unit and restarts the bot, which first lets the running
#    game end. Deploying the previous tag again is the rollback.
#
# Exit code: 0 deployed, 1 a check of the new binary failed and nothing changed, 2 bad usage or
# setup. A failing build or system command stops the script with that command's code.

set -euo pipefail
trap 'echo "deploy.sh: failed at line $LINENO" >&2' ERR

usage() {
    echo "usage: deploy/deploy.sh <tag>" >&2
    exit 2
}

fail_setup() {
    echo "deploy.sh: $1" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage
tag=$1

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
lichess_bot=$(dirname "$repo")/lichess-bot
user=$(id -un)

[[ $(id -u) -ne 0 ]] || fail_setup "run as the bot's user, not as root"
# deploy/config.yml finds the engine at ../chess-engine/build/bot/ from lichess-bot.
[[ $(basename "$repo") == chess-engine ]] || fail_setup "the repository must be named chess-engine"
for tool in git cmake g++ sudo systemctl; do
    command -v "$tool" >/dev/null || fail_setup "$tool is not installed"
done
[[ -x $lichess_bot/venv/bin/python ]] ||
    fail_setup "no lichess-bot with a venv in $lichess_bot (see CLAUDE.md, Lichess bot)"
sudo test -s /etc/lichess-bot.env ||
    fail_setup "/etc/lichess-bot.env is missing or empty: it holds LICHESS_BOT_TOKEN=..."

# 1. Config and scripts come from main; the engine comes from the tag.
[[ $(git -C "$repo" symbolic-ref --short HEAD) == main ]] ||
    fail_setup "$repo must be on main"
git -C "$repo" pull --ff-only --quiet
git -C "$repo" fetch --tags --quiet
commit=$(git -C "$repo" rev-parse --quiet --verify "refs/tags/$tag^{commit}") ||
    fail_setup "no tag $tag; deploy tags only, never a branch"

# 2. A clean copy of the tag: nothing from the working tree gets into the build.
build=$repo/build/deploy/$tag
echo "building $tag (${commit:0:7}) in Release"
rm -rf "$build"
mkdir -p "$build/src"
git -C "$repo" archive "$commit" | tar -x -C "$build/src"
cmake -S "$build/src" -B "$build/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$build/build" --target engine --parallel >/dev/null
engine=$build/build/engine

# 3. The end of the input stops the search, and depth 1 always completes, so a bestmove must
#    come back.
uci=$(printf 'uci\nisready\nposition startpos moves e2e4\ngo depth 4\n' | timeout 60 "$engine" 2>&1) ||
    true
grep -q '^bestmove ' <<<"$uci" || {
    echo "deploy.sh: $tag gives no bestmove in a UCI session; nothing changed:" >&2
    echo "$uci" >&2
    exit 1
}
echo "UCI check passed: $(grep '^bestmove ' <<<"$uci")"

# Every commit that changes the bench total ends its message with "Bench: N", so the last such
# line in the tag's history is the total this build must reproduce. Tags from before the bench
# (v0.1) skip the check.
# awk reads the whole log: stopping early would kill git log with SIGPIPE, which pipefail
# turns into a failure.
expected=$(git -C "$repo" log --format=%B "$commit" |
    awk '!found && /^Bench: *[0-9]+ *$/ { print $2; found = 1 }')
if bench=$("$engine" bench 2>/dev/null); then
    total=$(sed -n 's/^nodes \([0-9][0-9]*\)$/\1/p' <<<"$bench")
    if [[ -z $expected ]]; then
        echo "bench: $total nodes; no Bench line in the history of $tag to compare with"
    elif [[ $total != "$expected" ]]; then
        echo "deploy.sh: bench gives $total nodes, the history of $tag says $expected;" \
            "nothing changed" >&2
        exit 1
    else
        echo "bench check passed: $total nodes"
    fi
else
    echo "bench: $tag has no bench command; check skipped"
fi

# 4. A rename is atomic: lichess-bot never sees a half-copied binary.
mkdir -p "$repo/build/bot"
[[ ! -f $repo/build/bot/engine ]] || cp -p "$repo/build/bot/engine" "$repo/build/bot/engine.previous"
install -m 755 "$engine" "$repo/build/bot/engine.new"
mv -f "$repo/build/bot/engine.new" "$repo/build/bot/engine"
printf '%s %s\n' "$tag" "$commit" >"$repo/build/bot/DEPLOYED"

# 5. The unit names this user and these paths.
unit=$(mktemp)
trap 'rm -f "$unit"' EXIT
sed -e "s|@USER@|$user|g" -e "s|@REPO@|$repo|g" -e "s|@LICHESS_BOT@|$lichess_bot|g" \
    "$repo/deploy/lichess-bot.service" >"$unit"
if ! cmp -s "$unit" /etc/systemd/system/lichess-bot.service; then
    echo "installing /etc/systemd/system/lichess-bot.service"
    sudo install -m 644 "$unit" /etc/systemd/system/lichess-bot.service
    sudo systemctl daemon-reload
fi
sudo systemctl enable --quiet lichess-bot

if systemctl is-active --quiet lichess-bot; then
    echo "restarting the bot; it first lets a running game end (at most 25 minutes)"
else
    echo "starting the bot"
fi
sudo systemctl restart lichess-bot
echo "deployed $tag"
systemctl --no-pager --lines=5 status lichess-bot || true