# R57 — Browser Windows version-pointer safety (Draft)

## Goal
Prevent breaking the installed Browser while switching the versioned `current` Junction. Older releases used `Remove-Item -LiteralPath $Current -Force -Recurse` before constructing the candidate link. This was not a safe automatic update or rollback primitive.

## New narrow behavior
- Resolve the exact installed `current` Junction and require it point to an existing version directory below the explicit install root. Unexpected files/directories/symlink targets fail closed.
- Refuse overwriting existing candidate versions. Copy a new candidate into an isolated staging path, compare the complete file list and per-member SHA against source, and commit a version directory only after validation.
- Create and validate the replacement Junction before touching `current`. Rename existing current to a preserved `current.rollback-* ` alias (not delete); promote the pending Junction and re-read its target.
- On an injected exception after either baseline move or new candidate move, restore the previous Junction and keep staged/failed artifacts for forensic review. Unknown outcome becomes DEGRADED/UNRESOLVED instead of a claimed PASS.
- Production fault injection is forbidden; the optional test flag is restricted to `-NoPublicIntegration` and the checked out repository’s `test/swap-r57` fixture root.

## Evidence
On Saeid Windows, fresh isolated installation and two injected mid-swap failures, successful upgrade with retained rollback Junction, immutable-version idempotency refusal, rejection of non-Junction current and refusal of fault injection in public mode all PASS. The exact installed Browser version `v0.8.1` pointer and Core Router SHA were unchanged after the tests.

First local receipt SHA256: `9154a989cf3c3687399d6c13b73bd1899c6835d874f24d04e33bd9ecb6341854`.
Portable clean-copy fixture receipt SHA256: `cb3cece4493ea1c802cf95f6ccb561e5d4f7f98d12e009799ace3d41b428ef7f`.

## Scope limitations / Open gates
This is a **Candidate** safety improvement, not public release authority. Installing the source without `-NoPublicIntegration` still requires independent full public-integration transactional tests and trusted code signing. Linux Browser sandbox owner-local sudo and native UI acceptance, MMZ tunnel connectivity, four-host canary install/uninstall/rollback, Control Center signed packaging and authenticated same-ChatGPT-conversation ACK all remain OPEN.

No user browser profile, cookie, credentials, native input, Windows Start Menu, installed Browser, production Router or owner files were touched in R57. Do **not** merge, tag, publish or enable automatic update until the remaining release gates independently PASS.
