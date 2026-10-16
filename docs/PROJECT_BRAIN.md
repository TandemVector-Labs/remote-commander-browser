System.Object[]


---

## Archived pre-reconcile feature brain snapshot

> Preserved verbatim for provenance during rc.3 reconciliation. Current authority is the main-derived content above.

System.Object[]
## 2026-10-05 — Remote Commander Browser rc.3 finalization

- Status: candidate, not yet stable.
- Reconciled feature/v0.8.0-rc.2-windows with current main in isolated finalization clone; only PROJECT_BRAIN.md conflicted and history was preserved append-only.
- Product identity selected: Remote Commander Browser; repository migration target: Usefull-Skills/remote-commander-browser.
- Compatibility boundary: legacy executable/profile/runtime identifiers chatgpt-cef-v2 remain unchanged in rc.3 to preserve login/session state.
- Windows build reproducibility root cause found: inherited CC/CXX pointed to system LLVM clang, causing CEF MSVC flags to be passed to GNU-mode clang. Prevention: build-windows.ps1 clears CC/CXX and pins CMAKE_C_COMPILER/CMAKE_CXX_COMPILER to Visual Studio cl.exe.
- Windows local gates on rc.3 candidate: native build PASS; private-file safe-read PASS; broad Everyone-read ACL rejection PASS; native lifecycle/self-test PASS; package PASS.
- Windows package: remote-commander-browser-v0.8.0-rc.3-windows-x86_64.zip; SHA-256 81b2e5e1683197bebdc51c56cf1e6efbf74f070d9ce1248f24666f405da27ec1.
- Remaining promotion gates: exact-head hosted Windows/Linux CI, Linux native lifecycle/package, real UI observation/input smoke, Commander integration/recovery/rollback acceptance.

## 2026-10-05 — Release-line correction to rc.4

- Confirmed remote tag v0.8.0-rc.3 already existed on historical commit 6064c0693f917ad40211715cd4500249053a48fd.
- Decision: never force or rewrite the existing tag. The reconciled Remote Commander Browser candidate advances to v0.8.0-rc.4.
- Prior rc.3 qualification evidence remains historical evidence for the same product tree lineage; rc.4 must receive fresh exact-head hosted CI before tagging/publishing.

## 2026-10-05 - rc.4 Windows branded-shell candidate

- Exact local head: 604401cd4444a20f64fda688e1893a630d225349.
- Change: Windows EXE icon/resource, per-user versioned installer + Start Menu registration, and separation of Commander companion trust-state from Chromium authenticated profile state.
- Safety rationale: browser cookies/login state are not cleared or migrated; companion trust data now defaults under ChatGPTRemoteCommander/browser-companion while CGWA_PROFILE_ROOT remains compatibility fallback for isolated tests.
- Exact-head local Windows native build: PASS (225/225).
- Exact-head Windows package: PASS; SHA-256 4869caaaf54ef2a9c8064874d44e9c9a26a38303102bf498beef0db616e3baa7.
- Promotion status: LOCAL QUALIFIED ONLY. Remote push/hosted exact-head Windows+Linux CI and real authenticated UI/Commander E2E remain OPEN.

## 2026-10-05 — Window drag / browser-engine architecture audit
- Context: user reported Remote Commander Browser opens centered and cannot be moved; requested deep review of browser-engine architecture and whether multiple engines should be combined.
- Fact/CONFIRMED: current Browser candidate uses CEF 154.0.32 + Chromium 154.0.8037.58. The shell is frameless and previously marked only the brand-button area as draggable; because that area is itself a clickable button, runtime movement was not reliable/usable.
- Method evidence: Chromium-based CEF already exposes Chromium multi-process isolation and DevTools/CDP capabilities. Current external evidence also shows WebView2 stores independent user data/profile state and that multi-profile WebView2 is its own runtime/profile architecture; adding it beside CEF would create another session/runtime test matrix rather than improve the existing CEF path. Playwright connectOverCDP is officially documented as lower fidelity than the native Playwright protocol connection; therefore Playwright/CDP is suitable as an optional control/test layer, not as a second rendering engine.
- Decision/PROBABLE: retain one rendering engine (CEF/Chromium) and add control/observability through CEF DevTools/CDP where needed. Do not add WebView2/Firefox/WebKit without a concrete compatibility requirement that CEF cannot satisfy.
- Change: branch fix/window-drag-cdp-20261005 adds a dedicated 72px non-interactive header drag handle and maps the frameless draggable region to its actual laid-out bounds.
- Verification: clean Windows native rebuild PASS, 225/225, output build-windows/bin/chatgpt-cef-v2.exe. First build failed because CefPanel has no SetTooltipText; root cause fixed by removing the invalid call; clean rebuild then PASS.
- Validation: real pointer-drag acceptance is still UNPROVEN because the shared GUI-control lease was unavailable during the acceptance step. Do not promote this patch as final until real window movement is observed.
- Safety: candidate launch used an isolated temporary browser profile; authenticated production browser state/cookies/password store were not touched.
- Reuse Targets: Browser release notes, architecture, window-shell QA, automation/control design.


