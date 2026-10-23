# Remote Commander Browser v0.9.0

The new native Glass Browser generation uses plain semantic versioning.
Glass is the default user interface. Use --rc-classic-chrome to select
the legacy chrome without changing a browser profile or saved sessions.

Features:
- Native rounded Glass control surfaces, themes, compact horizontal and
  vertical tab modes, and Chromium overlay scrolling.
- Preserve up to 16 ChatGPT tabs, their order, and the active selection
  across restart; private local profiles are never reset.
- Read-only native chat observer and Commander Companion monitor remain
  isolated from browser-origin command execution.
- Cross-platform Windows and Linux CEF builds with pinned dependencies.

Important:
- ChatGPT/OpenAI login remains the normal website flow. The application
  is unofficial and not endorsed by OpenAI.
- Do not bypass the Chromium sandbox or transfer passwords/cookies
  during updates. On Linux the chrome-sandbox helper requires trusted
  root:root ownership and mode 4755.
- Keep the previous installed runtime for rollback until the owner
  accepts the new Glass GUI and real signed-in multi-tab workflows.
- The release branch is under review until exact tagged CI, signed
  checksums and provenance, independent code review and GUI acceptance
  are completed.

Project details: docs/RELEASE_0.9.0.md in the source repository.
