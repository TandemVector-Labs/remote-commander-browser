# Corrected architecture: Chat monitoring is part of Remote Commander Browser Core

**Owner decision, 2026-10-21:** ChatGPT tab/conversation observation and management MUST be built into **Remote Commander Browser core** and governed by Commander Core. It must NOT require an extension installed in Brave/Chrome, an external Native Messaging Host or a companion extension bridge.

## Implementation paths
- Chromium/CDP Commander browser: process-local Target lifecycle observer in `src/browser-native-chat-observer.mjs`, wired into `tools/browser-control.mjs`; read-only lease-scoped `browser_chat_tabs` via `src/browser-tools.mjs` and `src/browser-contract.mjs`. Strict origin/URL parsing, ephemeral HMAC references, bounded state, no DOM, cookies or raw conversation IDs exposed.
- CEF Remote Commander Browser: in-process native observer `src/native_chat_observer.h`, wired to CEF `OnAddressChange`, tab creation/activation/closure and the existing companion panel. No browser add-on; existing control-center/CEF panel remains part of browser product.
- Core authorizes durable project bindings and any future action. Browser observations are NOT account authentication or permission to send ChatGPT messages. Maintain independent ownership and user-approved action boundaries.

## Rejected architecture / migration
- Legacy `desktop/linux-control-center/chat-companion/` and private Emad `RC_MULTICHAT_EMAD_PILOT_R1/extension` are **QA prototypes only**, **deprecated/no-promote**, not prerequisites or architecture targets.
- Do not blindly delete owner state/installed Native Host or modify active browser sessions. Revoke legacy host registrations only after verifying exact path/ID, whether an active process depends on it, and independent rollback.
- Avoid touching MMZ's or Emad's unrelated worktrees. New independent git worktrees hold source prototypes for review. Neither is production installed.
- CEF currently caps tabs at **8** in `AppController::NewTab`; multi-chat **10+** acceptance remains OPEN until product tabs limits and safe resource model are qualified.

## Gate status
- Internal CDP source tests: 19 PASS, 0 FAIL on Emad and MMZ isolated source worktrees; no real authenticated account.
- CEF independent C++ observer test: PASS on Emad-PC with static MinGW executable. Full CEF build and UI not yet qualified.
- Real chat binding, authenticated operator identity, full CEF integration build and canary/release: OPEN.
- PR #316 remains DRAFT/NoPromote and must be reconciled with this architecture before review/merge.

**No promotion, no Core deployment, no GUI incident unlock, no delivery replay.**
