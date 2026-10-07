#!/usr/bin/env bash
# Headless IBus end-to-end probe for I07 design evidence.
#
# Proves a REAL path: real dbus session bus + real ibus-daemon + an IBus engine
# process that calls the myswy Rust core through the C ABI + a real IBus client
# that receives the committed text. Not a mock.
#
# Prerequisites: ibus (daemon + dev headers), dbus, cc, and a built
# libmyswy_ime (`cargo build --release -p myswy-ffi`).
#
# Build the two probes first (from this directory, with the repo root as $REPO):
#   cc -std=c11 -Wall -Wextra -Werror -I"$REPO/include" $(pkg-config --cflags ibus-1.0) \
#      ibus-engine-probe.c -o build/engine-probe \
#      $(pkg-config --libs ibus-1.0) -L"$REPO/target/release" -lmyswy_ime \
#      -Wl,-rpath,"$REPO/target/release"
#   cc -std=c11 -Wall -Wextra -Werror $(pkg-config --cflags ibus-1.0) \
#      ibus-client-probe.c -o build/client $(pkg-config --libs ibus-1.0)
#
# Then: ./ibus-e2e.sh
#
# Expected tail: the client prints `[client] COMMIT: 你`.
#
# Why the isolated HOME matters: ibus-daemon writes its address to
# $HOME/.config/ibus/bus/. A stale entry makes a new daemon refuse to start with
# "current session already has an ibus-daemon", and --single is required.
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${1:-$HERE/build}"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/myswy-ibus-e2e-XXXXXX")"

export HOME="$WORK/home"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_CACHE_HOME="$HOME/.cache"
export XDG_RUNTIME_DIR="$WORK/xdg"
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

cleanup() {
  pkill -x ibus-daemon 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

export DBUS_SESSION_BUS_ADDRESS="$(dbus-daemon --session --fork --print-address=1)"
echo "BUS=$DBUS_SESSION_BUS_ADDRESS"

ibus-daemon --single --daemonize --panel=disable --config=disable -n probe
sleep 3
echo "--- daemon pid: $(pgrep -x ibus-daemon | head -1)"

echo "--- run engine probe + client ---"
"$BUILD/engine-probe" > "$WORK/engine.log" 2>&1 &
EPID=$!
sleep 2
timeout 30 "$BUILD/client" > "$WORK/client.log" 2>&1
echo "CLIENT_EXIT=$?"
sleep 1
kill "$EPID" 2>/dev/null

echo "=== ENGINE LOG ==="
cat "$WORK/engine.log"
echo "=== CLIENT LOG ==="
cat "$WORK/client.log"
echo "=== committed text ==="
grep -a "COMMIT" "$WORK/client.log" | sed 's/.*COMMIT: //' | head -3 || true

grep -aq "COMMIT: 你" "$WORK/client.log" && echo "PASS: end-to-end commit observed" || echo "FAIL: no commit observed"
