# Remote Commander Browser v0.8.3-rc.1 — UNSIGNED VOLUNTARY PREVIEW

## Scope and owner authorization
An optional unsigned installer and portable Linux/Windows Browser release. Users choose whether to install. Publisher signature is not required for voluntary preview; show UNSIGNED prominently and verify SHA256 from release assets. This version is NOT stable automatic deployment and is NOT complete four-host Remote Commander acceptance. Core v0.10.20 and user login/profile data must remain unchanged.

## User-reported defects addressed
- **Last tab no longer closes entire Browser:** Native R97 change creates and verifies a replacement tab before closing the last one; dedicated X still intentionally shuts the window.
- **Native Light/Dark switch and palette:** R98 adds a selectable header control, accessible labels, shared neutral high-contrast colors for tabs/Commander panel, and Chromium CEF color scheme. Preferences persist privately as `ui-theme.state` with atomic change and link safeguards.
- **First-run theme saving:** R99 permits a legitimately absent preference file while still rejecting symlinks and unsuitable destinations. R98 initial native test FAILED on Windows/Linux; new R99 exact-head Build/Windows Native Preview both SUCCESS, including Light/Dark toggle/persist/restore and last-tab lifecycle.

## Evidence and limits
- R97 code commit `8cae3649cd5c8e24f37a5fcf28432369cc565744`: Build and Windows Native Preview SUCCESS on Windows/Linux.
- R98 initial code `1bbab1a90fae4718064c39e55371f14252fa599a`: Native Theme FAIL, documented; not released.
- R99 corrected code `2514ea34a86c95cc7da72c595280d60a86a0a146`: Build run 38061283934 and Windows Native Preview 38061283720 both complete SUCCESS. Only isolated unauthenticated CI profile tested.
- R100 release packaging/version branch inherits R99 code unchanged; requires its own successful CI and independent installer/portable asset SHA and rollback checks prior to public publication.

## Installation and security
Windows Setup executable has no public publisher signature. Keep browser v0.8.1 production pointer as fallback and test v0.8.3-rc.1 with an isolated empty profile first. Do not automatically logout of ChatGPT or manipulate cookies. Do not force an update or bypass Windows security software.
Linux Chromium Sandbox requires exact verified `chrome-sandbox` with owner root:root mode 4755 locally authorized and inspected; never use `--no-sandbox`. Emad Linux previous Companion private ancestor guard requires separate safe-path validation; MMZ local protected sandbox has not been accepted yet.

## Gates still OPEN
Real owner's visual RTL/HiDPI/Dark appearance acceptance, functional profile/task Control Center CRUD, Emad Windows uncertain GUI controller input, Emad Linux native Companion, MMZ protected Browser install, Journal crash/restart proof, authenticated same-chat Runner ACK, and full four-host reversible canary. Games/Video owner acceptance also OPEN. Stable automatic deployment OFF.

Project Brain: latest accepted cumulative MAIN_R94 (SHA256 106c566683ab8883bac67a83bb93def0ac4353fa4fc1480526ec955775dc7a68); newer R97/R98/R99 source evidence pending next cumulative accepted Brain.

SUPERSEDED—DO NOT RUN: R98 failed binary, rejected R90 Brain, R77–R79 native Linux failed runner retries, stale installer branches and any automatic Stable promotion from hosted CI.
