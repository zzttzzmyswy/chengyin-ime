#!/usr/bin/env bash
# Headless end-to-end IBus check for the Chengyin engine (I09 acceptance).
#
# Runs a REAL chain: private session bus -> real ibus-daemon -> the shipped
# ibus-engine-chengyin process -> Rust shared core -> commit signal back to a real
# client. Nothing here is mocked.
#
# Two IBus traps, both learned while producing the I07 design evidence, are
# handled below:
#   * ibus-daemon writes its bus address into $HOME/.config/ibus/bus/, and a
#     stale entry makes a new daemon refuse to start ("current session already
#     has an ibus-daemon"), so HOME is isolated per run;
#   * --single is required, or the daemon starts a second instance.
#
# Usage: e2e.sh <build-dir> [engine-name]
# Expects <build-dir> to already contain ibus-engine-chengyin and chengyin-e2e-client.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${1:-$HERE/../../build/ibus}"
ENGINE="${2:-chengyin}"
ENGINE_BIN="$BUILD/ibus-engine-chengyin"
CLIENT_BIN="$BUILD/chengyin-e2e-client"
# <exec> is spawned by the daemon from its own working directory, so both
# paths must be absolute before they go into the XML.
BUILD="$(cd "$BUILD" && pwd)"
ENGINE_BIN="$BUILD/ibus-engine-chengyin"
CLIENT_BIN="$BUILD/chengyin-e2e-client"

if [[ ! -x "$ENGINE_BIN" || ! -x "$CLIENT_BIN" ]]; then
  echo "missing build products in $BUILD; run cmake --build first" >&2
  exit 2
fi

WORK="$(mktemp -d "${TMPDIR:-/tmp}/chengyin-ibus-e2e-XXXXXX")"
export HOME="$WORK/home"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_CACHE_HOME="$HOME/.cache"
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_RUNTIME_DIR="$WORK/xdg"
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$XDG_DATA_HOME"
chmod 700 "$XDG_RUNTIME_DIR"

DAEMON_PID=""
ENGINE_PID=""
cleanup() {
  # Kill only the PIDs this run started: `pkill ibus-daemon` would also hit a
  # daemon owned by someone else on a shared machine.
  [[ -n "$ENGINE_PID" ]] && kill "$ENGINE_PID" 2>/dev/null
  [[ -n "$DAEMON_PID" ]] && kill "$DAEMON_PID" 2>/dev/null
  # gvfsd mounts a FUSE directory under XDG_RUNTIME_DIR lazily; unmount before
  # deleting so the removal does not fail with EBUSY.
  if command -v fusermount3 >/dev/null 2>&1; then
    fusermount3 -u "$XDG_RUNTIME_DIR/gvfs" 2>/dev/null
  fi
  rm -rf "$WORK"
}
trap cleanup EXIT

# The engine registers its own component on the bus at startup, so it is started
# directly here rather than declared through a component XML: packaging the XML
# is a separate iteration (I10). This keeps the check on the engine itself.
export DBUS_SESSION_BUS_ADDRESS="$(dbus-daemon --session --fork --print-address=1)"

ibus-daemon --single --daemonize --panel=disable --config=disable -n chengyin-e2e
sleep 3
DAEMON_PID="$(pgrep -x ibus-daemon | head -1)"
echo "--- daemon pid: ${DAEMON_PID:-none}"
if [[ -z "$DAEMON_PID" ]]; then
  echo "FAIL: ibus-daemon did not start" >&2
  exit 1
fi

"$ENGINE_BIN" > "$WORK/engine.log" 2>&1 &
ENGINE_PID=$!
sleep 2
echo "--- engine pid: $ENGINE_PID"
echo "--- client ---"
"$CLIENT_BIN" "$ENGINE" > "$WORK/client.log" 2>&1
CLIENT_STATUS=$?
echo "CLIENT_EXIT=$CLIENT_STATUS"

echo "=== ENGINE LOG ==="
cat "$WORK/engine.log"
echo "=== CLIENT LOG ==="
cat "$WORK/client.log"

echo "=== assertions ==="
FAILED=0
grep -aq "COMMIT: 你" "$WORK/client.log" || { echo "FAIL: expected COMMIT: 你"; FAILED=1; }
grep -aq "PREEDIT: ni" "$WORK/client.log" || { echo "FAIL: expected PREEDIT: ni"; FAILED=1; }
[[ $CLIENT_STATUS -eq 0 ]] || { echo "FAIL: client exit $CLIENT_STATUS"; FAILED=1; }

if [[ $FAILED -eq 0 ]]; then
  echo "PASS: end-to-end commit observed"
else
  echo "FAIL: end-to-end assertions failed"
fi
exit $FAILED
