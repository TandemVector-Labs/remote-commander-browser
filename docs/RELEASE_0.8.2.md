# Remote Commander Browser v0.8.2 — Candidate, NOT final

## New Native Browser interface
- 52-logical-pixel native header with larger ergonomic Windows/Linux controls and accessible keyboard tooltips.
- A compact tab selector keeps the active tab accessible in narrow windows. All 8 tab model entries persist, and Ctrl+1…8 / Ctrl+Tab remain available.
- Companion/Commander monitoring panel recalculates visible rows at resize and explicitly reports hidden count; unknown, blocked and disconnected data are not presented as healthy.
- Existing exact-origin media/clipboard permission policies, login/session persistence, Chrome sandbox and no-Browser-to-Commander-command design remain unchanged.

## Evidence — scoped
- Native Win64 MSVC full CEF 225/225 compile/link PASS and independent hosted Windows/Linux CI PASS on head R31.
- Pure C++17 viewport layout regression PASS and 5 browser source/accessibility/safety tests PASS.
- 61/61 isolated Companion contract tests PASS and private Windows file-owner ACL positive/negative gate PASS.
- Browser Headless/Native GUI interaction, HiDPI, RTL/LTR, owner-approved session privacy and four-host uninstall/rollback **still OPEN**. Native OS visual acceptance is not inferred from compile or synthetic preview.

## Release policy
The v0.8.1 tag/releases are immutable. This new v0.8.2 version is prepared in a **Draft PR** only; do not create the v0.8.2 production tag or point stable Auto Update to it until a valid code signature and Release Acceptance package verify. In particular, Linux root-owned `chrome-sandbox` mode 4755 still requires owner-local privilege; no --no-sandbox fallback. MMZ Commander tunnel is currently offline and its previous package root missing. Do not remotely promote it.

## Cross-product dependency
Core Commander baseline remains v0.10.20. Windows Control Center R34 and Linux GTK Control Center R36 are separate PRs and must be qualified for a coordinated release. A new Browser version alone never installs the Control Center on all hosts.
