'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.join(__dirname,'../src');
const cc=fs.readFileSync(path.join(root,'controller.cc'),'utf8');
const h=fs.readFileSync(path.join(root,'controller.h'),'utf8');
const colors=fs.readFileSync(path.join(root,'ui_chrome_palette.h'),'utf8');
const fn=(start,next)=>cc.split(start)[1]?.split(next)[0]||'';
const palette=name=>{
 const match=colors.match(new RegExp('k'+name+'Palette\\s*\\{([^}]*)\\}'));
 assert.ok(match,'missing palette '+name);
 return [...match[1].matchAll(/0xFF([0-9A-F]{6})u/g)].map(m=>m[1]);
};
const lum=color=>[0,2,4].map(i=>parseInt(color.slice(i,i+2),16)/255)
 .map(v=>v<=.04045?v/12.92:((v+.055)/1.055)**2.4)
 .reduce((sum,v,i)=>sum+v*[.2126,.7152,.0722][i],0);
const contrast=(a,b)=>(Math.max(lum(a),lum(b))+.05)/(Math.min(lum(a),lum(b))+.05);

test('light chrome is sky blue and dark chrome is navy, with accessible text',()=>{
 const light=palette('Light'),dark=palette('Dark');
 assert.equal(light.length,16);
 assert.equal(dark.length,16);
 assert.equal(light[0],'EAF5FF');
 assert.equal(dark[0],'111E32');
 assert.equal(dark[15],'0C1526');
 for(const [mode,v] of [['Light',light],['Dark',dark]]){
  assert.ok(contrast(v[9],v[0])>=7,mode+' toolbar text');
  assert.ok(contrast(v[10],v[1])>=4.5,mode+' muted tab text');
  assert.ok(contrast(v[11],v[3])>=4.5,mode+' selected tab text');
 }
 assert.doesNotMatch(cc,/DeleteAllCookies|clearBrowsingData|--no-sandbox|--disable-web-security/);
});

test('fast active tab change updates visible numerator without rebuilding the tab strip',()=>{
 const fast=fn('void AppController::SetActiveTabInternal(int tab_id) {','bool AppController::VerticalTabsVisible() const {');
 assert.ok(fast);
 assert.match(fast,/active_tab_id_ = tab_id;\s*\/\/[^\r\n]*[\r\n]+\s*\/\/[^\r\n]*[\r\n]+\s*UpdateTabSwitcherLabel\(\)/);
 assert.match(fast,/if \(!already_displayed\) RebuildTabStrip\(\)/);
 const update=fn('void AppController::UpdateTabSwitcherLabel() {','void AppController::UpdateTabButton(Tab& tab) {');
 assert.match(update,/tab_switcher_button_->SetText\(label\)/);
 assert.match(update,/tabs_\.size\(\)/);
 assert.match(update,/active_tab_id_/);
 assert.match(update,/SetAccessibleName/);
 assert.match(fn('void AppController::RebuildTabStrip() {','void AppController::UpdateTabSwitcherLabel() {'),/UpdateTabSwitcherLabel\(\)/);
});

test('no unpainted black gap: tab strip, spacer and full height Commander use theme delegates',()=>{
 assert.match(cc,/tab_strip_ = CefPanel::CreatePanel\(header_surface_delegate_\)/);
 assert.match(cc,/header_spacer_ = CefPanel::CreatePanel\(header_surface_delegate_\)/);
 assert.match(cc,/companion_panel_ = CefPanel::CreatePanel\(header_surface_delegate_\)/);
 assert.match(cc,/companion_fill_ = CefPanel::CreatePanel\(header_surface_delegate_\)/);
 assert.match(cc,/companion_layout_->SetFlexForView\(companion_fill_, 1\)/);
 assert.match(cc,/class AppController::HeaderSurfaceDelegate[\s\S]*?OnThemeChanged/);
 assert.match(cc,/if \(companion_fill_\) companion_fill_->SetBackgroundColor\(UiPalette\(\).header\)/);
});

