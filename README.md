# Remote Commander Browser

A hardened, cross-platform native browser shell for Remote Commander workflows, built with Chromium Embedded Framework (CEF), with native multi-tab controls, Persian/Arabic RTL support, voice/media permissions scoped to ChatGPT, session restore, and a modern minimal UI.

> **Unofficial project.** This software is not affiliated with or endorsed by OpenAI. It loads `https://chatgpt.com/` and uses the normal ChatGPT sign-in flow.

## Release status

**Published Stable v0.8.1 — live local Commander monitoring, cross-platform.** Current development `main` includes post-release work and is not itself a published update.

Canonical source: [TandemVector-Labs/remote-commander-browser](https://github.com/TandemVector-Labs/remote-commander-browser). Previous Usefull-Skills paths are redirects; update new clone/setup instructions to use the canonical path.

v0.8.1 keeps Browser-origin command authority disabled, but adds a native live monitor panel fed by an owner-private local snapshot written by Remote Commander Core. The panel reports Core identity, GUI-control readiness, background-browser readiness, durable workflow count, Agent Extension count, and active operation/lock counts on both Windows and Linux. Existing chat/project binding evidence remains an independent fail-closed read-only layer.

The release baseline is qualified with strict warnings-as-errors, CEF-free positive/negative contract tests, isolated runtime lifecycle self-test, dependency checks, secret/privacy scanning, sandbox integrity, session/profile preservation, RTL/LTR acceptance, upload/clipboard/voice acceptance, and single-instance validation.

## Highlights

- Native CEF Views shell on Linux/X11
- Frameless window with native tabs (up to 8)
- `Ctrl+T`, `Ctrl+W`, `Ctrl+Tab`, `Ctrl+Shift+Tab`, `Ctrl+1..8`, `F11`
- Atomic session URL restore
- Single-instance relaunch handling
- Exact-origin permission policy for `https://chatgpt.com`
- Camera/microphone/clipboard support without broad origin grants
- Internal ChatGPT navigation stays in-app; external links route to the system browser
- Modern light-neutral shell with ghost controls and one accent color
- Vazirmatn-first Persian typography with automatic RTL/LTR detection
- Code/math isolation as LTR
- Hardened sandbox and launcher validation model
- No credentials or ChatGPT profile data stored in the repository

## Security model

The application uses Chromium's normal web isolation model and a Linux SUID sandbox. The project does **not** recommend `--no-sandbox`.

Media permissions are only granted by application policy to the exact trusted ChatGPT origin. Lookalike domains are not accepted by prefix matching.

Runtime browser data lives under:

```text
~/.config/chatgpt-cef-v2/
```

Do not commit or share that directory. It can contain authenticated browser state.

## Build

The project pins the tested CEF release:

```text
CEF 154.0.32+g682c378+chromium-154.0.8037.58
```

Quick build:

```bash
./scripts/fetch-cef.sh
./scripts/build-release.sh
```

See [docs/BUILDING.md](docs/BUILDING.md) for prerequisites and details.

## Test

```bash
./scripts/static-qa.sh
./scripts/runtime-self-test.sh
```

Runtime self-test requires an X11 display and a correctly configured Chromium sandbox.

## Install locally

After building:

```bash
./scripts/install-local.sh
```

The installer copies the runtime to the user's local application directory and configures `chrome-sandbox` as `root:root` mode `4755` using `sudo`. Review the script before running it.

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Roadmap

Future work is tracked in [docs/FUTURE_FEATURES.md](docs/FUTURE_FEATURES.md) and should be mirrored as GitHub issues in the company repository.

## Licensing and trademarks

See [NOTICE.md](NOTICE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Project-specific source code is not automatically granted an open-source license by this repository. Third-party components retain their original licenses.
