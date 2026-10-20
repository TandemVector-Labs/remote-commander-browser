#!/usr/bin/env bash
set -Eeuo pipefail
# CI-only: two independent X11 servers and private CEF profiles.
# No host desktop / signed-in profile / network is used.
[[ "${GITHUB_ACTIONS:-}" == true && "${CI:-}" == true &&
   -n "${GITHUB_WORKSPACE:-}" && -n "${RUNNER_TEMP:-}" &&
   -n "${GITHUB_RUN_ID:-}" && -n "${GITHUB_RUN_ATTEMPT:-}" ]] ||
  { echo RMI_CI_ONLY; exit 71; }
[[ "$(id -u)" -ne 0 ]] || { echo RMI_REFUSE_ROOT; exit 72; }
for cmd in Xvfb xauth xdotool openssl sha256sum; do
  command -v "$cmd" >/dev/null || { echo "RMI_MISSING_TOOL=$cmd"; exit 73; }
done
exe="$(realpath build/bin/chatgpt-cef-v2)"
[[ "$exe" == "$GITHUB_WORKSPACE/build/bin/chatgpt-cef-v2" && -x "$exe" ]] ||
  { echo RMI_INVALID_EXECUTABLE; exit 74; }
root="$RUNNER_TEMP/rc-multi-native-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT"
[[ ! -e "$root" && ! -L "$root" ]] || { echo RMI_EXISTING_ROOT; exit 75; }
for disp in 476 477; do
  [[ ! -e "/tmp/.X11-unix/X$disp" && ! -e "/tmp/.X$disp-lock" ]] ||
    { echo RMI_DISPLAY_IN_USE; exit 76; }
done
umask 077
mkdir -m 700 "$root" "$root/a" "$root/b"
mkdir -m 700 "$root/a/home" "$root/b/home" "$root/a/cache" "$root/b/cache"
mkdir -m 700 "$root/a/profile" "$root/b/profile"
for id in a b; do
  printf 'horizontal\n' > "$root/$id/profile/ui-tabs-orientation.state"
  printf 'active=0\nhttps://chatgpt.com/?dual_%s=a\nhttps://chatgpt.com/?dual_%s=b\n' "$id" "$id" > "$root/$id/profile/tabs.state"
done
orig_a="$(sha256sum "$root/a/profile/tabs.state" | cut -d' ' -f1)"
orig_b="$(sha256sum "$root/b/profile/tabs.state" | cut -d' ' -f1)"
xa='' xb='' ca='' cb=''
cleanup(){
  local code=$?
  trap - EXIT
  for pid in "$ca" "$cb" "$xa" "$xb"; do
    [[ -z "$pid" ]] || kill -TERM "$pid" 2>/dev/null || :
  done
  for pid in "$ca" "$cb" "$xa" "$xb"; do
    [[ -z "$pid" ]] || wait "$pid" 2>/dev/null || :
  done
  mkdir -p .qa
  for id in a b; do
    for name in chrome.log native.stdout xvfb.log; do
      if [[ -f "$root/$id/$name" ]]; then
        cp -- "$root/$id/$name" ".qa/multi-$id-$name"
      fi
    done
  done
  if [[ "$root" == "$RUNNER_TEMP/rc-multi-native-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT" &&
        -d "$root" && ! -L "$root" ]]; then
    rm -rf -- "$root"
  fi
  return "$code"
}
trap cleanup EXIT
for id in a b; do
  if [[ "$id" == a ]]; then disp=476; else disp=477; fi
  cookie="$(openssl rand -hex 16)"
  xauth -f "$root/$id/Xauthority" add ":$disp" MIT-MAGIC-COOKIE-1 "$cookie" >/dev/null
  Xvfb ":$disp" -screen 0 1280x900x24 -nolisten tcp -auth "$root/$id/Xauthority" > "$root/$id/xvfb.log" 2>&1 &
  if [[ "$id" == a ]]; then xa=$!; else xb=$!; fi
done
for i in $(seq 1 60); do
  [[ -S /tmp/.X11-unix/X476 && -S /tmp/.X11-unix/X477 ]] && break
  kill -0 "$xa" 2>/dev/null && kill -0 "$xb" 2>/dev/null || break
  sleep 0.15
done
[[ -S /tmp/.X11-unix/X476 && -S /tmp/.X11-unix/X477 ]] ||
  { echo RMI_X_SERVERS_NOT_READY; exit 77; }