test('orientation is at the LEFT in horizontal and vertical native layouts even in fullscreen',()=>{
 const build=fn('void AppController::BuildHeaderControls() {','void AppController::UpdateDraggableRegions() {');
 assert.ok(build.indexOf('header_->AddChildView(orientation)') < build.indexOf('header_->AddChildView(tab_strip_)'));
 assert.match(cc,/sidebar_orientation->SetID\(kSidebarOrientationButton\)/);
 assert.match(cc,/sidebar_->AddChildView\(sidebar_orientation\)/);
 assert.match(cc,/id == kSidebarOrientationButton\) \{ ToggleTabsOrientation\(\); return; \}/);
 assert.match(cc,/button->SetVisible\(!vertical\); \/\/ Vertical rail owns the alternate action/);
 assert.match(fn('void AppController::ToggleTabsOrientation() {','void AppController::RunVerticalTabsSelfTest() {'),/if \(fullscreen_ && header_\) header_->SetVisible\(!vertical_tabs_\)/);
});

test('Commander is right-anchored and mouse-accessible in vertical fullscreen',()=>{
 const layout=fn('void AppController::LayoutTabOverlays() {','void AppController::SaveSessionState() {');
 assert.match(layout,/const bool show_companion = companion_visible_ &&\s*width - left >= kCompanionWidth \+ 320/);
 assert.match(layout,/CefRect\(left \+ browser_width, top, kCompanionWidth, height\)/);
 assert.doesNotMatch(layout,/companion_visible_ && !fullscreen_/);
 assert.match(cc,/sidebar_companion->SetID\(kSidebarCompanionButton\)/);
 assert.match(cc,/id == kSidebarCompanionButton\) \{ ToggleCompanionPanel\(\); return; \}/);
 assert.match(cc,/SetAccelerator\(kAccelCompanion, kVKeyC/);
});

test('Esc exits fullscreen, F11 stays reversible and close/max controls form one contiguous group',()=>{
 const accel=fn('bool AppController::OnAccelerator(int command_id) {','void AppController::OnButtonStateChanged(');
 assert.match(accel,/if \(command_id == kAccelExitFullscreen\)/);
 assert.match(accel,/if \(!window_ \|\| !window_->IsFullscreen\(\)\) return false;/);
 assert.match(accel,/ToggleFullscreen\(\);\s*return true;/);
 assert.match(cc,/SetAccelerator\(kAccelExitFullscreen, kVKeyEscape, false, false, false, true\)/);
 assert.match(cc,/SetAccelerator\(kAccelFullscreen, kVKeyF11/);
 assert.match(cc,/controls.between_child_spacing = 0/);
 for(const id of ['full','max','min','close'])
  assert.match(cc,new RegExp('window_controls_->AddChildView\\('+id+'\\)'));
 assert.match(cc,/header_->AddChildView\(window_controls_\)/);
 assert.match(cc,/button->GetParentView\(\)->IsSame\(window_controls_\)/);
 assert.match(h,/CefRefPtr<CefPanel> window_controls_/);
});

test('Commander monitor explicitly separates local Core health from unproven chat control',()=>{
 const monitor=fs.readFileSync(path.join(root,'commander_companion.cc'),'utf8');
 const tick=fn('void AppController::CompanionTick() {','void AppController::RunCompanionSelfTest() {');
 assert.match(monitor,/CONNECTED \(monitor only\)/);
 assert.match(monitor,/Chat binding — فقط مشاهده/);
 assert.match(monitor,/GUI control: /);
 assert.match(tick,/companion_observation_ = companion::ReadObservation\(root\)/);
 assert.match(tick,/companion_binding_ = companion::ReadNativeBinding\(root\)/);
 assert.match(tick,/companion_binding_poll_ >= 3/);
 assert.match(cc,/rows\[i\]\.text\.find\("UNCERTAIN"\)/);
 assert.match(cc,/companion::ReadMonitor\(root\)/);
 assert.doesNotMatch(tick,/GetCookies|ExecuteJavaScript|SendProcessMessage|StartWorkflow/);
});


test('native self-test changes the active tab counter without theme or chrome rebuild',()=>{
 const run=fn('void AppController::RunLiveTabCounterSelfTest() {','void AppController::RunFullscreenTabsSelfTest() {');
 assert.match(run,/SetActiveTabInternal\(first\)/);
 assert.match(run,/SetActiveTabInternal\(last\)/);
 assert.match(run,/CGWA_LIVE_TAB_COUNTER_SELFTEST/);
 assert.match(run,/first_ok && last_ok && restore_ok && final_ok/);
 assert.match(cc,/HasSwitch\("live-tab-counter-self-test"\)/);
 assert.match(cc,/CGWA_LIVE_TAB_COUNTER_REQUIRES_BACKGROUND/);
 assert.doesNotMatch(run,/ToggleTheme\(/);
});
