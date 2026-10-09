#!/usr/bin/env bash
# Real-daemon end-to-end Fcitx 5 check for the Chengyin plugin (I14).
#
# Runs a REAL chain: private session bus -> real fcitx5 daemon with the staged
# chengyin plugin -> real D-Bus input context -> Rust core -> committed text
# observed by the client as a D-Bus signal.
#
# Nothing here is mocked. Where I13 drove this through the X11 XIM frontend and
# could not recover the commit, this drives the framework's own D-Bus frontend,
# which is the path a D-Bus client (and the desktop's own portal integration)
# uses. See the "Why D-Bus and not XIM" note below.
#
# Usage: e2e.sh <build-dir>
#
# Expects <build-dir> to contain the built chengyin.so and chengyin-e2e-client.
# Missing host dependencies are reported and skipped with exit code 77 (CTest
# registers this test with SKIP_RETURN_CODE 77), never a failure.
#
# Why D-Bus and not XIM (I13's blocker, kept here as the record):
#   fcitx5's XIM frontend returns committed text inside the X event stream
#   (XIM_COMMIT arrives as a synthetic KeyPress that the client must read with
#   Xutf8LookupString). A headless client only receives those events while it
#   owns the X input focus, and under Xvfb with no window manager the focus
#   never settled on the test window - XFilterEvent on every event, both
#   callback and Root styles, XNFocusWindow, focus after MapNotify, and
#   injecting before activating were all tried. The D-Bus frontend instead
#   delivers CommitString as a signal on the input-context object, so the
#   assertion needs no X server, no window manager and no focus at all.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${1:-$HERE/../../build/fcitx5}"
if [[ ! -d "$BUILD" ]]; then
  echo "SKIP: build directory $BUILD does not exist; run cmake --build first" >&2
  exit 77
fi
BUILD="$(cd "$BUILD" && pwd)"

# Every external tool this check needs. A missing one is a skip, not a failure:
# the e2e path is optional infrastructure, and a machine without a daemon to
# drive can still build and unit-test the module.
missing=""
for tool in dbus-daemon fcitx5 cmake; do
  command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
if [[ -n "$missing" ]]; then
  echo "SKIP: missing host dependencies:$missing" >&2
  exit 77
fi

MODULE="$BUILD/chengyin.so"
CLIENT="$BUILD/chengyin-e2e-client"
if [[ ! -f "$MODULE" || ! -x "$CLIENT" ]]; then
  echo "SKIP: $BUILD does not contain chengyin.so and chengyin-e2e-client; run cmake --build first" >&2
  exit 77
fi

# Private everything. A stale $HOME is what makes these checks flaky, and here
# it also keeps a real user's fcitx profile, lexicon and input history out of
# the test entirely. DISPLAY is cleared rather than reused: this run wants no X
# connection at all, and inheriting a real session's display would only invite
# the daemon to touch a desktop that is not ours.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/chengyin-fcitx5-e2e-XXXXXX")"
export HOME="$WORK/home"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_CACHE_HOME="$HOME/.cache"
export XDG_RUNTIME_DIR="$WORK/run"
unset DISPLAY
mkdir -p "$XDG_CONFIG_HOME/fcitx5/conf" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

