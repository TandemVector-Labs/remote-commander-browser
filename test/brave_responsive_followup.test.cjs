'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const {join}=require('node:path');
const src=fs.readFileSync(join(__dirname,'../src/controller.cc'),'utf8');
const header=fs.readFileSync(join(__dirname,'../src/controller.h'),'utf8');
test('compact and narrow chrome transition on width thresholds independently of tab capacity',()=>{
 assert.match(src,/compact_toolbar = logical_width < 930/);
 assert.match(src,/narrow_toolbar = logical_width < 690/);
 assert.match(src,/\(new_bounds\.width < 930\) != compact_toolbar_cached_/);
 assert.match(src,/\(new_bounds\.width < 690\) != narrow_toolbar_cached_/);
 assert.match(header,/bool compact_toolbar_cached_ = false/);
 assert.match(header,/bool narrow_toolbar_cached_ = false/);
});
test('narrow mode preserves direct tab switching and keyboard-only actions',()=>{
 assert.match(src,/button->SetVisible\(!vertical && !compact_toolbar && tabs_\.size\(\) > 1\)/);
 assert.match(src,/id == kCompanionButton\)\s*button->SetVisible\(!compact_toolbar \|\| fullscreen_\)/);
 assert.match(src,/id == kTabsOrientationButton\)\s*button->SetVisible\(!vertical\)/);
 assert.match(src,/SetAccelerator\(kAccelCompanion, kVKeyC/);
 assert.match(src,/SetAccelerator\(kAccelOrientation, kVKeyE/);
 assert.match(src,/SetAccelerator\(kAccelCloseTab, kVKeyW/);
});
test('hover does not erase selection highlight or modify site/auth behavior',()=>{
 assert.match(src,/bg = active \? UiPalette\(\)\.tabActive : \(hot \? UiPalette\(\)\.tabHover/);
 assert.match(src,/bg = active \? UiSidebarPalette\(\)\.active : \(hot \? UiSidebarPalette\(\)\.hover/);
 for(const unsafe of ['--no-sandbox','--disable-web-security','DeleteAllCookies','clearBrowsingData'])
   assert.doesNotMatch(src,new RegExp(unsafe));
});
