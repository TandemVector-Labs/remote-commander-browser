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

test('R247 native drag is scoped to title CEF state and live OS mouse, never page injection',()=>{
 assert.match(controller,/ReadNativeDragPointer/);
 assert.match(controller,/GetAsyncKeyState\(VK_LBUTTON\)/);
 assert.match(controller,/XQueryPointer\(/);
 assert.match(controller,/state == CEF_BUTTON_STATE_PRESSED\) BeginTabDrag\(button\)/);
 assert.match(controller,/CefDisplay::ConvertScreenPointFromPixels/);
 assert.match(controller,/TabUnderPointer\(pointer\)/);
 assert.match(controller,/rc_ui::MoveTabTo\(tabs_, source_id, target_id\)/);
 assert.match(controller,/SetActiveTabInternal\(source_id\)/);
 assert.match(controller,/tab_drag_\.Cancel\(\)/);
 assert.doesNotMatch(controller,/DeleteAllCookies|clearBrowsingData|--disable-web-security|--no-sandbox/);
});

test('R249 native drag guards compare the actual CEF View identity, not C++ wrapper pointers',()=>{
 assert.match(controller,/title_button->IsSame\(button\)/);
 assert.match(controller,/if \(!tab \|\| tab->closing \|\| !same_title_view\) return;/);
 assert.doesNotMatch(controller,/title_button != button/);
 assert.match(controller,/CGWA_NATIVE_DRAG_DIAG stage=source/);
});


test('R105 native stdout-fatal source gate remains fail-closed across line endings',()=>{
 const script=read('scripts/runtime-self-test.sh').replace(/\r\n/g,'\n');
 const begin='# R105_OUTPUT_FATAL_BEGIN\n',end='# R105_OUTPUT_FATAL_END\n';
 const from=script.indexOf(begin),to=script.indexOf(end);
 assert.ok(from>=0&&to>from,'native stdout must have a fail-closed fatal gate');
 const after=script.indexOf('[[ "$rc" -eq 0 ]]');
 const before=script.indexOf('LOG="$QA/chrome_debug.log"');
 assert.ok(after>=0&&before>after&&from>after&&to<before,
   'fatal stdout gate must be after process rc check, before normal PASS log checks');
 const body=script.slice(from+begin.length,to);
 assert.match(body,/grep -Eq 'FATAL\|SIGABRT\|SIGSEGV/);
 assert.match(body,/exit 30/);
 assert.match(body,/exit 31/);
 assert.match(body,/captured output scan error/);
});
test('R105 Linux native child fatal Bash fixtures reject zero-parent-exit diagnostics',
 {skip:process.platform==='win32'?'Bash runtime fixture exercised by Linux CI; Windows source guard tested separately':false},()=>{
 const script=read('scripts/runtime-self-test.sh').replace(/\r\n/g,'\n');
 const begin='# R105_OUTPUT_FATAL_BEGIN\n',end='# R105_OUTPUT_FATAL_END\n';
 const from=script.indexOf(begin),to=script.indexOf(end);
 assert.ok(from>=0&&to>from,'runtime Bash gate missing');
 const {tmpdir}=require('node:os');
 const {spawnSync}=require('node:child_process');
 const dir=fs.mkdtempSync(path.join(tmpdir(),'cef-r105-native-fatal-'));
 const gate=script.slice(from+begin.length,to);
 const runner='set -euo pipefail\n'+gate+'\nprintf "OUTPUT_GATE_PASS\\n"\n';
 const lines=[
  '*** stack smashing detected ***: terminated',
  'child received SIGSEGV',
  'segfault in a child process',
  'stack buffer overflow',
  'AddressSanitizer: detected error',
  'ThreadSanitizer: detected race',
  "GPU process isn't usable. Goodbye.",
  'FATAL: CEF child abort',
  'SIGABRT in a utility child'
 ];
 const fixture=path.join(dir,'runtime-selftest.out');
 const run=(env={})=>spawnSync('bash',['-c',runner],
   {env:{...process.env,QA:dir,...env},encoding:'utf8',timeout:5000});
 try {
  for(const line of lines){
   fs.writeFileSync(fixture,'mock private marker DO_NOT_DISCLOSE\n'+line+'\n',{mode:0o600});
   const res=run();
   assert.equal(res.error,undefined,line);
   assert.equal(res.status,30,'fatal line was not blocked: '+line);
   assert.match(res.stderr,/RUNTIME_SELFTEST=FAIL/);
   assert.doesNotMatch(res.stderr,/DO_NOT_DISCLOSE|mock private marker/,'do not echo captured raw child output');
  }
  fs.writeFileSync(fixture,'CGWA_SELFTEST PASS\nCGWA_SHUTDOWN WINDOW_DESTROYED\n');
  const clean=run();
  assert.equal(clean.error,undefined);
  assert.equal(clean.status,0,clean.stderr);
  assert.match(clean.stdout,/OUTPUT_GATE_PASS/);
  fs.rmSync(fixture);
  const absent=run();
  assert.equal(absent.status,31,'missing captured output must fail closed');
  assert.match(absent.stderr,/RUNTIME_SELFTEST=FAIL/);
  fs.writeFileSync(fixture,'CGWA_SELFTEST PASS\n');
  const shim=path.join(dir,'grep');
  fs.writeFileSync(shim,'#!/bin/sh\nexit 2\n',{mode:0o700});
  const scanError=run({PATH:dir+path.delimiter+process.env.PATH});
  assert.equal(scanError.status,31,'grep I/O failure must fail closed');
  assert.match(scanError.stderr,/captured output scan error/);
 } finally {fs.rmSync(dir,{recursive:true,force:true});}
});
