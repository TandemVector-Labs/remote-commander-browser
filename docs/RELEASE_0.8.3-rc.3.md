# Remote Commander Browser v0.8.3-rc.3 — optional unsigned preview

**Release channel: PRERELEASE / opt-in testing only.** This is **not v0.8.3 Stable**, not accepted for automatic deployment, and does not supersede the installed Stable fleet. Keep existing user profiles, cookies, credentials, private chats and Stable launcher untouched. Users must opt in to any trial installation and retain rollback paths.

## Changes since v0.8.3-rc.1

- Native CEF last-tab close/reopen, Light/Dark theme and shutdown lifecycle hardening inherited from v0.8.3-rc.2.
- Restore up to **16 native ChatGPT tabs** on restart (previously limited to 8); preserve order, selected tab, and legacy session format. A seventeenth saved URL is intentionally ignored.
- Normalize conversation UUID letter-case before counting and deduplicating observed conversations; maintain read-only, ephemeral observer behavior.
- Fail-closed Cppcheck/static QA if scanner itself fails, and maintain native Release test assertions.
- Windows Start Menu installer contract checks: actual launch target, icon, working directory, and unexpected arguments.
- Refuse user-facing Windows Installer execution in NT AUTHORITY\\SYSTEM/SID S-1-5-18 or systemprofile contexts even when environment paths are redirected; private stage-only QA remains available.

## Evidence from integrated exact source

Integration source commit **fc15778506762a9491aa6d139a4996f3b13b8914** from QA PR #133, tree **3be5fb0fb31fd7c0c707de919807099f8b40fdd6**, incorporating PRs #129, #130, and #132 on #127. Its exact-head GitHub Actions integration completed with **10/10 operational CI checks PASS**, publishing intentionally skipped on PR.

Independently verified on Mohammad Linux: static QA/Cppcheck PASS, Node 20/20 PASS, compiled native C++ CTest 4/4 PASS, full Linux CEF build PASS. Native CEF launched with 16 offline synthetic ChatGPT-format URLs; restored 16 tabs, recognized 16 conversations, retained selected tab 16 and cleanly shut down. Windows static native tests and actual SYSTEM/Administrator context guards passed on Emad and Amirreza Windows machines.

## Unresolved acceptance — important

The 16-tab runtime evidence uses synthetic, unauthenticated URLs, **not 16 logged-in ChatGPT chats/projects**. Independent human reviewer approval for PRs #127, #129, #130, #132 and #133 is outstanding. The actual Windows owner GUI install/last-tab-click acceptance and cross-user canary/rollback are not completed.

**Never automatically install or auto-update to this preview.** In particular, do not install via the background Windows SYSTEM agent or bypass native Chromium sandbox protections; for Windows use an authorized interactive user install with a separately tested profile. Unsigned executables may trigger OS security warnings. Do not suppress security tools or omit checksum/attestation validation.

## Packaging and trust

This pre-release must be published **only** by the repository release workflow after exact new-version commit CI, native Linux and Windows packaging, producer SHA256 checks and successful Sigstore artifact attestations. GitHub Release metadata must be `prerelease=true` and `latest=false`. On failed signing or integrity checks, do not publish any assets.

**Stable release v0.8.3 remains blocked** until independent reviewer acceptance, authenticated real-chat GUI proof, Windows owner-session acceptance, rollback and final release authorization.

Source: https://github.com/TandemVector-Labs/remote-commander-browser/pull/133
