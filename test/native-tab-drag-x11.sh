#!/usr/bin/env bash
set -euo pipefail
# R247 one-shot isolated X11 pointer acceptance; never read an owner's profile.
[[ "$(id -u)" -ne 0 ]] || { echo R247_REFUSE_ROOT_BROWSER; exit 31; }
[[ -n "$DISPLAY" && -n "$RUNNER_TEMP" && -n "$GITHUB_RUN_ID" && -n "$GITHUB_RUN_ATTEMPT" ]] || { echo R247_BAD_ENV; exit 32; }
command -v xdotool >/dev/null || { echo R247_XDOTOOL_MISSING; exit 33; }
exe="$(realpath build/bin/chatgpt-cef-v2)"
[[ "$exe" == "$GITHUB_WORKSPACE/build/bin/chatgpt-cef-v2" && -x "$exe" ]] || { echo R247_WRONG_BINARY; exit 34; }
root="$RUNNER_TEMP/r247-x11-drag-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT"
[[ ! -e "$root" && ! -L "$root" ]] || { echo R247_NO_BLIND_RERUN; exit 35; }
umask 077
mkdir -m 700 -- "$root"
profile="$root/profile"
mkdir -m 700 -- "$profile"
printf 'active=0\nhttps://chatgpt.com/?r247=a\nhttps://chatgpt.com/?r247=b\n' > "$profile/tabs.state"
original="$(sha256sum "$profile/tabs.state" | cut -d' ' -f1)"
"$exe" --cgwa-profile-dir="$profile" \
  --log-file="$profile/chrome_debug.log" \
  --native-tab-drag-diagnostic \
  '--host-resolver-rules=MAP * ~NOTFOUND' > "$root/native.stdout" 2>&1 &
pid=$!
finish() {
  if kill -0 "$pid" 2>/dev/null; then kill -TERM "$pid" 2>/dev/null || true; fi
  wait "$pid" 2>/dev/null || true
}
trap finish EXIT
wid=''
for _ in $(seq 1 40); do
  wid="$(xdotool search --onlyvisible --class 'ChatGPT-CEF-V2' 2>/dev/null | head -1 || true)"
  [[ -n "$wid" ]] && break
  sleep 0.25
done
[[ -n "$wid" ]] || { echo R247_NATIVE_WINDOW_NOT_FOUND; exit 36; }
geometry="$(xdotool getwindowgeometry --shell "$wid")"
x="$(printf '%s\n' "$geometry" | sed -n 's/^X=\(-\?[0-9]\+\)$/\1/p')"
y0="$(printf '%s\n' "$geometry" | sed -n 's/^Y=\(-\?[0-9]\+\)$/\1/p')"
[[ "$x" =~ ^-?[0-9]+$ && "$y0" =~ ^-?[0-9]+$ ]] || { echo R247_WINDOW_GEOMETRY_INVALID; exit 37; }
# Native layout at 1240 DIP: 164-DIP brand then two 135-DIP tab titles.
sx=$(( x + 210 )); tx=$(( x + 375 )); sy=$(( y0 + 26 ))
echo "R248_X11_COORDS wid=$wid window=${x},${y0} source=${sx},${sy} target=${tx},${sy}"
xdotool mousemove --sync "$sx" "$sy"
sleep 0.1
xdotool mousedown 1
sleep 0.25
xdotool mousemove --sync "$tx" "$sy"
sleep 0.35
xdotool mouseup 1
sleep 0.7
if [[ -f "$profile/chrome_debug.log" ]]; then
  echo "R248_NATIVE_TRACE_START"
  grep -E "CGWA_NATIVE_DRAG_DIAG|CGWA_NATIVE_TAB_DRAG_REORDER" "$profile/chrome_debug.log" | tail -55 || true
  echo "R248_NATIVE_TRACE_END"
else
  echo "R248_NATIVE_LOG_NOT_CREATED"
fi
[[ -f "$profile/tabs.state" ]] || { echo R247_STATE_MISSING; exit 38; }
state="$(sed -n '2,3p' "$profile/tabs.state")"
expected="$(printf 'https://chatgpt.com/?r247=b\nhttps://chatgpt.com/?r247=a')"
if [[ "$state" != "$expected" ]]; then
  echo "R247_NATIVE_REORDER_NOT_ACCEPTED input=$original current=$(sha256sum "$profile/tabs.state" | cut -d' ' -f1)"
  if [[ -f "$profile/chrome_debug.log" ]]; then
    grep 'CGWA_NATIVE_TAB_DRAG' "$profile/chrome_debug.log" | tail -10 || true
  fi
  exit 39
fi
[[ -f "$profile/chrome_debug.log" ]] &&
grep -q 'CGWA_NATIVE_TAB_DRAG_REORDER' "$profile/chrome_debug.log" ||
  { echo R247_MISSING_NATIVE_MARKER; exit 40; }
printf 'R247_NATIVE_DRAG_PASS isolated=1 inputSha=%s stateSha=%s\n' \
 "$original" "$(sha256sum "$profile/tabs.state" | cut -d' ' -f1)"
