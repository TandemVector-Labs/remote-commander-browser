"use strict";
const {test}=require("node:test"),assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path");
const s=fs.readFileSync(path.join(__dirname,"../src/controller.cc"),"utf8");
const h=fs.readFileSync(path.join(__dirname,"../src/controller.h"),"utf8");
test("preview only has a usable native popup for secondary controls and direct actions remain",()=>{
 assert.match(s,/id == kChromeMoreButton && g_native_colors_preview/);
 assert.match(s,/CefMenuModel::CreateMenuModel\(chrome_menu_delegate_\)/);
 assert.match(s,/CEF_MENU_COLOR_BACKGROUND, menu_bg/);
 assert.match(s,/CEF_MENU_COLOR_BACKGROUND_HOVERED, menu_hover/);
 assert.match(s,/CEF_MENU_COLOR_TEXT, menu_text/);
 const menuFn=s.split("void AppController::ShowChromeMenu(")[1].split("void AppController::OnPolishMenuCommand(")[0];
 assert.doesNotMatch(menuFn,/model->AddSeparator\(\)/);
 assert.match(s,/dark_menu \? 0xFF203047u : 0xFFF8FAFDu/);
 assert.match(s,/window_->ShowMenu\(model,/);
 for(const label of ["Light appearance","Dark appearance","Vertical tab layout","Horizontal tab layout","Move active tab left","Move active tab right","Toggle Commander panel"])
  assert.ok(s.includes(label),label);
 for(const action of ["ToggleTheme()","ToggleTabsOrientation()","MoveActiveTab(-1)","MoveActiveTab(1)","ToggleCompanionPanel()"])
  assert.ok(s.includes(action),action);
 assert.match(s,/if \(chrome_menu_delegate_\) chrome_menu_delegate_->Detach\(\)/);
 assert.match(s,/button->SetVisible\(!g_native_colors_preview\)/);
 assert.match(s,/button->SetVisible\(!g_native_colors_preview && !vertical\)/);
 assert.match(s,/SetAccelerator\(kAccelOrientation,/);
 assert.match(s,/SetAccelerator\(kAccelMoveTabLeft,/);
 assert.match(s,/SetAccessibleName\("More browser actions"\)/);
});
test("hover uses its own cached circular color ring and real native tooltip",()=>{
 assert.match(s,/const int mode = active \? 1 : \(g_native_colors_preview && hovered \? 2 : 0\)/);
 assert.match(s,/hover_only \? 12\.1f : 12\.55f/);
 assert.match(s,/RoundedTabBadge\(tab_id, tab_id == active_tab_id_, hot\)/);
 assert.match(s,/label->SetImage\(CEF_BUTTON_STATE_NORMAL, badge\)/);
 assert.match(s,/title->SetTooltipText\(tab.title\)/);
 assert.match(s,/tab.title_button->SetAccessibleName/);
 assert.match(h,/> tab_badges_\{\}/);
});
test("first party and ChatGPT remain separate, normal start unchanged",()=>{
 assert.match(s,/HasSwitch\("rc-colorful-chrome-preview"\)/);
 assert.match(s,/\? rc_home::kUrl : "https:\/\/chatgpt.com\/"/);
 assert.match(s,/if \(g_native_colors_preview\)/);
 assert.equal((s.match(/CreateBrowserView\(/g)||[]).length,1);
 assert.doesNotMatch(s,/--disable-web-security|--no-sandbox|DeleteAllCookies/);
});

test("real native menu snapshot is gated behind opt-in preview and QA-only flag",()=>{
 assert.match(s,/HasSwitch\("rc-premium-menu-self-test"\) &&/);
 assert.match(s,/g_native_colors_preview && !background_test/);
 assert.match(s,/app->ShowChromeMenu\(app->chrome_more_button_\)/);
 assert.match(s,/CGWA_PREMIUM_MENU_SELFTEST SHOWN/);
});

test("user screenshot: no rectangular native color tiles or redundant control labels in horizontal preview",()=>{
 assert.match(s,/const bool seamless_horizontal = !VerticalTabsVisible\(\)/);
 assert.match(s,/bg = seamless_horizontal \? NativeHeaderColor\(\)/);
 assert.match(s,/visible.visible < static_cast<int>\(tabs_\.size\(\)\)/);
 assert.match(s,/id == kNewTabButton\)\s*button->AsLabelButton\(\)->SetText\(compact_toolbar \? "" : "New tab"\)/);
 assert.match(s,/id == kCompanionButton\)\s*button->AsLabelButton\(\)->SetText\(compact_toolbar \? "" : "Commander"\)/);
 assert.match(s,/if \(g_native_colors_preview && !vertical && tab_switcher_button_\)/);
 assert.match(s,/SetAccelerator\(kAccelNextTab,/);
 assert.match(s,/SetAccelerator\(kAccelPrevTab,/);
 assert.match(s,/SetAccessibleName\("Switch tabs"\)/);
});

test("dark horizontal tab gaps keep themed parent panel, including layout orientation changes",()=>{
 assert.match(s,/class AppController::TabItemSurfaceDelegate : public CefPanelDelegate/);
 assert.match(s,/const bool vertical = owner && owner->VerticalTabsVisible\(\)/);
 assert.match(s,/view->SetBackgroundColor\(vertical \? UiSidebarPalette\(\)\.background/);
 assert.match(s,/auto item = CefPanel::CreatePanel\(g_native_colors_preview/);
 assert.match(s,/\? tab_item_surface_delegate_ : nullptr\)/);
 assert.match(s,/tab.ui_panel->SetBackgroundColor\(vertical/);
 assert.match(s,/settings.between_child_spacing = vertical \? 5 : 3/);
});