for id in a b; do
  if [[ "$id" == a ]]; then disp=476; else disp=477; fi
  (
    export DISPLAY=":$disp" XAUTHORITY="$root/$id/Xauthority"
    export HOME="$root/$id/home" XDG_CONFIG_HOME="$root/$id/home" XDG_CACHE_HOME="$root/$id/cache"
    export CGWA_PROFILE_ROOT="$root/$id/profile"
    unset WAYLAND_DISPLAY
    exec timeout -k 3 32 "$exe" --cgwa-profile-dir="$root/$id/profile" \
      --native-tab-drag-diagnostic \
      '--host-resolver-rules=MAP * ~NOTFOUND' \
      --log-file="$root/$id/chrome.log"
  ) > "$root/$id/native.stdout" 2>&1 &
  if [[ "$id" == a ]]; then ca=$!; else cb=$!; fi
done
tool(){
  local id="$1"; shift
  if [[ "$id" == a ]]; then disp=476; else disp=477; fi
  DISPLAY=":$disp" XAUTHORITY="$root/$id/Xauthority" xdotool "$@"
}
get_window(){
  local id="$1" wid=''
  for i in $(seq 1 60); do
    wid="$(tool "$id" search --onlyvisible --class ChatGPT-CEF-V2 2>/dev/null | head -1 || :)"
    [[ -n "$wid" ]] && break
    kill -0 "$ca" 2>/dev/null && kill -0 "$cb" 2>/dev/null || break
    sleep 0.2
  done
  [[ -n "$wid" ]] || { echo "RMI_WINDOW_NOT_FOUND_$id" >&2; return 1; }
  printf '%s' "$wid"
}
wa="$(get_window a)"
wb="$(get_window b)"
echo "RMI_BOTH_NATIVE_WINDOWS_READY a=$wa b=$wb"
drag(){
  local id="$1" wid="$2" geometry x y sx tx sy
  geometry="$(tool "$id" getwindowgeometry --shell "$wid")"
  x="$(awk -F= '$1=="X"{print $2}' <<< "$geometry")"
  y="$(awk -F= '$1=="Y"{print $2}' <<< "$geometry")"
  [[ "$x" =~ ^-?[0-9]+$ && "$y" =~ ^-?[0-9]+$ ]] ||
    { echo "RMI_WINDOW_GEOMETRY_INVALID_$id"; return 1; }
  sx=$((x+210)); tx=$((x+375)); sy=$((y+26))
  tool "$id" mousemove --sync "$sx" "$sy"
  sleep 0.1
  tool "$id" mousedown 1
  sleep 0.2
  tool "$id" mousemove --sync "$((sx+8))" "$sy"
  local ready=0
  for i in $(seq 1 20); do
    if [[ -f "$root/$id/chrome.log" ]] &&
      grep -Fq 'CGWA_NATIVE_DRAG_DIAG stage=press_sample ok=1 held=1 hit=1' "$root/$id/chrome.log"; then
      ready=1; break
    fi
    sleep 0.1
  done
  [[ "$ready" -eq 1 ]] ||
    { tool "$id" mouseup 1 || :; echo "RMI_PRESS_HANDSHAKE_FAILED_$id"; return 1; }
  tool "$id" mousemove --sync "$tx" "$sy"
  sleep 0.35
  tool "$id" mouseup 1
  sleep 0.7
  grep -q 'CGWA_NATIVE_TAB_DRAG_REORDER' "$root/$id/chrome.log" ||
    { echo "RMI_DRAG_MARKER_MISSING_$id"; return 1; }
  local expected actual
  expected="$(printf 'https://chatgpt.com/?dual_%s=b\nhttps://chatgpt.com/?dual_%s=a' "$id" "$id")"
  actual="$(sed -n '2,3p' "$root/$id/profile/tabs.state")"
  [[ "$actual" == "$expected" ]] ||
    { echo "RMI_REORDER_FAILED_$id"; return 1; }
}
drag a "$wa"
[[ "$(sha256sum "$root/a/profile/tabs.state"|cut -d' ' -f1)" != "$orig_a" ]] ||
  { echo RMI_A_NOT_CHANGED; exit 78; }
[[ "$(sha256sum "$root/b/profile/tabs.state"|cut -d' ' -f1)" == "$orig_b" ]] ||
  { echo RMI_B_CHANGED_DURING_A; exit 79; }
echo RMI_A_REORDER_PASS_B_UNTOUCHED
a_after="$(sha256sum "$root/a/profile/tabs.state"|cut -d' ' -f1)"
drag b "$wb"
[[ "$(sha256sum "$root/a/profile/tabs.state"|cut -d' ' -f1)" == "$a_after" ]] ||
  { echo RMI_A_CHANGED_DURING_B; exit 80; }
[[ "$(sha256sum "$root/b/profile/tabs.state"|cut -d' ' -f1)" != "$orig_b" ]] ||
  { echo RMI_B_NOT_CHANGED; exit 81; }
kill -0 "$ca"
kill -0 "$cb"
echo RMI_DUAL_POINTER_ISOLATION_PASS
