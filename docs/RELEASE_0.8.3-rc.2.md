# Remote Commander Browser v0.8.3-rc.2 — Emad development candidate only

**Authority:** Emad Windows PC + Emad Linux laptop are exclusive development/build/Native owner-acceptance hosts. Saeid Windows and MMZ Linux are STABLE-ONLY. One stable and one development installation per applicable product on Emad, one stable on downstream. DO NOT publish or activate this development candidate on Saeid/MMZ or enable automatic fleet updates. Preserve existing Stable and personal user browser sessions.

## R114 fix
Live user-like testing of immutable v0.8.3-rc.1 on a separate Saeid Windows profile reproduced a serious failure: clicking the last tab-X closes the whole browser, despite previous background Native synthetic tests passing. SHA256 of live failure log: f41520630d83e0da6ee761a4241d7b2f3a16052e54df4d0dfc69dd86fe54161a. R114 changes tab-X and Ctrl+W to post exact tab-close to next CEF UI task to prevent destroying a pressed button within OnButtonPressed, and adds CGWA_UI_TAB_CLOSE_SELFTEST marker based on the button handler route. Root cause is a PROBABLE UI reentrancy, not conclusively verified until actual click tests.

## Independent verification and rollout gates
Base R114 exact source 721d2f183ad923260b664cccf79d490c1ade08f7; Version bump only in R115 plus Windows workflow filename references. R114 Build and Windows Native Preview both SUCCESS. R115 also requires Build, Windows Native Preview and Release contract+package CI green at exact new source HEAD. CI PASS alone does not provide user-like GUI validation. 
On Emad Windows PC: isolated dev path, live stable browser never closed or logged out; click tab-X while one tab open, observe same browser parent PID/window handle remains and fresh tab exists. Verify two-tab close, Ctrl+W, actual close X, Light/Dark toggle, theme persistence, RTL, HiDPI without modifying personal cookies. Keep Stable v0.8.1 until separately accepted.
On Emad Linux laptop: separate dev path, owner-private root including safe ancestors, protected existing root:root 4755 sandbox with exact SHA, Native tab/theme/Shutdown and GUI click acceptance. Do not rewrite the old stable launcher or source.
Only after both Emad environments acceptance + Rollback proof, promote an independently versioned Stable to GitHub and test on Saeid/MMZ; never deploy this dev build on stable-only machines.

**Known remaining gates:** Core/control-center version drift, GUI STOP/Mutex, dashboard operational CRUD, workflow checkpoint, authenticated same-chat ACK, user Game/Video acceptance and four-host reversibility.
**Published reference:** v0.8.3-rc.1 is an immutable optional unsigned preview with GUI bug and is SUPERSEDED for development. It remains archived public, not stable acceptance.
**Account-transfer:** Last accepted cumulative Brain MAIN_R94 until a new independent verified ZIP with R114/R115 and Emad evidence is produced.