## 2026-10-05 — Full-header drag + authenticated-session safety adjudication
- Context: user reported the frameless Browser could not be moved from the full top bar and observed a logged-out ChatGPT window during QA.
- Fact/CONFIRMED — logout appearance: the logged-out window was the deliberately isolated QA instance launched with `--cgwa-profile-dir=C:\Users\Aa.Emad\AppData\Local\Temp\rc-browser-*-20261005`, not the installed production profile. The installed Start Menu shortcut has no profile override. Production state remained at `C:\Users\Aa.Emad\AppData\Local\chatgpt-cef-v2`; `Default\Network\Cookies`, `Default\Login Data`, and `Local State` remained present and their timestamps predated the isolated header QA. No production cookie/password-store mutation was performed.
- Root cause/CONFIRMED — window movement: the initial patch exposed only a 72 px dedicated drag spacer, which did not satisfy normal browser-title-bar behavior. CEF documents `CefWindow::SetDraggableRegions` as window-coordinate mouse-interception regions, so the corrected implementation marks the full visible header draggable and subtracts only real clickable controls and occupied tab items.
- Verification/PASS: clean Windows rebuild completed 225/225 build steps with exit 0; reproducible native `WM_NCHITTEST` scan on an isolated background profile returned `HTCAPTION` across 65 samples (large blank header range x=275..905) while 52 control/tab samples remained `HTCLIENT`; native lifecycle/companion/shutdown self-test returned `WINDOWS_NATIVE_LIFECYCLE_PASS`.
- Packaging/PASS: `remote-commander-browser-v0.8.0-rc.4-windows-x86_64.zip` SHA-256 `f99a8b483378899b5b2014b8fbc22ec279cde65114b4854c1144e675d19decb2`.
- Engine decision/CONFIRMED method choice: keep CEF/Chromium as the single rendering engine. Current CEF headers expose in-process `ExecuteDevToolsMethod` / `AddDevToolsMessageObserver` without requiring a remote-debugging session. Chrome explicitly warns that remote-debugging has been abused to extract cookies and recommends non-default user-data directories; therefore the authenticated production profile must not expose a remote-debugging port/pipe. Playwright documents CDP attachment as significantly lower fidelity than native Playwright protocol. WebView2 documentation confirms separate UDF/profile runtime state and extra resource cost for multiple UDF/runtime instances. Adding WebView2/Firefox/WebKit would therefore expand session/compatibility/test matrices without a demonstrated product outcome.
- External method sources: https://developer.chrome.com/blog/remote-debugging-port ; https://playwright.dev/docs/api/class-browsertype ; https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/user-data-folder ; https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/multi-profile-support
- Decision: no second rendering engine. Prefer CEF-native in-process DevTools APIs for any future visible-browser instrumentation; keep Commander background automation isolated from the authenticated Browser profile. A second engine requires a concrete compatibility failure plus evidence that it resolves the failure.
- Confidence/Status: CONFIRMED/HIGH for local Windows drag geometry, lifecycle, package and production-profile separation. Hosted exact-head Windows+Linux CI and release promotion remain OPEN.
- Reuse Targets: Browser architecture, release notes, session-safety policy, Commander Browser integration, QA.

### 2026-10-06 — Browser rc.5 standalone installer candidate

**Previous accepted state:** main/rc.4 had session-safe native Browser and read-only Commander companion, but no official standalone Setup EXE and Windows build required 7-Zip when extracting CEF.

**Current delta:** rc.5 candidate builds a native Windows Browser, ZIP package and standalone Inno Setup; the actual Setup completed isolated silent install with private companion ACL and zero production session-file changes. Windows CI now includes Setup build/install acceptance. 7-Zip is optional because Windows tar.exe is supported.