DAEMON_PID=""
DBUS_PID=""
cleanup() {
  # Only the PIDs this run started. `pkill fcitx5` / `pkill Xvfb` would also kill
  # a real session the user is running on this machine, which is never acceptable.
  [[ -n "$DAEMON_PID" ]] && kill "$DAEMON_PID" 2>/dev/null
  [[ -n "$DBUS_PID" ]] && kill "$DBUS_PID" 2>/dev/null
  wait 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

# Stage the plugin where the daemon will look for it, exactly as a distribution
# install would, and keep the system addon directory in the search path so the
# framework's own keyboard/dbus addons are still found. The addon files are used
# exactly as shipped - no configuration is rewritten to make the test pass.
DEST="$WORK/stage"
DESTDIR="$DEST" cmake --install "$BUILD" >/dev/null

MODULE_DIR=""
for candidate in "$DEST/usr/lib/fcitx5" "$DEST/usr/lib/x86_64-linux-gnu/fcitx5" "$DEST/usr/lib64/fcitx5"; do
  [[ -f "$candidate/chengyin.so" ]] && MODULE_DIR="$candidate" && break
done
if [[ -z "$MODULE_DIR" ]]; then
  echo "FAIL: cmake --install did not stage chengyin.so under $DEST" >&2
  exit 1
fi

FCITX_ADDON_DIRS="$MODULE_DIR:/usr/lib/fcitx5:/usr/lib/x86_64-linux-gnu/fcitx5:/usr/lib64/fcitx5"
FCITX_DATA_DIRS="$DEST/usr/share/fcitx5:/usr/share/fcitx5"
export FCITX_ADDON_DIRS FCITX_DATA_DIRS

# Fixture lexicon and the profile that makes chengyin the group's input method.
# The profile is what a user would have after adding the input method once. The
# TSV is the same shape I13 used, so scenario 3's "second candidate" is a
# property of the fixture and not of an installed dictionary.
printf "ni'hao\t你好\t3000\nni'hao\t拟好\t2000\nzhong'guo\t中国\t4000\nma\t马\t900\nma\t妈\t800\nma\t麻\t700\n" >"$WORK/lexicon.tsv"
printf 'DictionaryPath=%s\n' "$WORK/lexicon.tsv" >"$XDG_CONFIG_HOME/fcitx5/conf/chengyin.conf"
printf '[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=chengyin\n\n[Groups/0/Items/0]\nName=chengyin\n\n[Groups/0/Items/1]\nName=keyboard-us\n\n[GroupOrder]\n0=Default\n' >"$XDG_CONFIG_HOME/fcitx5/profile"

echo "--- staged module: $MODULE_DIR/chengyin.so"

# A private session bus: the daemon needs one, and reusing the real one would
# make this test depend on (and disturb) the user's session.
dbus-daemon --session --fork --print-address=1 --print-pid=1 >"$WORK/dbus.env" 2>/dev/null
DBUS_PID="$(sed -n 2p "$WORK/dbus.env")"
DBUS_SESSION_BUS_ADDRESS="$(sed -n 1p "$WORK/dbus.env")"
export DBUS_SESSION_BUS_ADDRESS
if [[ -z "$DBUS_SESSION_BUS_ADDRESS" ]]; then
  echo "FAIL: could not start a private session bus" >&2
  exit 1
fi

# -D keeps the daemon in the foreground so $! is the daemon itself; --disable=all
# plus an explicit enable list keeps unrelated modules (and their background
# work, X connections included) out of the test. dbus/dbusfrontend are the path
# this check drives; keyboard supplies the fallback entry the profile's group
# expects; chengyin is the plugin under test.
fcitx5 -D --disable=all --enable=dbus,dbusfrontend,keyboard,chengyin \
  >"$WORK/daemon.log" 2>&1 &
DAEMON_PID=$!

# The daemon registers its D-Bus name before it has loaded every addon, and
# chengyin - which is OnDemand in the shipped addon file - is one of the last,
# roughly a second later. Both facts have to hold before a key can be typed, so
# poll for the plugin load itself: that is the real readiness condition.
ready=0
for _ in $(seq 1 300); do
  if grep -q "Loaded addon chengyin" "$WORK/daemon.log" 2>/dev/null; then ready=1; break; fi
  kill -0 "$DAEMON_PID" 2>/dev/null || break
  sleep 0.1
done
if [[ "$ready" != 1 ]]; then
  echo "FAIL: fcitx5 daemon did not load the staged chengyin plugin" >&2
  tail -30 "$WORK/daemon.log" >&2
  exit 1
fi

# Separate a missing framework frontend from a broken plugin. A fcitx5 built or
# packaged without libdbusfrontend cannot run this check at all, which is a host
# dependency problem (skip), whereas a daemon that refuses to load chengyin is
# the failure this test exists to report.
if ! grep -q "Loaded addon dbusfrontend" "$WORK/daemon.log"; then
  echo "SKIP: this fcitx5 has no dbusfrontend addon; cannot drive the daemon over D-Bus" >&2
  grep -iE "dbusfrontend|Failed to load" "$WORK/daemon.log" >&2
  exit 77
fi
echo "--- daemon ready (pid $DAEMON_PID)"
echo "--- loaded chengyin from $MODULE_DIR, addons: dbus dbusfrontend keyboard chengyin"

# One scenario. The client owns the whole assertion: it polls the daemon's own
# preedit signal to prove each keystroke reached the engine, only then sends the
# terminating key, and waits on a bounded deadline for the terminal condition.
# It prints COMMIT: <text> per commit and exits non-zero with a named reason if
# a stage never happens, so the greps below only have to check the text.
FAILED=0
run_scenario() {
  local name="$1"
  local capture="$WORK/captured-$name.txt"
  echo "=== scenario: $name"
  "$CLIENT" "$name" >"$capture" 2>"$WORK/client-$name.err"
  local status=$?
  sed 's/^/    /' "$capture"
  if [[ "$status" -ne 0 ]]; then
    sed 's/^/    /' "$WORK/client-$name.err" >&2
    FAILED=1
  fi
}

run_scenario commit
grep -aq "^COMMIT: 你好$" "$WORK/captured-commit.txt" \
  || { echo "FAIL commit: expected exactly '你好'" >&2; FAILED=1; }

run_scenario escape
grep -aq "你好" "$WORK/captured-escape.txt" \
  && { echo "FAIL escape: 你好 was committed after Escape" >&2; FAILED=1; }

run_scenario second
grep -aq "^COMMIT: 拟好$" "$WORK/captured-second.txt" \
  || { echo "FAIL second: expected '拟好' from the second candidate" >&2; FAILED=1; }

if [[ "$FAILED" -eq 0 ]]; then
  echo "PASS: end-to-end commit observed through the real fcitx5 daemon over D-Bus"
else
  echo "FAIL: end-to-end assertions failed"
  echo "=== daemon log ==="
  grep -iE "chengyin|Failed to load|dbusfrontend" "$WORK/daemon.log"
fi
exit "$FAILED"
