#!/usr/bin/env bash
set -euo pipefail
umask 077

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BIN="$ROOT/build/bin/chatgpt-cef-v2"
SANDBOX="$ROOT/build/bin/chrome-sandbox"
QA="$ROOT/.qa"
PROFILE=$(mktemp -d /tmp/chatgpt-cef-v2-selftest.XXXXXX)
trap 'rm -rf "$PROFILE"' EXIT
mkdir -p "$QA"

[[ -n "${DISPLAY:-}" ]] || { printf 'DISPLAY is not set; runtime self-test requires X11.\n' >&2; exit 20; }
[[ -x "$BIN" ]] || { printf 'Build first: ./scripts/build-release.sh\n' >&2; exit 21; }
[[ -e "$SANDBOX" ]] || { printf 'chrome-sandbox is missing.\n' >&2; exit 22; }
read -r mode owner group < <(stat -Lc '%a %U %G' "$SANDBOX")
if [[ "$mode" != '4755' || "$owner" != 'root' || "$group" != 'root' ]]; then
  printf 'Sandbox must be root:root mode 4755 before runtime testing.\n' >&2
  printf 'sudo chown root:root %q && sudo chmod 4755 %q\n' "$SANDBOX" "$SANDBOX" >&2
  exit 23
fi

set +e
timeout 18 "$BIN" \
  --cgwa-profile-dir="$PROFILE" \
  --background-test --self-test --shutdown-self-test \
  >"$QA/runtime-selftest.out" 2>&1
rc=$?
set -e
cp -f "$PROFILE/chrome_debug.log" "$QA/chrome_debug.log" 2>/dev/null || true

[[ "$rc" -eq 0 ]] || { printf 'RUNTIME_SELFTEST=FAIL rc=%s\n' "$rc" >&2; exit 24; }
# R105_OUTPUT_FATAL_BEGIN
# A zero parent exit does not prove the CEF child process was healthy.
# Suppress raw diagnostic output, which may contain private session data.
if [[ ! -f "$QA/runtime-selftest.out" || ! -r "$QA/runtime-selftest.out" ]]; then
  printf 'RUNTIME_SELFTEST=FAIL captured output unavailable.\n' >&2
  exit 31
fi
fatal_scan_rc=0
grep -Eq 'FATAL|SIGABRT|SIGSEGV|segfault|stack smashing detected|stack buffer overflow|AddressSanitizer|ThreadSanitizer|GPU process isn.t usable' "$QA/runtime-selftest.out" || fatal_scan_rc=$?
case "$fatal_scan_rc" in
  0) printf 'RUNTIME_SELFTEST=FAIL child fatal in captured output; inspect private QA log.\n' >&2; exit 30 ;;
  1) ;;
  *) printf 'RUNTIME_SELFTEST=FAIL captured output scan error.\n' >&2; exit 31 ;;
esac
# R105_OUTPUT_FATAL_END

LOG="$QA/chrome_debug.log"
[[ -f "$LOG" ]] || { printf 'CEF debug log not produced.\n' >&2; exit 25; }
grep -q 'CGWA_SELFTEST PASS' "$LOG" || { printf 'Tab lifecycle PASS marker missing.\n' >&2; exit 26; }
grep -q 'CGWA_SHUTDOWN FINALIZE' "$LOG" || { printf 'Shutdown finalize marker missing.\n' >&2; exit 27; }
grep -q 'CGWA_SHUTDOWN WINDOW_DESTROYED' "$LOG" || { printf 'Window-destroyed marker missing.\n' >&2; exit 28; }
if grep -Eq 'FATAL|GPU process isn.t usable|segfault|stack smashing|trap' "$LOG"; then
  grep -E 'FATAL|GPU process isn.t usable|segfault|stack smashing|trap' "$LOG" >&2
  exit 29
fi

printf 'RUNTIME_SELFTEST=PASS\n'
