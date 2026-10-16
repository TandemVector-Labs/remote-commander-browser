'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const {join}=require('node:path');
const c=fs.readFileSync(join(__dirname,'../src/controller.cc'),'utf8');
const h=fs.readFileSync(join(__dirname,'../src/controller.h'),'utf8');
test('CEF tab buttons are reused during layout and tab changes',()=>{
 const rebuild=c.split('void AppController::RebuildTabStrip() {')[1]?.split('void AppController::UpdateTabButton(')[0];
 assert.ok(rebuild);
 assert.match(rebuild,/if \(tab\.ui_panel && tab\.title_button && tab\.close_button/);
 assert.match(rebuild,/AddChildView\(tab\.ui_panel\)/);
 assert.doesNotMatch(rebuild,/tab\.ui_panel = nullptr/);
 assert.doesNotMatch(rebuild,/tab\.close_button = nullptr/);
 assert.equal((rebuild.match(/CefLabelButton::CreateLabelButton/g)||[]).length,2);
});
test('deferred retirement keeps CEF view references beyond native callbacks',()=>{
 assert.match(h,/std::vector<RetiredUi> retired_ui_/);
 assert.match(c,/RetireTabUi\(\*it\);\s*tabs_\.erase\(it\)/);
 assert.match(c,/CefPostDelayedTask\(TID_UI, base::BindOnce\(\[\]\(\) \{/);
 assert.match(c,/void AppController::ReleaseRetiredUi\(\)/);
 assert.match(c,/1500\)/);
});
test('long-running 24-cycle native-soak gate is explicit test-only',()=>{
 assert.match(c,/HasSwitch\("tab-lifetime-soak-test"\)/);
 assert.match(c,/CGWA_LIFETIME_SOAK_REQUIRES_BACKGROUND/);
 assert.match(c,/if \(cycle >= 24\)/);
 assert.match(c,/CGWA_NATIVE_TAB_LIFETIME_SOAK PASS/);
 assert.match(c,/QueueTabClose\(closed_id\)/);
});

test('overlapping tab retirements cannot drop younger button refs',()=>{
 assert.match(c,/std::chrono::steady_clock::now/);
 assert.match(c,/std::chrono::milliseconds\(1250\)/);
 assert.match(c,/return item.created <= limit/);
 const release = c.split('void AppController::ReleaseRetiredUi() {')[1]
                  ?.split('void AppController::QueueTabClose')[0];
 assert.ok(release);
 assert.doesNotMatch(release,/retired_ui_\.clear\(\)/);
});

test('session autosave only writes when tab order, active id or URL changes',()=>{
 assert.match(c,/if \(snapshot == saved_session_snapshot_\) return;/);
 assert.match(c,/saved_session_snapshot_ = snapshot;/);
 assert.match(c,/if \(good && CommitStateFile\(temp, path\)\)/);
});

test('switching existing visible tabs does not detach or allocate CEF views',()=>{
 assert.match(c,/const bool already_displayed = target->ui_panel/);
 assert.match(c,/if \(!already_displayed\) RebuildTabStrip\(\)/);
 assert.match(c,/tabs_\.erase\(it\);\s*\/\/ A structural deletion must reconcile/);
});
