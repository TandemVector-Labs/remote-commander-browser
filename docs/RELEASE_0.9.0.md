# Remote Commander Browser v0.9.0

## New browser generation

v0.9.0 is the intended next stable-numbered release of the native
Glass Browser. The former separately enabled Glass interface is
now the default; --rc-classic-chrome restores the earlier chrome when
an owner needs a UI rollback. Light/Dark palettes, rounded native
controls, vertical and horizontal tabs, and Chromium overlay scrolling
are native CEF interfaces rather than injected site CSS.

## Native session and Commander

- Restore up to 16 saved ChatGPT tabs with order and selected tab retained.
- Preserve the in-process, read-only conversation observer and
  separate private Commander companion status/monitoring path.
- Never import old cookies, passwords, ChatGPT profiles, or hidden sign-ins.
- Keep an isolated profile for preview/first-time tests and retain
  the existing runtime as rollback until each owner approves the
  default launcher replacement.

## Install packages

The signed/attested CI release workflow builds the Windows
setup, portable Windows ZIP, and Linux tarball. It verifies exact
asset checksums and Sigstore provenance before publishing the
immutable release. A v0.9.0 tag must point to the versioned
source commit containing this document. Do not bypass native
Chromium sandbox or system administrator approval for Linux
setuid sandbox installation.

## Validation status at version-preparation time

The parent Glass integration PR #135 has passing hosted Linux,
Windows Native, release integrity and companion-contract
workflows; its exact built binaries passed real isolated native
self-tests on Emad Linux, Emad Windows, and HPC-154-66.
These results cover the flagged preview parent source, not yet
the newly default-enabled v0.9.0 source. The actual v0.9.0
tagged-commit CI and publication must be separately verified.

Before full stable rollout: secure independent code review,
owner visual GUI acceptance, real authenticated multi-tab
session acceptance without modifying existing profiles, and
rollback on each client. MMZ laptop was temporarily unreachable
during the candidate stage. HPC-159-17 is a headless Commander
Core host and is not a desktop CEF target.

Do not call a version ready for public Stable adoption before
these remaining acceptance checks are complete.
Source lineage: https://github.com/TandemVector-Labs/remote-commander-browser/pull/135
