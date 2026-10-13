# Linux CEF subprocess stack guard — diagnostic and scoped fix

**State:** Source-only candidate, pending exact-SHA native Linux build and runtime acceptance. Do not replace an installed stable Browser before native validation.

## Observations

An isolated CEF runtime printed `*** stack smashing detected ***: terminated` while the primary browser process could still complete tab creation and shutdown with exit code 0. Thus the main-process self-test alone does not establish every subprocess exited normally. An AppArmor sys_admin denial also appeared during a clean self-test, so it does not establish the crash cause.

Disassembly of the official stable binary showed `main` is guarded by `NO_STACK_PROTECTOR`, but intermediate non-inlined `RunBrowser` calls `CefExecuteProcess` and has compiler-inserted `%fs:0x28` stack-canary checks. Upstream CEF issue #3912 documents the same error signature caused by changing the stack guard on fork and the need to annotate all stack frames in the call chain.

Primary references:
- https://github.com/chromiumembedded/cef/issues/3912
- https://chromium.googlesource.com/chromium/src/+/main/base/stack_canary_linux.cc
- https://chromium.googlesource.com/chromium/src/+/main/docs/security/apparmor-userns-restrictions.md

## Minimum change

Apply `NO_STACK_PROTECTOR` to `RunBrowser` **only on Linux**, preserving Windows behavior. Keep Chromium sandbox, AppArmor and user namespace security settings intact; no insecure flags. `scripts/static-qa.sh` holds the fail-closed Linux annotation regression.

## Evidence, limitations and next action

Source regression RED before patch and GREEN after patch; static QA PASS. This is not compiled-product acceptance and has not changed any stable installation. Build isolated Linux candidate from pinned CEF, verify its RunBrowser no longer has canary instructions, and run a separate no-account isolated profile self-test. Require both stdout/stderr and chrome_debug.log to be free of stack-smashing, along with lifecycle markers, real X11 window presence, sandbox ownership, session preservation and explicit owner promotion gate.

## R1.1 — Linux CI stderr guard

The Linux native jobs in `.github/workflows/build.yml` and the
`linux-regression` job of `.github/workflows/windows-native.yml` previously
checked `chrome_debug.log` but not the captured subprocess stdout/stderr.
That was a **false-acceptance risk**: an aborted CEF utility process can print
a stack-canary error while the browser parent exits 0 and internal lifecycle
markers PASS. Both Linux jobs now fail closed (exit 36) on
`stack smashing detected` in the captured subprocess stream.

Regression: a retained, isolated Linux smoke stderr fixture containing the
stack-canary message is rejected by `grep -Fq`, while the clean control
stream passes. CI workflow YAML parses successfully; no Linux sandbox
settings or Windows-only jobs were modified. Local CEF-free native tests:
`companion-contract` and `tab-order-contract` PASS (2/2). Full CEF compile
and Ubuntu 26.04 native acceptance remain OPEN.