**Evidence:** contract/private-file/native lifecycle/package/Setup gates all PASS locally. Setup SHA-256 = `86162caa43a6ad265b677636072badaf49311b9abe476188d7943c76a592288f`.

**Open gate:** exact-head hosted Windows + Linux CI, merge/release, and production upgrade/readback. Authenticode signing is MISSING/EXTERNAL because no valid Code Signing certificate is installed.

**Exact next action:** commit/push rc.5 installer candidate, require exact-head CI PASS, then merge/tag/release and perform session-preserving live upgrade/readback.

### 2026-10-06 — Browser rc.5 hosted Setup blocker narrowed

**Current result:** PR #65 head `a324b8c`: Linux regression/build + companion contract PASS; Windows native fails only after Setup launches the embedded PowerShell installer. Custom QA paths are propagated correctly, but the child exits 1 and no install root is created. Setup currently masks that child failure with exit 0.

**← CURRENT:** enable official Inno child-output logging only, rerun exact hosted Windows gate, then fix the evidenced child error and make Setup fail closed on nonzero child exit. No merge/release until that sequence passes.

### 2026-10-06 — Browser rc.5 owner-repair / fail-closed Setup change

**Root cause:** hosted Windows proved `BROWSER_COMPANION_ACL_OWNER_MISMATCH`; parameter propagation and Browser build were good. The strict owner invariant remains authoritative.

**Current delta:** installer now repairs owner to the current identity with official Windows ACL tooling, verifies owner SID, then applies/verifies the private DACL. Setup now executes the child via Inno `ExecAndLogOutput` and treats child exit != 0 as fatal instead of returning success.

**← CURRENT:** local compile + isolated Setup regression, then exact-head hosted Windows/Linux gates. Merge/release remains blocked until hosted Windows PASS.

### 2026-10-06 — Browser local release gate PASS after owner/fail-closed repair

Local isolated Setup now passes both positive and negative paths on Windows. Good path installs rc.5 with private companion ACL; deterministic child failure returns Setup exit 200 and promotes nothing. Artifact SHA-256: `96793f036d62fe3ccad5dec2065e02c1dd97ecb6996d6e737fce01234ddcf396`.

**← CURRENT:** commit/push this exact change set and require hosted Windows native + Linux regression/build + companion contract PASS on the pushed SHA before merge/release.

### 2026-10-06 — Browser rc.5 exact DACL writer

**Previous blocker:** hosted Windows passed owner repair but failed strict DACL readback. Setup failure propagation is now proven fail-closed (exit 200).

**Current delta:** protected DACL is now written deterministically with a fresh `.NET DirectorySecurity` containing exactly current user + SYSTEM + Administrators FullControl ACEs; local script QA PASS. A structured diagnostic is emitted before any future verify failure.

**← CURRENT:** rebuild Setup with this helper, local Setup acceptance, commit/push exact head, require hosted Windows + Linux gates PASS. No release/merge before those gates.

### 2026-10-06 — Browser rc.5 local Setup PASS after exact DACL

**Result:** rebuilt Setup SHA `a0a96b...40c88` installed successfully in an isolated root with protected 3-rule companion ACL and no public integration. Previous hosted evidence already proved nonzero child failures propagate as Setup exit 200.

**← CURRENT:** commit/push the exact-DACL writer and local evidence; require hosted Windows native + Linux regression + build PASS on that exact head before merge/release.

### 2026-10-06 — Browser rc.6 release-infrastructure recovery

**Previous accepted product state:** rc.5 exact product tree qualified on hosted Windows/Linux and was merged as `f34f012`.

**Failure:** official rc.5 Release workflow did not publish because the mutable Chocolatey feed stopped serving pinned Inno Setup 6.7.3. Runtime/installer acceptance did not regress.

**Current delta:** rc.6 retains the Browser runtime and pins the official immutable Inno Setup 6.7.3 release asset by exact SHA-256 plus Authenticode signer verification. rc.5 tag remains unchanged for provenance.

**← CURRENT:** locally validate rc.6 metadata + Setup build/isolated install, then push PR and require exact-head Windows/Linux gates. Merge/tag/release only after all PASS.

### 2026-10-06 — Browser rc.6 local PASS

**Result:** official Inno 6.7.3 bootstrap script PASS by exact hash/signature; rc.6 Setup build PASS; silent isolated rc.6 Setup PASS with exact private 3-rule ACL.

