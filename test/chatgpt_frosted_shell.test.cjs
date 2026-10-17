'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const dir=path.resolve(__dirname,'../src');
const cc=fs.readFileSync(path.join(dir,'controller.cc'),'utf8');
const palette=fs.readFileSync(path.join(dir,'ui_chrome_palette.h'),'utf8');
const main=fs.readFileSync(path.join(dir,'main.cc'),'utf8');
const colors=k=>[...palette.split('inline constexpr ThemePalette k'+k+'Palette{')[1]
 .split('};')[0].matchAll(/0xFF([A-F0-9]{6})u/g)].map(m=>m[1]);
const relativeLuminance=color=>[0,2,4].map(i=>parseInt(color.slice(i,i+2),16)/255)
 .map(v=>v<=0.04045?v/12.92:Math.pow((v+0.055)/1.055,2.4))
 .reduce((v,n,i)=>v+n*[0.2126,0.7152,0.0722][i],0);
const contrast=(a,b)=>(Math.max(relativeLuminance(a),relativeLuminance(b))+.05)/
 (Math.min(relativeLuminance(a),relativeLuminance(b))+.05);

test('glass effect uses opaque native surfaces, no blur, shadow compositors or new web views',()=>{
 assert.match(palette,/deliberately|one opaque native paint surface/);
 assert.doesNotMatch(cc,/BackdropFilter|BlurView|GaussianBlur|SetOpacity\(|windowless_rendering_enabled|CSSFilter/);
 assert.doesNotMatch(palette,/0x(?:0[0-9A-F]{6}|[1-9A-E][0-9A-F]{6})u/);
 assert.equal((cc.match(/CreateBrowserView\(/g)||[]).length,1);
 assert.match(cc,/SetInkDropEnabled\(false\)/);
 assert.doesNotMatch(cc,/SetInkDropEnabled\(true\)/);
 assert.match(main,/persist_session_cookies\s*=\s*1/);
});
test('new profiles default to navy, persisted light/dark preferences remain respected',()=>{
 assert.match(cc,/ThemeMode g_ui_theme = rc_ui::ThemeMode::Dark/);
 assert.match(cc,/value == "light"\) return rc_ui::ThemeMode::Light/);
 assert.match(cc,/value == "dark"\) return rc_ui::ThemeMode::Dark/);
 assert.match(cc,/return rc_ui::ThemeMode::Dark;[\s\S]*?\}/);
 assert.match(cc,/SaveThemePreference/);
});
test('native workspace labels are ChatGPT-specific, not a generic URL browser facade',()=>{
 assert.match(cc,/SetTitle\("ChatGPT Commander"\)/);
 assert.match(cc,/desktop workspace/);
 assert.match(cc,/◆  ChatGPT/);
 assert.match(cc,/if \(ec == std::errc::no_such_file_or_directory\) return true/);
 assert.match(cc,/New Chat/);
 assert.match(cc,/Open ChatGPT in a new workspace tab/);
 assert.match(cc,/sidebar_new_tab_button_->SetText\(compact \? "\+" : "\+   New Chat"\)/);
 assert.match(cc,/https:\/\/chatgpt\.com\//);
 assert.doesNotMatch(cc,/DeleteAllCookies|clearBrowsingData|--no-sandbox|--disable-web-security/);
});
test('accessible CTA remains legible in daylight/navy and dark mode retains contrast',()=>{
 for(const theme of ['Light','Dark']) {
  const c=colors(theme);
  assert.equal(c.length,16);
  assert.ok(contrast(c[9],c[0])>=7,theme+' header text');
  assert.ok(contrast(c[10],c[1])>=4.5,theme+' muted tab');
  assert.ok(contrast(c[11],c[3])>=4.5,theme+' selected text');
  assert.ok(contrast(c[15],c[7])>=4.5,theme+' Commander primary');
  assert.ok(contrast(c[15],c[8])>=4.5,theme+' Commander hover');
 }
 assert.match(cc,/compact_toolbar_cached_ \? 40 : 112/);
 assert.match(cc,/id == kSidebarCompanionButton/);
});
