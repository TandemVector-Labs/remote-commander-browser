#!/usr/bin/env bash
# Reproducible, unprivileged single-preview GitHub release auto-update service.
set -euo pipefail
umask 077
[[ "$(id -u)" -ne 0 ]] || { echo ROOT_UPDATER_DENIED >&2; exit 51; }
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/scripts/update-preview-from-github.py"
BIN="$HOME/.local/bin/rc-browser-github-updater"
LAUNCH="$HOME/.local/bin/rc-browser-glass-preview"
UNITS="$HOME/.config/systemd/user"
SERVICE="$UNITS/rc-browser-github-update.service"
TIMER="$UNITS/rc-browser-github-update.timer"
[[ -f "$SRC" && ! -L "$SRC" && -x "$SRC" ]] || exit 52
[[ -f "$LAUNCH" && ! -L "$LAUNCH" ]] || exit 53
[[ "$(sha256sum "$LAUNCH" |awk '{print $1}')" == '5cc4bc1e81d7526f9970d72027e2b28e972fce9255809cccdffeb87be92d0853' ]] || exit 54
[[ -d "$UNITS" && ! -L "$UNITS" ]] || exit 55
[[ ! -e "$BIN" && ! -e "$SERVICE" && ! -e "$TIMER" ]] || {
  echo UPDATE_TIMER_ALREADY_INSTALLED_OR_CONFLICT >&2; exit 56;
}
PYTHONDONTWRITEBYTECODE=1 /usr/bin/python3 "$SRC" --check | grep -Fq 'GITHUB_PREVIEW_UPDATE=NO_NEWER_STABLE_RELEASE current=0.8.3-rc.2'
SRC_SHA="$(sha256sum "$SRC"|awk '{print $1}')"
install -m 0700 "$SRC" "$BIN"
[[ "$(sha256sum "$BIN"|awk '{print $1}')" == "$SRC_SHA" ]] || exit 57
cat > "$SERVICE" <<SERVICE
[Unit]
Description=Single RC Browser Preview signed GitHub Release updater
Documentation=https://github.com/Usefull-Skills/remote-commander-browser/pull/123
ConditionPathExists=$LAUNCH

[Service]
Type=oneshot
ExecStart=/usr/bin/python3 $BIN --auto
Environment=PYTHONDONTWRITEBYTECODE=1
TimeoutStartSec=20min
NoNewPrivileges=true
PrivateTmp=true
SERVICE
cat > "$TIMER" <<TIMER
[Unit]
Description=Automatically check and apply signed stable updates for one RC Browser Preview

[Timer]
OnStartupSec=3min
OnUnitActiveSec=12h
Persistent=true
Unit=rc-browser-github-update.service

[Install]
WantedBy=timers.target
TIMER
chmod 0600 "$SERVICE" "$TIMER"
/usr/bin/systemd-analyze verify --user "$SERVICE" "$TIMER" >/dev/null 2>&1 || {
  echo USER_TIMER_UNIT_VERIFICATION_FAILED >&2; exit 58;
}
systemctl --user daemon-reload
systemctl --user enable --now rc-browser-github-update.timer
printf 'ONE_PREVIEW_GITHUB_UPDATER_INSTALLED\nSHA256=%s\n' "$SRC_SHA"
systemctl --user --no-pager status rc-browser-github-update.timer | head -12
