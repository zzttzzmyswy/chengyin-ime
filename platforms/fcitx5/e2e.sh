#!/usr/bin/env bash
# Headless end-to-end Fcitx 5 check for the Chengyin plugin (I13).
#
# Runs a REAL chain: private Xvfb display -> private session bus -> real fcitx5
# daemon with the staged chengyin plugin -> real X11 XIM client -> xdotool key
# injection -> Rust core -> committed text observed by the client.
#
# Nothing here is mocked. Where platforms/ibus/e2e.sh drives a real ibus-daemon,
# this drives a real fcitx5 and lets the framework's own XIM frontend carry the
# keys, which is the path a desktop session uses.
#
# Usage: e2e.sh <build-dir>
#
# Expects <build-dir> to contain the built chengyin.so and chengyin-e2e-client.
# Missing host dependencies are reported and skipped with exit code 77 (CTest
# registers this test with SKIP_RETURN_CODE 77), never a failure.
set -uo pipefail


HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${1:-$HERE/../../build/fcitx5}"
BUILD="$(cd "$BUILD" && pwd)"

# Every external tool this check needs. A missing one is a skip, not a failure:
# the e2e path is optional infrastructure, and a machine without Xvfb or xdotool
# can still build and unit-test the module.
missing=""
for tool in Xvfb xdotool dbus-daemon fcitx5 cc; do
  command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done
for header in /usr/include/X11/Xlib.h; do
  [[ -r "$header" ]] || missing="$missing libx11-dev"
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

# Private everything. A stale $HOME is what makes the ibus check flaky, and here
# it also keeps a real user's fcitx profile, lexicon and input history out of the
# test entirely.
WORK="$(mktemp -d "${TMPDIR:-/tmp}/chengyin-fcitx5-e2e-XXXXXX")"
export HOME="$WORK/home"
export XDG_CONFIG_HOME="$HOME/.config"
export XDG_DATA_HOME="$HOME/.local/share"
export XDG_CACHE_HOME="$HOME/.cache"
export XDG_RUNTIME_DIR="$WORK/run"
mkdir -p "$XDG_CONFIG_HOME/fcitx5/conf" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

