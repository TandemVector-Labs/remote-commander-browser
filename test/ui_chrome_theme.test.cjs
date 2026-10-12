'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const head=fs.readFileSync(path.join(root,'src/ui_chrome_palette.h'),'utf8');
const controller=fs.readFileSync(path.join(root,'src/controller.cc'),'utf8');
const layout=fs.readFileSync(path.join(root,'src/ui_chrome_layout.h'),'utf8');
const defs=(name)=>{
 const block=head.match(new RegExp('k'+name+'Palette\\s*\\{([\\s\\S]*?)\\};'));
 assert.ok(block,'palette definition '+name);
 const values=[...block[1].matchAll(/0xFF([0-9A-Fa-f]{6})u/g)].map(m=>m[1]);
 assert.equal(values.length,16);return values;
};
const light=defs('Light'),dark=defs('Dark');
const lum=h=>[0,2,4].map(i=>parseInt(h.slice(i,i+2),16)/255)
  .map(v=>v<=0.04045?v/12.92:Math.pow((v+0.055)/1.055,2.4))
  .reduce((a,v,i)=>a+v*[.2126,.7152,.0722][i],0);
const ratio=(a,b)=>(Math.max(lum(a),lum(b))+.05)/(Math.min(lum(a),lum(b))+.05);
test('R98 accessible Light/Dark contrast',()=>{
 assert.ok(ratio(light[9],light[0])>=7);
 assert.ok(ratio(dark[9],dark[0])>=7);
 assert.ok(ratio(light[10],light[1])>=4.5);
 assert.ok(ratio(dark[10],dark[1])>=4.5);
 assert.ok(ratio(light[11],light[6])>=4.5);
 assert.ok(ratio(dark[11],dark[6])>=4.5);
 assert.ok(ratio(dark[14],dark[13])>=4.5);
});
test('R98 theme toggle uses native CEF request context not website injection',()=>{
 assert.match(controller,/kThemeButton = 27;/);
 assert.match(controller,/if \(id == kThemeButton\) \{ ToggleTheme\(\); return; \}/);
 assert.match(controller,/SetChromeColorScheme/);
 assert.match(controller,/CEF_COLOR_VARIANT_DARK/);
 assert.match(controller,/CEF_COLOR_VARIANT_LIGHT/);
 assert.match(controller,/ThemeStatePath\(\)/);
 assert.match(controller,/SaveThemePreference\(next\)/);
 assert.match(controller,/CGWA_THEME_SELFTEST/);
 assert.doesNotMatch(controller,/DeleteAllCookies|clearBrowsingData|--disable-web-security|--no-sandbox/);
});
test('R99 first launch without ui-theme.state does not fail symlink privacy guard',()=>{
 assert.match(controller,/std::filesystem::symlink_status/);
 assert.match(controller,/ec == std::errc::no_such_file_or_directory/);
 assert.match(controller,/safe_file\(path, false\)/);
 assert.match(controller,/safe_file\(tmp, true\)/);
 assert.match(controller,/std::filesystem::is_symlink\(status\)/);
 assert.match(controller,/std::filesystem::is_regular_file\(status\)/);
 assert.doesNotMatch(controller,/is_symlink\(std::filesystem::u8path\(path\), error\)/);
});
test('R244 native theme and tab-order controls reserve space on narrow and wide windows',()=>{
 assert.match(layout,/chrome_reserved = width < 900 \? 464 : 700/);
 assert.match(controller,/SetAccessibleName\("Switch browser theme"\)/);
 assert.match(controller,/SetAccessibleName\("Move selected tab left"\)/);
 assert.match(controller,/SetAccessibleName\("Move selected tab right"\)/);
 assert.match(controller,/SetAccelerator\(kAccelMoveTabLeft, kVKeyPageUp, true, true, false, true\)/);
 assert.match(controller,/SetAccelerator\(kAccelMoveTabRight, kVKeyPageDown, true, true, false, true\)/);
});
