#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
VERSION=$(tr -d '[:space:]' < "$ROOT/VERSION")
APPROOT="$HOME/.local/share/chatgpt-cef-v2"
RUNTIME="$APPROOT/runtime-v$VERSION"
LAUNCHER="$HOME/.local/bin/chatgpt-cef-v2"
DESKTOP="$HOME/.local/share/applications/chatgpt-cef-v2.desktop"

# Do not let an old-version uninstall break a newer active SUID sandbox.
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z][0-9A-Za-z.-]*)?$ ]] || { printf 'UNINSTALL_VERSION_INVALID\n' >&2; exit 40; }
# Validate every affected ancestor below HOME before uninstall, sudo or profile purge.
# A linked parent redirects rm -rf even when RUNTIME itself is not a symlink.
for ancestor in "$HOME/.local" "$HOME/.local/share" "$APPROOT" \
                "$HOME/.local/bin" "$HOME/.local/share/applications"; do
  [[ ! -L "$ancestor" ]] || {
    printf 'UNINSTALL_ANCESTOR_REPARSE_REFUSED\n' >&2; exit 47;
  }
done
if [[ "${1:-}" == '--purge-profile' ]]; then
  for ancestor in "$HOME/.config" "$HOME/.config/chatgpt-cef-v2"; do
    [[ ! -L "$ancestor" ]] || {
      printf 'UNINSTALL_ANCESTOR_REPARSE_REFUSED\n' >&2; exit 47;
    }
  done
fi

[[ ! -L "$RUNTIME" ]] || { printf 'UNINSTALL_RUNTIME_REPARSE_REFUSED\n' >&2; exit 41; }
if [[ -d "$APPROOT" ]]; then
  for other in "$APPROOT"/runtime-v*; do
    [[ "$other" == "$RUNTIME" ]] && continue
    [[ -d "$other" || -L "$other" ]] || continue
    [[ ! -L "$other" ]] || { printf 'UNINSTALL_FOREIGN_RUNTIME_REPARSE_REFUSED\n' >&2; exit 42; }
    if [[ -L "$other/chrome-sandbox" ]]; then
      sandbox_real=$(readlink -f -- "$other/chrome-sandbox") || {
        printf 'UNINSTALL_FOREIGN_SANDBOX_DANGLING\n' >&2; exit 43;
      }
      if [[ "$sandbox_real" == "$RUNTIME/"* ]]; then
        printf 'UNINSTALL_SANDBOX_REFERENCED_BY_ANOTHER_VERSION\n' >&2
        exit 44
      fi
    fi
  done
fi

# Never delete the stable launcher when uninstalling a different runtime.
active_launcher=false
if [[ -L "$LAUNCHER" ]]; then
  printf 'UNINSTALL_LAUNCHER_REPARSE_REFUSED\n' >&2
  exit 45
fi
if [[ -f "$LAUNCHER" ]]; then
  literal_home="RUNTIME=\"\$HOME/.local/share/chatgpt-cef-v2/runtime-v$VERSION\""
  if grep -Fxq "RUNTIME=$RUNTIME" "$LAUNCHER" ||
     grep -Fxq "RUNTIME=\"$RUNTIME\"" "$LAUNCHER" ||
     grep -Fxq "$literal_home" "$LAUNCHER"; then
    active_launcher=true
  fi
fi
if [[ "${1:-}" == '--purge-profile' && "$active_launcher" != true ]]; then
  printf 'UNINSTALL_PROFILE_PURGE_REQUIRES_ACTIVE_RUNTIME\n' >&2; exit 46
fi

if [[ -d "$RUNTIME" ]]; then
  # chrome-sandbox may be root-owned; remove it explicitly with sudo if needed.
  if [[ -e "$RUNTIME/chrome-sandbox" && ! -w "$RUNTIME/chrome-sandbox" ]]; then
    sudo rm -f "$RUNTIME/chrome-sandbox"
  fi
  rm -rf "$RUNTIME"
fi
if [[ "$active_launcher" == true ]]; then
  rm -f "$LAUNCHER" "$DESKTOP"
fi

if [[ "${1:-}" == '--purge-profile' ]]; then
  rm -rf "$HOME/.config/chatgpt-cef-v2"
  printf 'PROFILE_PURGED=YES\n'
else
  printf 'PROFILE_PRESERVED=%s\n' "$HOME/.config/chatgpt-cef-v2"
fi
printf 'UNINSTALL=PASS\n'
