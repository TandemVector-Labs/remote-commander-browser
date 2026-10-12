'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const read=n=>fs.readFileSync(path.join(root,n),'utf8');
const controller=read('src/controller.cc');
const header=read('src/controller.h');
const layout=read('src/ui_chrome_layout.h');
test('native chrome height is one shared constant for header and BrowserView overlays',()=>{
 assert.match(controller,/constexpr int kHeaderHeight = 52;/);
 assert.match(controller,/new FixedPanelDelegate\(1240, kHeaderHeight\)/);
 assert.match(controller,/header_visible \? kHeaderHeight : 0/);
 assert.match(controller,/fullscreen_ \? 0 : kHeaderHeight/);
});
test('all eight restored tab states remain in the model, only controls are bounded',()=>{
 assert.match(layout,/SelectTabs\(int logical_width, int count, int active\)/);
 assert.match(controller,/tabs_\.size\(\) >= 8/);
 assert.match(controller,/!in_strip[\s\S]*tab\.ui_panel = nullptr/);
 assert.match(controller,/tab_switcher_button_->SetText\(label\)/);
 assert.match(controller,/kAccelTab1 \+ i/);
 assert.match(controller,/if \(id == kTabSwitcherButton\) \{ CycleTab\(1\); return; \}/);
});
test('tab interactions and labels are accessible and remain distinct from draggable regions',()=>{
 assert.match(controller,/SetAccessibleName\("Switch tabs"\)/);
 assert.match(controller,/SetAccessibleName\("Activate tab "/);
 assert.match(controller,/SetAccessibleName\("Close tab "/);
 assert.match(controller,/UpdateDraggableRegions\(\)/);
 assert.match(controller,/regions\.emplace_back/);
});
test('companion panel is viewport bounded and never silently hides data as healthy',()=>{
 assert.match(layout,/CompanionRowSlots/);
 assert.match(controller,/const bool overflow = static_cast<int>\(rows\.size\(\)\) > slots;/);
 assert.match(controller,/more • enlarge window/);
 assert.match(controller,/SetTooltipText\("Additional local monitor fields/);
 assert.match(controller,/caution \? UiPalette\(\)\.dangerText : UiPalette\(\)\.text/);
});
test('UI-only patch does not disable sandbox or touch ChatGPT authentication',()=>{
 assert.doesNotMatch(controller,/--no-sandbox|--disable-web-security|DeleteAllCookies|clearBrowsingData/);
 assert.match(controller,/IsChatGPTURL\(url\)/);
 assert.match(controller,/SaveSessionState\(\)/);
 assert.match(header,/visible_tab_capacity_/);
});

test('closing only tab replaces it before destroy and keeps top-level window alive',()=>{
 const close=controller.split('void AppController::CloseTab(int tab_id) {')[1]?.split('void AppController::QueueTabClose(int tab_id) {')[0];
 assert.ok(close);
 const executableClose=close.replace(/\/\/[^\r\n]*/g,'');
 assert.doesNotMatch(executableClose,/RequestCloseWindow\(\)/);
 assert.match(close,/if \(tabs_\.size\(\) == 1\) \{[\s\S]*?NewTab\(\);[\s\S]*?tabs_\.size\(\) != 2/);
 assert.match(close,/it = std::find_if\(tabs_\.begin\(\), tabs_\.end\(\)/);
 assert.match(close,/CGWA_TAB_LAST_REPLACEMENT_FAILED/);
 assert.match(controller,/CGWA_LAST_TAB_SELFTEST.*PASS/);
 assert.match(header,/BackgroundLastTabSelfTestVerify/);
});
test('dedicated close-window button remains functional',()=>{
 assert.match(controller,/if \(id == kCloseWindowButton\) \{ RequestCloseWindow\(\); return; \}/);
 assert.match(controller,/bool AppController::CanWindowClose\(\)/);
 assert.match(controller,/void AppController::BeginShutdown\(\)/);
});

test('R114 tab-X defers callback-time native view destruction',()=>{
 const pressed=controller.split('void AppController::OnButtonPressed(CefRefPtr<CefButton> button) {')[1]?.split('void AppController::RebuildTabStrip()')[0];
 const queued=controller.split('void AppController::QueueTabClose(int tab_id) {')[1]?.split('void AppController::CloseActiveTab()')[0];
 assert.ok(pressed&&queued);
 assert.match(pressed,/id >= kTabCloseBase\) \{ QueueTabClose\(id - kTabCloseBase\); return; \}/);
 assert.doesNotMatch(pressed,/CloseTab\(id - kTabCloseBase\)/);
 assert.match(queued,/CefPostTask\(/);
 assert.match(queued,/base::BindOnce\(&AppController::CloseTab/);
 assert.match(controller,/CGWA_UI_TAB_CLOSE_SELFTEST/);
 assert.match(header,/BackgroundUiTabCloseSelfTestVerify/);
});
