'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const src=fs.readFileSync(path.join(__dirname,'../src/controller.cc'),'utf8');
const header=fs.readFileSync(path.join(__dirname,'../src/controller.h'),'utf8');
test('vertical toggle is a native CEF control, not page script',()=>{
 assert.match(src,/kTabsOrientationButton/);
 assert.match(src,/if \(id == kTabsOrientationButton\) \{ ToggleTabsOrientation\(\); return; \}/);
 assert.match(src,/SetAccessibleName\([\s\S]*?Use vertical tabs/);
 assert.match(src,/vertical_tab_strip_->AddChildView\(item\)/);
 assert.match(src,/tab_strip_->AddChildView\(item\)/);
 assert.doesNotMatch(src,/--no-sandbox|--disable-web-security|DeleteAllCookies/);
});
test('native sidebar reserves nonoverlapping BrowserView and companion bounds',()=>{
 assert.match(src,/const int left = show_sidebar \? kSidebarWidth : 0;/);
 assert.match(src,/CefRect bounds\(left, top, browser_width, height\)/);
 assert.match(src,/CefRect\(left \+ browser_width, top, kCompanionWidth, height\)/);
 assert.match(src,/sidebar_overlay_->SetVisible\(show_sidebar\)/);
});
test('preference is private, persistent and symlink fail-closed',()=>{
 assert.match(src,/ui-tabs-orientation\.state/);
 assert.match(src,/std::filesystem::is_symlink\(status\)/);
 assert.match(src,/SaveVerticalTabsPreference\(next\)/);
 assert.match(src,/CommitStateFile\(temp, path\)/);
 assert.match(header,/bool vertical_tabs_ = false;/);
});
test('session identity and tab order remain unchanged while toggling',()=>{
 const fn=src.split('void AppController::ToggleTabsOrientation() {')[1]?.split('void AppController::RunVerticalTabsSelfTest()')[0];
 assert.ok(fn);
 assert.doesNotMatch(fn,/NewTab\(|CloseTab\(|SaveSessionState\(|CreateBrowserView\(|DeleteAllCookies/);
 assert.match(src,/CGWA_VERTICAL_TABS_SELFTEST/);
 assert.match(src,/tab->overlay->GetBounds\(\).x == kSidebarWidth/);
});
