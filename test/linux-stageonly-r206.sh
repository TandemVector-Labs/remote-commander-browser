#!/usr/bin/env bash
set -Eeuo pipefail
umask 077
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
BUILD="$ROOT/build/bin"
STAGE="$ROOT/test/stage-r206/runtime-v$VERSION"
PROFILE="$HOME/.config/chatgpt-cef-v2"
LAUNCH="$HOME/.local/bin/chatgpt-cef-v2"
DESKTOP="$HOME/.local/share/applications/chatgpt-cef-v2.desktop"
[[ "$CI" == true && -n "$RUNNER_TEMP" ]] || { echo R206_GITHUB_CI_ONLY; exit 31; }
[[ ! -e "$BUILD" && ! -e "$ROOT/test/stage-r206" ]] || { echo R206_FIXTURE_ALREADY_EXISTS; exit 32; }
mkdir -p "$BUILD" "$PROFILE" "$(dirname "$LAUNCH")" "$(dirname "$DESKTOP")"
printf '#!/bin/sh\nexit 0\n' > "$BUILD/chatgpt-cef-v2"
chmod 0755 "$BUILD/chatgpt-cef-v2"
printf 'R206-FIXTURE-SANDBOX-NOT-ACTUAL-SUID-BINARY\n' > "$BUILD/chrome-sandbox"
chmod 0644 "$BUILD/chrome-sandbox"
printf 'OWNER-CI-PRIVATE-PROFILE-MARKER\n' > "$PROFILE/profile-marker-r206.txt"
printf '#!/bin/sh\nprintf "original launcher"\n' > "$LAUNCH"
chmod 0755 "$LAUNCH"
printf '[Desktop Entry]\nType=Application\nName=Original Browser\n' > "$DESKTOP"
chmod 0644 "$DESKTOP"
before="$(sha256sum "$PROFILE/profile-marker-r206.txt" "$LAUNCH" "$DESKTOP")"
bash "$ROOT/scripts/install-local.sh" --stage-only > "$RUNNER_TEMP/R206_STAGE.log"
grep -q 'R206_STAGE_ONLY_PASS' "$RUNNER_TEMP/R206_STAGE.log"
[[ -x "$STAGE/chatgpt-cef-v2" && -f "$STAGE/chrome-sandbox" ]]
[[ "$(sha256sum "$STAGE/chatgpt-cef-v2"|cut -d' ' -f1)" == "$(sha256sum "$BUILD/chatgpt-cef-v2"|cut -d' ' -f1)" ]]
[[ "$(sha256sum "$STAGE/chrome-sandbox"|cut -d' ' -f1)" == "$(sha256sum "$BUILD/chrome-sandbox"|cut -d' ' -f1)" ]]
after="$(sha256sum "$PROFILE/profile-marker-r206.txt" "$LAUNCH" "$DESKTOP")"
[[ "$before" == "$after" ]] || { echo R206_LOGIN_PROFILE_OR_LAUNCHER_MUTATED; exit 33; }
[[ ! -d "$HOME/.local/share/chatgpt-cef-v2/runtime-v$VERSION" ]] || { echo R206_PRODUCTION_RUNTIME_CREATED; exit 34; }
set +e
bash "$ROOT/scripts/install-local.sh" --stage-only > "$RUNNER_TEMP/R206_SECOND.log" 2>&1
code=$?
set -e
[[ "$code" -eq 24 ]] || { echo R206_RERUN_GUARD_FAILED; exit 35; }
[[ "$before" == "$(sha256sum "$PROFILE/profile-marker-r206.txt" "$LAUNCH" "$DESKTOP")" ]] || exit 36
echo R206_CI_STAGE_PROFILE_MARKER_AND_NO_REPLAY_PASS
