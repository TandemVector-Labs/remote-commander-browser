'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const s=fs.readFileSync(path.join(__dirname,'../src/controller.cc'),'utf8');
const h=fs.readFileSync(path.join(__dirname,'../src/controller.h'),'utf8');
test('real circular alpha badges are rasterized natively, without a web context',()=>{
 assert.match(s,/CefImage::CreateImage\(\)/);
 assert.match(s,/std::sqrt\(dx\*dx \+ dy\*dy\)/);
 assert.match(s,/CEF_COLOR_TYPE_RGBA_8888/);
 assert.match(s,/CEF_ALPHA_TYPE_POSTMULTIPLIED/);
 assert.match(s,/RoundedTabBadge\(tab.id, active\)/);
 assert.match(h,/tab_badges_/);
 assert.doesNotMatch(s,/CreateBrowserView\(.*[Rr]ound.*[Bb]adge/);
});
test('badges are cached per theme, active state and tab position',()=>{
 assert.match(s,/tab_badges_\[theme_slot\]\[mode\]\[ordinal - 1\]/);
 assert.match(s,/if \(stored\) return stored/);
 assert.match(s,/if \(badge && \(!shown \|\| !shown->IsSame\(badge\)\)\)/);
 assert.match(s,/GetText\(\).ToString\(\) != text/);
});
