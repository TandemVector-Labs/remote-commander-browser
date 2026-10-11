#!/usr/bin/env bash
set -euo pipefail
umask 077

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
VERSION=$(tr -d '[:space:]' < "$ROOT/VERSION")
BUILD="$ROOT/build/bin"
APPROOT="$HOME/.local/share/chatgpt-cef-v2"
RUNTIME="$APPROOT/runtime-v$VERSION"
LAUNCHER="$HOME/.local/bin/chatgpt-cef-v2"
DESKTOP="$HOME/.local/share/applications/chatgpt-cef-v2.desktop"

[[ -x "$BUILD/chatgpt-cef-v2" ]] || { printf 'Build first: ./scripts/build-release.sh\n' >&2; exit 20; }
[[ -f "$BUILD/chrome-sandbox" ]] || { printf 'Build output is missing chrome-sandbox.\n' >&2; exit 21; }
# R206: StageOnly is an isolated developer-only payload copy.
# It never runs sudo, writes a launcher or desktop file, or accesses browser userdata.
if [[ "$#" -gt 0 && "$1" == '--stage-only' ]]; then
  [[ "$#" == 1 ]] || { echo R206_STAGE_ONLY_UNKNOWN_ARGUMENTS >&2; exit 23; }
  STAGE_BASE="$ROOT/test/stage-r206"
  STAGE="$STAGE_BASE/runtime-v$VERSION"
  [[ ! -e "$STAGE_BASE" && ! -L "$STAGE_BASE" ]] || { echo R206_STAGE_ONLY_NO_BLIND_RERUN >&2; exit 24; }
  [[ ! -L "$BUILD" && ! -L "$ROOT/build" ]] || { echo R206_BUILD_SYMLINK_DENIED >&2; exit 25; }
  [[ -z "$(find "$BUILD" -type l -print -quit)" ]] || { echo R206_SOURCE_SYMLINK_DENIED >&2; exit 25; }
  mkdir -m 700 "$STAGE_BASE"
  cp -a -- "$BUILD" "$STAGE"
  [[ -x "$STAGE/chatgpt-cef-v2" && -f "$STAGE/chrome-sandbox" ]] || { echo R206_STAGE_PAYLOAD_INCOMPLETE >&2; exit 26; }
  BIN_SHA="$(sha256sum "$BUILD/chatgpt-cef-v2" | cut -d' ' -f1)"
  SAN_SHA="$(sha256sum "$BUILD/chrome-sandbox" | cut -d' ' -f1)"
  [[ "$(sha256sum "$STAGE/chatgpt-cef-v2" | cut -d' ' -f1)" == "$BIN_SHA" ]] || { echo R206_STAGE_EXE_SHA_MISMATCH >&2; exit 27; }
  [[ "$(sha256sum "$STAGE/chrome-sandbox" | cut -d' ' -f1)" == "$SAN_SHA" ]] || { echo R206_STAGE_SANDBOX_SHA_MISMATCH >&2; exit 28; }
  printf 'R206_STAGE_ONLY_PASS version=%s root=%s binarySha=%s sandboxSha=%s\n' "$VERSION" "$STAGE" "$BIN_SHA" "$SAN_SHA"
  printf 'R206_USER_PROFILE_UNTOUCHED path=%s\n' "$HOME/.config/chatgpt-cef-v2"
  exit 0
fi
[[ "$#" == 0 ]] || { echo R206_UNKNOWN_PUBLIC_INSTALL_ARGUMENTS >&2; exit 29; }
[[ ! -e "$RUNTIME" ]] || { printf 'Runtime already exists: %s\nRefusing to overwrite.\n' "$RUNTIME" >&2; exit 22; }

mkdir -p "$APPROOT" "$HOME/.local/bin" "$HOME/.local/share/applications"
cp -a "$BUILD" "$RUNTIME"

printf 'Configuring Chromium SUID sandbox (sudo required)...\n'
sudo chown root:root "$RUNTIME/chrome-sandbox"
sudo chmod 4755 "$RUNTIME/chrome-sandbox"

BIN_SHA=$(sha256sum "$RUNTIME/chatgpt-cef-v2" | awk '{print $1}')
SAN_SHA=$(sha256sum "$RUNTIME/chrome-sandbox" | awk '{print $1}')

cat > "$LAUNCHER" <<LAUNCH
#!/usr/bin/env bash
set -euo pipefail
umask 077
RUNTIME="\$HOME/.local/share/chatgpt-cef-v2/runtime-v$VERSION"
BIN="\$RUNTIME/chatgpt-cef-v2"
SANDBOX="\$RUNTIME/chrome-sandbox"
EXPECTED_BIN_SHA="$BIN_SHA"
EXPECTED_SANDBOX_SHA="$SAN_SHA"
fail(){ printf 'ChatGPT CEF V2: %s\\n' "\$1" >&2; exit "\$2"; }
[[ -x "\$BIN" ]] || fail "executable missing" 20
[[ -f "\$SANDBOX" ]] || fail "sandbox missing" 21
read -r mode owner group < <(stat -Lc '%a %U %G' "\$SANDBOX")
[[ "\$mode" == 4755 && "\$owner" == root && "\$group" == root ]] || fail "unsafe sandbox permissions" 22
[[ "\$(sha256sum "\$BIN" | awk '{print \$1}')" == "\$EXPECTED_BIN_SHA" ]] || fail "binary SHA256 mismatch" 23
[[ "\$(sha256sum "\$SANDBOX" | awk '{print \$1}')" == "\$EXPECTED_SANDBOX_SHA" ]] || fail "sandbox SHA256 mismatch" 24
cd "\$RUNTIME"
exec "\$BIN" "\$@"
LAUNCH
chmod 0755 "$LAUNCHER"

cat > "$DESKTOP" <<DESKTOP
[Desktop Entry]
Version=1.0
Type=Application
Name=Remote Commander Browser
Comment=Remote Commander Browser native CEF shell
Exec=$LAUNCHER
TryExec=$LAUNCHER
Icon=applications-internet
Terminal=false
Categories=Network;Utility;
StartupWMClass=ChatGPT-CEF-V2
StartupNotify=false
DESKTOP
chmod 0644 "$DESKTOP"

printf 'INSTALL=PASS\nRUNTIME=%s\nLAUNCHER=%s\nBINARY_SHA256=%s\n' "$RUNTIME" "$LAUNCHER" "$BIN_SHA"
printf 'Profile data is stored separately at ~/.config/chatgpt-cef-v2 and is not modified by this installer.\n'