**Prevention:** the same pinned compiler bootstrap is now exercised in PR Windows qualification and reused by tag Release, so mutable Chocolatey-feed drift cannot first appear only after tagging.

**← CURRENT:** commit/push rc.6 branch; require all hosted exact-head gates. No tag until merge-tree equality is verified.

### 2026-10-06 — Browser rc.6 Inno space-path fix

**Root cause:** shared bootstrap did not quote the portable installer `/DIR` path; hosted default contains spaces. Both independent hosted Windows runs failed at the same point.

**Fix + local proof:** quote `/DIR`; real default path with spaces now returns `INNO_SETUP_PIN_PASS` with exact hash/signer and compiler readback.

**← CURRENT:** push new exact head and require fresh Windows PR gate. Linux/companion on the prior runtime-equivalent head were already PASS.

### 2026-10-06 — Browser rc.7 release-layout gate

**Previous state:** rc.6 runtime/build gates PASS and tag Release workflow SUCCESS, but published Release omitted the standalone Windows Setup because it remained nested under `installer/` while publish selected top-level files only.

**Current delta:** rc.7 keeps runtime semantics unchanged, copies Setup to the release artifact root, and fails publication if the top-level Setup is missing.

**← CURRENT:** static/local release-contract validation, push PR, require exact-head Windows/Linux PASS, then merge/tag rc.7 and verify published Setup asset + checksum before Commander may pin it.

### 2026-10-06 — Browser rc.7 local layout PASS

**Result:** YAML parse/diff checks PASS; actual rc.7 Setup compiled with Inno 6.7.3; top-level release copy is byte-identical to built Setup (local SHA `4f21ca40...9fb181`). Runtime semantics remain inherited from qualified rc.6.

**← CURRENT:** commit/push rc.7 and require hosted Windows native Setup acceptance + Build/Linux PASS. Only then merge/tag and verify the published GitHub Release contains the top-level Setup asset and aggregate checksum.

### 2026-10-06 — Browser rc.8 release-path redesign

**Previous state:** rc.7 runtime and tag Windows/Linux builds PASS, but publish guard caught incomplete downloaded release layout. This was the third release-infrastructure failure family occurrence.

**Current delta:** explicit four-file Windows staging + exact filename/count guard + single staging upload; the same Release build path now runs on release-related PRs, with publish tag-only. Runtime semantics unchanged.

**← CURRENT:** validate YAML/diff locally, push rc.8 PR, require Release PR build + existing Windows/Linux gates; only then merge/tag and verify published Setup + checksums.

### 2026-10-06 — rc.8 staging PASS; upload selector narrowed

**Result:** hosted rc.8 stage produced exactly four required Windows files; Linux Release job PASS. Only upload selector failed on the Windows wildcard. Changed to official whole-directory upload while retaining exact pre-upload guard.

**← CURRENT:** push new exact head and require fresh Release PR PASS.

### 2026-10-06 — Browser rc.8 artifact action updated to native Node 24 release

**Evidence:** Windows four-file staging PASS twice; v4 upload search failed twice while running under GitHub's forced Node 24 compatibility path. Official current upload-artifact v7.0.1 is pinned by exact SHA `043fb46d...` for Release workflow only.

**← CURRENT:** fresh rc.8 Release PR gate on the pinned action; merge/tag only after it and existing product gates PASS.

### 2026-10-06 — Browser rc.8 release blocker root-caused

**Root cause:** a nullable `$LASTEXITCODE` check after the PowerShell-only packaging script silently exited the GitHub step with code 0 before installer/staging. This explains all “files not found” upload failures.

**Fix:** remove those inappropriate LASTEXITCODE guards; rely on script exceptions. Keep exact four-file staging guard. **← CURRENT:** fresh exact-head Release PR gate must show installer + staging runtime markers and upload PASS.


### 2026-10-06 - rc.9 release integrity recovery

Scope: metadata, producer checksums and release infrastructure only; source root isolated from active Commander development. Baseline b45cb2b5042318866fe4287bacd1323f4793718a. A new 14-case contract suite rejects the previous publication policy before mutation; the Windows post-change run passed 12 cases with 2 native POSIX skips. Native Linux packaging/symlink tests and all hosted exact-head gates remain open at this checkpoint. Whole product NOT_FINAL. See RELEASE_0.8.0-rc.9.md. Old rc.8 staging-PASS and upload-runtime root-cause claims are superseded by the independently reproduced nullable-LASTEXITCODE false-success. Existing rc.8 assets/tag are preserved; no production runtime or source/src changes.

