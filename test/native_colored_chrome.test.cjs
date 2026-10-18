'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const src=path.resolve(__dirname,'../src');
const code=fs.readFileSync(path.join(src,'controller.cc'),'utf8');
const header=fs.readFileSync(path.join(src,'controller.h'),'utf8');
const palette=fs.readFileSync(path.join(src,'ui_chrome_palette.h'),'utf8');
const lum=c=>[0,2,4].map(i=>parseInt(c.slice(i,i+2),16)/255)
  .map(v=>v<=.04045?v/12.92:Math.pow((v+.055)/1.055,2.4))
  .reduce((a,v,i)=>a+v*[.2126,.7152,.0722][i],0);
const ratio=(a,b)=>(Math.max(lum(a),lum(b))+.05)/(Math.min(lum(a),lum(b))+.05);
const swatches=name=>[...palette.split('inline constexpr TabHue k'+name+'TabHues[8]{')[1]
  .split('};')[0].matchAll(/\{0xFF([0-9A-F]{6})u,0xFF([0-9A-F]{6})u,0xFF([0-9A-F]{6})u,0xFF([0-9A-F]{6})u\}/g)].map(a=>a.slice(1));
test('each native tab has a distinct repeatable color identity for both themes',()=>{
  for(const theme of ['Light','Dark']){
    const hues=swatches(theme);
    assert.equal(hues.length,8);
    assert.equal(new Set(hues.map(h=>h[3])).size,8,'badge identity');
    assert.equal(new Set(hues.map(h=>h[1])).size,8,'active surface identity');
    for(const colors of hues){
      assert.notEqual(colors[0],colors[1]);
      assert.notEqual(colors[1],colors[2]);
      const text=theme==='Light'?'1B2940':'EEF5FF';
      assert.ok(ratio(text,colors[1])>=4.5,theme+' active tab readability');
      assert.ok(ratio(text,colors[0])>=4.5,theme+' inactive tab readability');
      const glyph=theme==='Light'?'FFFFFF':'101B31';
      assert.ok(ratio(glyph,colors[3])>=4.5,theme+' badge glyph');
    }
  }
  assert.match(palette,/\(\(tab_id - 1\) % 8 \+ 8\) % 8/);
});
test('CEF native circular icons are immutable 1x and 2x bitmap badges with accessible glyphs',()=>{
  assert.match(code,/NativeControlIcon\(int id, bool hot\)/);
  assert.match(code,/NativeControlSlot\(int id\)/);
  assert.match(code,/std::uint8_t symbols\[15\]\[5\]/);
  assert.match(code,/CefImage::CreateImage\(\)/);
  assert.match(code,/for \(int scale : \{1, 2\}\)/);
  assert.match(code,/CEF_COLOR_TYPE_RGBA_8888,CEF_ALPHA_TYPE_POSTMULTIPLIED/);
  assert.match(code,/label->SetImage\(CEF_BUTTON_STATE_NORMAL, pict\)/);
  assert.match(code,/chrome_badges_\[theme\]\[hot \? 1 : 0\]\[slot\]/);
  assert.match(code,/if \(g_native_colors_preview\)/);
  assert.doesNotMatch(code,/--no-sandbox|--disable-web-security|DeleteAllCookies/);
  assert.equal((code.match(/CreateBrowserView\(/g)||[]).length,1);
});
test('normal Chrome and existing sessions remain unchanged unless preview is explicitly selected',()=>{
  assert.match(code,/HasSwitch\("rc-colorful-chrome-preview"\)/);
  assert.match(code,/if \(g_native_colors_preview\) \{/);
  assert.match(code,/command_line->HasSwitch\("rc-home-start"\)/);
  assert.match(code,/\? rc_home::kUrl : "https:\/\/chatgpt\.com\/"/);
  assert.match(code,/if \(IsChatGPTURL\(url\)\) payload << url/);
  assert.match(code,/NativeHeaderColor\(\)/);
  assert.match(code,/IsFrameless\(CefRefPtr<CefWindow> window\) override \{ return true; \}/);
  assert.match(code,/SetDraggableRegions/);
  assert.match(code,/SetAccessibleName\("Close application"\)/);
  assert.match(header,/std::array<std::array<std::array<CefRefPtr<CefImage>, 16>, 2>, 2> chrome_badges_/);
});

test('compact colored rail has genuinely circular selected tab with visible focus ring',()=>{
 assert.match(code,/const bool compact_rail = VerticalTabsVisible\(\) && SidebarCompactEffective\(\)/);
 assert.match(code,/bg = compact_rail \? UiSidebarPalette\(\)\.background/);
 assert.match(code,/const bool outlined = g_native_colors_preview && active/);
 assert.match(code,/const float outline_alpha = outlined/);
 assert.match(code,/std::max\(body_alpha, outline_alpha\)/);
 assert.match(code,/const bool is_outline = outline_alpha > body_alpha/);
 assert.match(code,/CEF_ALPHA_TYPE_POSTMULTIPLIED/);
 assert.match(code,/channel\(color,16\)\*opacity/);
 assert.match(code,/channel\(color,8\)\*opacity/);
 assert.match(code,/channel\(color,0\)\*opacity/);
 assert.match(code,/g_ui_theme == rc_ui::ThemeMode::Dark \? 0xFFD1E1F7u : 0xFF344D74u/);
});