DAEMON_PID=""
XVFB_PID=""
CLIENT_PID=""
DBUS_PID=""
cleanup() {
  # Only the PIDs this run started. `pkill fcitx5` / `pkill Xvfb` would also kill
  # a real session the user is running on this machine, which is never acceptable.
  [[ -n "$CLIENT_PID" ]] && kill "$CLIENT_PID" 2>/dev/null
  [[ -n "$DAEMON_PID" ]] && kill "$DAEMON_PID" 2>/dev/null
  [[ -n "$XVFB_PID" ]] && kill "$XVFB_PID" 2>/dev/null
  [[ -n "$DBUS_PID" ]] && kill "$DBUS_PID" 2>/dev/null
  wait 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

# Stage the plugin where the daemon will look for it, exactly as a distribution
# install would, and keep the system addon directory in the search path so the
# framework's own xcb/xim/keyboard addons are still found.
DEST="$WORK/stage"
DESTDIR="$DEST" cmake --install "$BUILD" >/dev/null

# The shipped addon is OnDemand=True, and the daemon resolves the profile's
# input-method group before an on-demand addon has been instantiated. The group
# item would therefore be dropped as invalid and the keys would go straight to
# the client, so this run stages the same addon with OnDemand=False - the
# configuration the project's own Xvfb probe already used to observe
# "Loaded addon chengyin". Nothing else about the module changes.
sed -i 's/^OnDemand=.*$/OnDemand=False/' "$DEST/usr/share/fcitx5/addon/chengyin.conf"
MODULE_DIR=""
for candidate in "$DEST/usr/lib/fcitx5" "$DEST/usr/lib/x86_64-linux-gnu/fcitx5" "$DEST/usr/lib64/fcitx5"; do
  [[ -f "$candidate/chengyin.so" ]] && MODULE_DIR="$candidate" && break
done

if [[ -z "$MODULE_DIR" ]]; then
  echo "FAIL: cmake --install did not stage chengyin.so under $DEST" >&2
  exit 1
fi

# Fixture lexicon and the profile that makes chengyin the default input method.
# The profile is what a user would have after adding the input method once.
write_file() { printf '%s' "$2" > "$1"; }
write_file "$WORK/lexicon.tsv" "$(printf 'ni\x27hao\t你好\t3000\nni\x27hao\t拟好\t2000\nzhong\x27guo\t中国\t4000\nma\t马\t900\nma\t妈\t800\nma\t麻\t700\n')"
write_file "$XDG_CONFIG_HOME/fcitx5/conf/chengyin.conf" "$(printf 'DictionaryPath=%s\n' "$WORK/lexicon.tsv")"

# A brand new Fcitx session starts inactive (Behavior/ActiveByDefault is off), so
# every key would go straight to the client and nothing would ever compose.
# Enabling it is exactly the state a user reaches after pressing the activate
# hotkey once; without it this script would trivially "pass" the escape scenario
# for the wrong reason.
write_file "$XDG_CONFIG_HOME/fcitx5/config" "[Behavior]
ActiveByDefault=True
"
write_file "$XDG_CONFIG_HOME/fcitx5/profile" "[Groups/0]
Name=Default
Default Layout=us
DefaultIM=chengyin

[Groups/0/Items/0]
Name=chengyin

[Groups/0/Items/1]
Name=keyboard-us

[GroupOrder]
0=Default
"

echo "--- staged module: $MODULE_DIR/chengyin.so"

# A private session bus: the daemon wants one, and reusing the real one would make
# this test depend on (and disturb) the user's session.
dbus-daemon --session --fork --print-address=1 --print-pid=1 >"$WORK/dbus.env" 2>/dev/null
DBUS_PID="$(sed -n 2p "$WORK/dbus.env")"
export DBUS_SESSION_BUS_ADDRESS="$(sed -n 1p "$WORK/dbus.env")"
if [[ -z "$DBUS_SESSION_BUS_ADDRESS" ]]; then
  echo "FAIL: could not start a private session bus" >&2
  exit 1
fi

# Pick a free display number with Xvfb itself, so two concurrent runs cannot
# collide on a hardcoded :99 and a real desktop is never touched.
Xvfb -displayfd 3 -screen 0 1024x768x24 >"$WORK/xvfb.log" 2>&1 3>"$WORK/display" &
XVFB_PID=$!
for _ in $(seq 1 100); do
  [[ -s "$WORK/display" ]] && break
  sleep 0.1
done
if [[ ! -s "$WORK/display" ]]; then
  echo "FAIL: Xvfb did not report a display" >&2
  exit 1
fi
export DISPLAY=":$(head -1 "$WORK/display")"
echo "--- display: $DISPLAY"

export FCITX_ADDON_DIRS="$MODULE_DIR:/usr/lib/fcitx5"
export FCITX_DATA_DIRS="$DEST/usr/share/fcitx5:/usr/share/fcitx5/testing"
export XMODIFIERS=@im=fcitx

# -D keeps the daemon in the foreground so $! is the daemon itself; --disable=all
# plus an explicit enable list keeps unrelated modules (and their background
# work) out of the test. dbus/dbusfrontend are needed because activation below
# goes through fcitx5-remote, which speaks the daemon's D-Bus interface.
fcitx5 -D --disable=all --enable=dbus,dbusfrontend,xcb,xim,keyboard,testfrontend,testim,chengyin \
  >"$WORK/daemon.log" 2>&1 &
DAEMON_PID=$!

# Wait for the XIM frontend to register on the display. XOpenIM in the client is
# the actual readiness test, so the loop below polls for the selection rather
# than sleeping a fixed time.
ready=0
for _ in $(seq 1 150); do
  if grep -q "Loaded addon xim" "$WORK/daemon.log" 2>/dev/null; then ready=1; break; fi
  kill -0 "$DAEMON_PID" 2>/dev/null || break
  sleep 0.1
done
if [[ "$ready" != 1 ]]; then
  echo "FAIL: fcitx5 daemon did not load the xim frontend" >&2
  tail -30 "$WORK/daemon.log" >&2
  exit 1
fi
echo "--- daemon ready (pid $DAEMON_PID)"



# One scenario: start the XIM client, wait for its own READY line (the XIM
# connection is established after the window is mapped, and a key injected
# before that is simply lost), focus its window, activate chengyin, then inject
# the keys.
#
# xdotool injects through XTEST (no --window). XTEST events travel the server's
# normal input path, which is what XIM reads; xdotool's --window uses
# XSendEvent, and XIM clients ignore those by design.
FAILED=0
run_scenario() {
  local name="$1" keys="$2"
  local capture="$WORK/captured-$name.txt"
  echo "=== scenario: $name"
  : > "$capture"
  # A one-line wrapper carries the capture path, because xterm runs its child
  # under a pty and the environment does not survive that hand-off.
  printf '#!/bin/sh\ncat > %s\n' "$capture" > "$WORK/sink.sh"
  chmod +x "$WORK/sink.sh"
  # -fa is required: this host has no classic bitmap fonts, and without an Xft
  # font xterm exits without ever mapping a window. Its window is found by
  # class, since -name only sets the instance name.
  xterm -display "$DISPLAY" -fa "DejaVu Sans Mono" -name "$name" \
    -e "$WORK/sink.sh" >"$WORK/xterm-$name.log" 2>&1 &
  CLIENT_PID=$!
  local win=""
  for _ in $(seq 1 150); do
    win="$(xdotool search --class xterm 2>/dev/null | head -1)"
    [[ -n "$win" ]] && break
    kill -0 "$CLIENT_PID" 2>/dev/null || break
    sleep 0.1
  done
  if [[ -z "$win" ]]; then
    echo "FAIL $name: xterm window never appeared" >&2
    head -5 "$WORK/xterm-$name.log" >&2
    FAILED=1
    kill "$CLIENT_PID" 2>/dev/null
    CLIENT_PID=""
    return
  fi
  # Raise and focus the window, then inject through XTEST (no --window): XTEST
  # events travel the server's normal input path, which is what XIM reads,
  # whereas xdotool's --window uses XSendEvent and XIM ignores those.
  xdotool windowactivate --sync "$win" 2>/dev/null || xdotool windowfocus "$win" 2>/dev/null
  sleep 1
  # One throwaway key: the XIM input context only becomes the daemon's most
  # recently focused context once it has delivered a key, and the switch below
  # is applied to that context.
  xdotool key --clearmodifiers space
  sleep 0.5
  # Activate chengyin through the daemon, which is what a user's language-switch
  # hotkey does. It is required rather than cosmetic: a fresh input context is
  # inactive, and Instance::inputMethod() returns the group's first entry while
  # inactive, so leaving this out would silently test the keyboard engine.
  fcitx5-remote -s chengyin >/dev/null 2>&1
  sleep 0.5
  for key in $keys; do
    xdotool key --clearmodifiers "$key"
    sleep 0.4
  done
  sleep 1
  kill "$CLIENT_PID" 2>/dev/null
  CLIENT_PID=""
  echo "--- captured: $(tr -d '\r' < "$capture" 2>/dev/null)"
}

# Scenario 1: nihao + space must commit 你好 through the whole chain.
run_scenario commit "n i h a o space"
grep -aq "^COMMIT: 你好$" "$WORK/captured-commit.txt" \
  || { echo "FAIL commit: expected exactly '你好'"; FAILED=1; }

# Scenario 2: Escape cancels the composition, so no Chinese word may be
# committed. (The throwaway space above is a raw space, not a commit.)
run_scenario escape "n i h a o Escape"
grep -aq "你好" "$WORK/captured-escape.txt" \
  && { echo "FAIL escape: 你好 was committed after Escape"; FAILED=1; }

# Scenario 3: digit 2 picks the fixture's second nihao entry (拟好), proving the
# injected digit reached the plugin's candidate list rather than the application.
run_scenario second "n i h a o 2"
grep -aq "拟好" "$WORK/captured-second.txt" \
  || { echo "FAIL second: expected '拟好'"; FAILED=1; }

if [[ "$FAILED" -eq 0 ]]; then
  echo "PASS: end-to-end commit observed through the real fcitx5 daemon"
else
  echo "FAIL: end-to-end assertions failed"
  echo "=== daemon log ==="
  grep -E "Group Item|Removed|Items in|Generated groups|Found 0" "$WORK/daemon.log"
fi
exit "$FAILED"