Repository future-release immutability was enabled through the existing authorized CLI and independently read back true at 2026-10-06T11:35:15Z; existing rc.8 remains mutable and is not converted by this setting. Its preview classification was separately corrected without asset/tag changes.


### 2026-10-06 - R4 Linux generated launcher repair

An isolated execution of the actual generated installer on the previous candidate
failed before launching the Browser: RUNTIME: unbound variable. Real privilege
commands were replaced with guarded no-op fixtures; no installed profiles changed.
The nested unquoted heredoc expanded launcher-only variables during installation.
The launcher body now uses a quoted delimiter, with installation target and verified
hash values supplied through Bash printf %q. Existing ownership and digest guards
remain enabled. GNU Bash Redirections/Here Documents and Bash Builtins/printf are
the primary semantics reference; source syntax alone was not acceptance evidence.

A new native POSIX functional case covers generated installation, HOME with spaces,
literal argument forwarding including Unicode, and binary/sandbox tamper rejection.
The suite now has15 distinct cases: Windows can execute12 with3 POSIX skips. Native
Linux before/after proof and hosted exact-head checks must be separately recorded.
This fixes the generated shell contract; real CEF UI, SUID/privilege behavior and
full installer rollback remain separate acceptance gates. No tag or live rollout.


### 2026-10-06 — v0.8.0 stable promotion candidate

Previous accepted candidate: rc.9 exact head 95b8c01b5169201e7a2d1955ab72530e9fe73827 with hosted Release, Build/Linux-native, and Windows-native gates PASS. Stable mutation is identity/publication-policy only; CEF runtime/source behavior is unchanged.

Current gate: local stable release-integrity tests then hosted exact-head Release/Build/Windows-native on the stable identity. No tag/release before all hosted gates PASS.

### 2026-10-06 — stable launcher fixture identity correction

PR #70 exact head `9120c545820cd58d7eed53393f081218e280899a` passed hosted Linux native build/package and Windows native preview, but the Ubuntu release-contract job failed only because its POSIX fixture still hard-coded `runtime-v0.8.0-rc.9/chrome-sandbox` after the stable identity became `0.8.0`. The production launcher/package scripts and native builds were already PASS. The fixture now derives that path from `VERSION`. Exact corrected test SHA-256 is `3f15de3d357b0094981055b7a642c0b6e4452cffce49f67a9f3ddd60fd49b90f`; Linux full release-integrity suite is 15/15 PASS and Windows contract PASS with three POSIX-only skips. Runtime `src/` remains byte-unchanged from rc.9.

### 2026-10-16 — Native horizontal/vertical tab layout (MMZ isolated preview)

Scope: a user-selected left-side native CEF sidebar as an alternative to the original horizontal tab strip, preserving the same live BrowserView/overlay, tab order, close/select/drag behavior and ChatGPT profile. UI control `Tabs ↕ / Tabs ↔` stores a symlink-guarded owner-local `ui-tabs-orientation.state`; narrow windows fall back visually to horizontal without resetting the preference. No page injection, sandbox bypass, credential access, or refresh of the user's current running instance.

Source baseline: `157a4594efda745da8b0b9751a350d6157545c7e` in the separate MMZ preview worktree. Pinned CEF `154.0.32+g682c378+chromium-154.0.8037.58`; archive SHA-256 `9b6a82e04506d5e1af560e031e718c89af5f96760413fd16380358784545d153`. Local Linux full native CEF build PASS; `scripts/static-qa.sh` PASS; companion CTest 2/2; node UI/security 24/24; release-integrity 16/16; private Xvfb runtime `CGWA_VERTICAL_TABS_SELFTEST PASS vertical=1 horizontal=1 unchanged_tabs=1` together with existing lifecycle/shutdown self-tests PASS. Preview binary SHA-256 `96141006c935f563d96b9966108cda2a20481b8fe6f6be7788bf7107c7034a40`.

Acceptance scope: LOCAL_LINUX_PREVIEW_PASS only. The authenticated production session was intentionally not interrupted. Hosted exact-head Linux/Windows CI, interactive usability acceptance and stable version promotion are separate and not yet PASS. Next step: stage transactional promotion at the user's next natural browser restart, then verify live code identity and obtain real UI feedback. Preserve v0.8.3-rc.2 base as rollback evidence; do not relabel this feature as a published stable release.
