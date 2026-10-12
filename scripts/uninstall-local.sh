#!/usr/bin/env bash
# R213 — legacy Browser uninstall is not an in-place update mechanism.
# Disable destructive behavior until owner login, rollback and exact payload
# retirement approval are independently proven. This script is read-only.
set -Eeuo pipefail
umask 077
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
VERSION=$(tr -d '[:space:]' < "$ROOT/VERSION")
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9._-]+)?$ ]] || {
  printf 'R213_UNKNOWN_VERSION_FAIL_CLOSED\n' >&2
  exit 64
}
RUNTIME="$HOME/.local/share/chatgpt-cef-v2/runtime-v$VERSION"
LAUNCHER="$HOME/.local/bin/chatgpt-cef-v2"
PROFILE="$HOME/.config/chatgpt-cef-v2"
arg=''
if [[ "$#" -ge 1 ]]; then arg="$1";fi
case "$arg" in
  --audit) [[ "$#" == 1 ]] || { echo R213_UNKNOWN_ARGUMENTS >&2; exit 64; } ;;
  --purge-profile)
    printf 'R213_PROFILE_PURGE_FORBIDDEN: authenticated browser state must never be removed by an uninstaller.\n' >&2
    exit 64
    ;;
  ''|--retire-runtime)
    [[ "$#" == 0 || "$#" == 1 ]] || { echo R213_UNKNOWN_ARGUMENTS >&2; exit 64; }
    ;;
  *) echo R213_UNKNOWN_ARGUMENTS >&2; exit 64 ;;
esac
reason='OWNER_ACCEPTANCE_AND_ROLLBACK_NOT_VERIFIED'
if [[ -L "$RUNTIME" || -L "$LAUNCHER" ]]; then
  reason='UNKNOWN_LINK_PROTECTED'
elif [[ -f "$LAUNCHER" ]]; then
  [[ "$(stat -c %s "$LAUNCHER")" -le 20000 ]] || { echo R213_UNKNOWN_LAUNCHER_PROTECTED >&2; exit 65; }
  if grep -Fq -- "$RUNTIME" "$LAUNCHER" ||
     grep -Fq -- "$RUNTIME/chrome-sandbox" "$LAUNCHER"; then
    reason='ACTIVE_RUNTIME_OR_SANDBOX_REFERENCE'
  fi
fi
if [[ -d "$PROFILE" || -L "$PROFILE" ]]; then
  printf 'R213_PROFILE_PRESERVED=%s\n' "$PROFILE"
fi
printf 'R213_RUNTIME_EXISTS=%s\n' "$([[ -e "$RUNTIME" ]] && echo yes || echo no)"
printf 'R213_RETIREMENT_STATUS=DEFER reason=%s\n' "$reason"
printf 'R213_DELETE_COUNT=0\n'
# --audit is successful READ-ONLY audit. All mutation intents refuse to delete.
[[ "$arg" == '--audit' ]] && exit 0
exit 3
