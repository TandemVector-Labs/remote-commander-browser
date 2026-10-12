## 0.8.2 — candidate, not yet released (2026-10-10)

- R31 responsive native CEF toolbar, accessible tab selector and correctly bounded Companion panel.
- Preserve eight-tab model, existing ChatGPT login/profile, exact-origin permissions and sandbox.
- 225-step native MSVC build; standalone native layout matrix and 5 source tests PASS; hosted Windows/Linux CI PASS.
- Do not release/auto-install until Native owner visual, code signing, install/uninstall/rollback and four-host acceptance gates pass. Earlier 0.8.1 release remains immutable and authoritative for installed users.

## 0.8.0 - 2026-10-06

- Promote the rc.9-qualified runtime and release-integrity hardening to stable.
- Keep CEF runtime/source behavior unchanged from rc.9.
- Publish stable releases as latest while retaining explicit prerelease/latest=false semantics for future release candidates.
- Preserve all prior preview tags/assets without rewrite.

# CEF 0.8.0-rc.1 preview — 2026-10-02

Version the Saeid native companion and V03 source snapshots with explicit incomplete integration/platform gates. Stable automatic update is not enabled.

# Changelog

## v0.7.1 — 2026-10-01

### Added
- Modern minimal native shell with Vazirmatn-first Persian typography.
- Automatic Persian/Arabic RTL and English/code/math LTR handling.
- Exact-origin media/clipboard permission policy.
- Native multi-tab shell, keyboard accelerators and atomic tab-session persistence.
- Single-instance relaunch behavior.
- Isolated background self-test and staged-shutdown self-test support.

### Hardened
- Release build validated with `-Wall -Wextra -Wpedantic -Werror`.
- Project source passes configured `cppcheck` release scan.
- Fixed a 4 px native overlay offset mismatch introduced when the modern header height changed from 42 px to 38 px.
- Unified active-tab visual updates through one canonical update path.
- Removed a shadowed local variable and standardized tab lookup.
- Kept Linux Chromium sandbox enabled and validated.

### Validated
- Login/session persistence
- file/image upload
- clipboard
- dictation/microphone
- Voice start/stop
- RTL/LTR
- windowed/maximized/fullscreen
- tab lifecycle and staged shutdown
- single-instance behavior
- GPU/renderer health
