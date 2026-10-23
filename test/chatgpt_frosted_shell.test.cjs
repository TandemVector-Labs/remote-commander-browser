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
 assert.match(palette,/Opaque CEF Views surfaces are intentional/);
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
test('native workspace is clearly distinguished from page-owned ChatGPT chat',()=>{
 assert.match(cc,/SetTitle\("ChatGPT Commander"\)/);
 assert.match(cc,/native workspace/);
 assert.match(cc,/◈  RC Browser/);
 assert.match(cc,/if \(ec == std::errc::no_such_file_or_directory\) return true/);
 assert.match(cc,/New tab/);
 assert.match(cc,/Open Browser tab/);
 assert.match(cc,/sidebar_new_tab_button_->SetText\(compact \? "\+" : "＋  New tab"\)/);
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

test('native X11 horizontal tab-drag fixture opts into horizontal layout explicitly',()=>{
 const drag=fs.readFileSync(path.join(__dirname,'native-tab-drag-x11.sh'),'utf8');
 assert.match(drag,/printf 'horizontal\\n'/);
 assert.match(drag,/ui-tabs-orientation\.state/);
 assert.match(drag,/chmod 600/);
 assert.match(drag,/R247_NATIVE_DRAG_PASS/);
 assert.doesNotMatch(drag,/--no-sandbox/);
});

test('ChatGPT quick actions are visible, compact and paint with palette contrast',()=>{
 assert.match(cc,/if \(id == kNewTabButton\)\s*return CefSize\(owner_ && owner_->compact_toolbar_cached_ \? 40 : 116, 36\)/);
 assert.match(cc,/auto add = make_button\(kNewTabButton, "＋ New tab", UiPalette\(\)\.softAccent\)/);
 assert.match(cc,/button->AsLabelButton\(\)->SetText\(compact_toolbar \? "＋" : "＋ New tab"\)/);
 assert.match(cc,/sidebar_new->SetBackgroundColor\(UiPalette\(\)\.softAccent\)/);
 assert.match(cc,/sidebar_new_tab_button_->SetBackgroundColor\(UiPalette\(\)\.softAccent\)/);
 assert.match(cc,/id == kSidebarNewTabButton\) \{[\s\S]*?bg = hot \? UiPalette\(\)\.tabHover : UiPalette\(\)\.softAccent/);
 assert.match(cc,/const cef_color_t fg = g_native_colors_preview \? UiPalette\(\)\.content/);
 for(const theme of ['Light','Dark']){
  const p=colors(theme);
  assert.ok(contrast(p[11],p[6])>=4.5,theme+' New Chat label');
  assert.ok(contrast(p[15],p[7])>=4.5,theme+' active badge numeral');
 }
 // No extra BrowserView, default web navigation, or account/session access.
 assert.equal((cc.match(/CreateBrowserView\(/g)||[]).length,1);
 assert.doesNotMatch(cc,/ExecuteJavaScript|DeleteAllCookies|--no-sandbox/);
});

test('narrow ChatGPT brand remains identifiable without stealing full title space',()=>{
 assert.match(cc,/if \(id == kBrandButton\)\s*return CefSize\(owner_ && owner_->narrow_toolbar_cached_ \? 44 : kBrandWidth, 36\)/);
 assert.match(cc,/id == kBrandButton\) \{[\s\S]*?SetText\(narrow_toolbar \? "◈" : "◈  RC Browser"\);\s*button->SetVisible\(true\)/);
 assert.match(cc,/SetAccessibleName\("Remote Commander Browser native workspace"\)/);
 assert.match(cc,/new_bounds\.width < 690/);
 assert.equal((cc.match(/CreateBrowserView\(/g)||[]).length,1);
});

test('native R247 drag checks a source-title drag handshake before moving to destination',()=>{
 const drag=fs.readFileSync(path.join(__dirname,'native-tab-drag-x11.sh'),'utf8');
 assert.match(drag,/xdotool mousemove --sync "\$\(\(sx \+ 8\)\)" "\$sy"/);
 assert.match(drag,/CGWA_NATIVE_DRAG_DIAG stage=press_sample ok=1 held=1 hit=1/);
 assert.match(drag,/R247_NATIVE_PRESS_HANDSHAKE_FAILED/);
 assert.match(drag,/CGWA_NATIVE_TAB_DRAG_REORDER/);
});
