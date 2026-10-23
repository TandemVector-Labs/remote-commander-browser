#pragma once
#include "ui_tab_drag.h"

#include <array>
#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "client.h"
#include "commander_companion.h"
#include "native_chat_observer.h"
#include "include/cef_browser.h"
#include "include/cef_image.h"
#include "include/cef_menu_model.h"
#include "include/cef_menu_model_delegate.h"
#include "include/cef_client.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"


class AppController {
 public:
  AppController();
  ~AppController();

  static AppController* Get();

  void CreateMainWindow();
  void ActivateMainWindow();
  void NewTab(const std::string& url = "https://chatgpt.com/");
  void ActivateTab(int tab_id);
  void CloseTab(int tab_id);
  void QueueTabClose(int tab_id);
  void CloseActiveTab();
  void ToggleFullscreen();
  void ToggleTheme();
  void ToggleTabsOrientation();
  void ToggleSidebarCompact();
  void ApplyNativeTheme();
  void ToggleMaximize();
  void Minimize();
  void RequestCloseWindow();
  void CycleTab(int delta);
  void MoveActiveTab(int direction);
  void BeginTabDrag(CefRefPtr<CefButton> button);
  void PollTabDrag();
  int TabUnderPointer(const CefPoint& point) const;

  void OnWindowCreated(CefRefPtr<CefWindow> window);
  void OnWindowBoundsChanged(const CefRect& new_bounds);
  void OnWindowFullscreenTransition(bool is_completed);
  void OnWindowDestroyed();
  bool CanWindowClose();
  bool OnAccelerator(int command_id);
  void OnButtonPressed(CefRefPtr<CefButton> button);
  void OnButtonStateChanged(CefRefPtr<CefButton> button);
  void OnPolishMenuCommand(int command_id);

  void OnBrowserCreated(CefRefPtr<CefBrowser> browser);
  bool OnBrowserDoClose(CefRefPtr<CefBrowser> browser);
  void OnBrowserClosed(CefRefPtr<CefBrowser> browser);
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const std::string& title);
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading);
  void OnNativeAddressChange(CefRefPtr<CefBrowser> browser, const std::string& url);

  bool HandlePopupURL(const std::string& url);
  bool OpenExternal(const std::string& url);
  bool IsChatGPTURL(const std::string& url) const;
  bool IsInternalNavigationURL(const std::string& url) const;

  CefRefPtr<AppClient> client() const { return client_; }
  CefRefPtr<CefWindow> window() const { return window_; }
  bool closing() const { return closing_; }

 private:
  struct Tab {
    int id = 0;
    CefRefPtr<CefBrowserView> view;
    CefRefPtr<CefOverlayController> overlay;
    CefRefPtr<CefPanel> ui_panel;
    CefRefPtr<CefLabelButton> title_button;
    CefRefPtr<CefLabelButton> close_button;
    int browser_id = -1;
    std::string title = "ChatGPT";
    std::string url = "https://chatgpt.com/";
    bool loading = true;
    bool closing = false;
  };

  class WindowDelegateImpl;
  class ButtonDelegateImpl;
  class BrowserViewDelegateImpl;
  class FixedPanelDelegate;
  class SidebarSurfaceDelegate;
  class HeaderSurfaceDelegate;
  class TabItemSurfaceDelegate;
  class ChromeMenuDelegate;

  void BuildWindowUI();
  bool VerticalTabsVisible() const;
  int SidebarWidth() const;
  CefRefPtr<CefImage> RoundedTabBadge(int tab_id, bool active, bool hovered = false);
  CefRefPtr<CefImage> NativeControlIcon(int id, bool hot);
  bool SidebarCompactEffective() const;
  void RunVerticalTabsSelfTest();
  void RunFullscreenTabsSelfTest();
  void RunLiveTabCounterSelfTest();
  void BuildHeaderControls();
  void ShowChromeMenu(CefRefPtr<CefButton> anchor);
  void UpdateDraggableRegions();
  void RebuildTabStrip();
  void UpdateTabSwitcherLabel();
  void RetireTabUi(const Tab& tab);
  void ReleaseRetiredUi();
  void TabLifetimeSoakStep(int cycle);
  void UpdateTabButton(Tab& tab);
  void SetActiveTabInternal(int tab_id);
  void LayoutTabOverlays();
  void BuildCompanionPanel();
  void ToggleCompanionPanel();
  void RefreshCompanion();
  void UpdateCompanionPanel();
  void CompanionTick();
  void RunCompanionSelfTest();
  void SaveSessionState();
  Tab* FindTabById(int tab_id);
  Tab* FindTabByBrowser(CefRefPtr<CefBrowser> browser);
  void RunBackgroundSelfTest();
  void BackgroundSelfTestClose();
  void BackgroundSelfTestVerify();
  void BackgroundLastTabSelfTestVerify();
  void BackgroundUiTabCloseSelfTestVerify();
  void BeginShutdown();
  void FinalizeShutdown();
  std::string ShortTitle(const std::string& title) const;

  static AppController* instance_;

  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefPanel> header_;
  CefRefPtr<CefPanel> tab_strip_;
  CefRefPtr<CefPanel> vertical_tab_strip_;
  CefRefPtr<CefPanel> sidebar_;
  CefRefPtr<CefPanel> sidebar_fill_;
  CefRefPtr<CefBoxLayout> sidebar_layout_;
  CefRefPtr<SidebarSurfaceDelegate> sidebar_surface_delegate_;
  CefRefPtr<CefPanel> header_spacer_;
  CefRefPtr<CefPanel> window_controls_;
  CefRefPtr<CefOverlayController> sidebar_overlay_;
  CefRefPtr<CefPanel> content_;
  CefRefPtr<CefPanel> companion_panel_;
  CefRefPtr<CefPanel> companion_fill_;
  CefRefPtr<CefBoxLayout> companion_layout_;
  CefRefPtr<CefOverlayController> companion_overlay_;
  std::vector<CefRefPtr<CefLabelButton>> companion_rows_;
  std::vector<CefRefPtr<CefLabelButton>> header_buttons_;
  CefRefPtr<CefLabelButton> tab_switcher_button_;
  CefRefPtr<CefLabelButton> theme_button_;
  CefRefPtr<CefLabelButton> chrome_more_button_;
  CefRefPtr<ChromeMenuDelegate> chrome_menu_delegate_;
  CefRefPtr<CefLabelButton> orientation_button_;
  CefRefPtr<CefLabelButton> sidebar_toggle_button_;
  CefRefPtr<CefLabelButton> sidebar_new_tab_button_;
  CefRefPtr<CefLabelButton> sidebar_orientation_button_;
  CefRefPtr<CefLabelButton> sidebar_companion_button_;
  int visible_tab_capacity_ = -1;
  bool compact_toolbar_cached_ = false;
  bool narrow_toolbar_cached_ = false;
  companion::Monitor companion_monitor_;
  companion::Automation companion_automation_;
  companion::Observation companion_observation_;
  companion::NativeBinding companion_binding_;
  rc_browser::NativeChatObserver native_chat_observer_;
  CefRefPtr<CefBoxLayout> window_layout_;
  CefRefPtr<CefBoxLayout> header_layout_;
  CefRefPtr<CefBoxLayout> tab_layout_;

  CefRefPtr<WindowDelegateImpl> window_delegate_;
  CefRefPtr<ButtonDelegateImpl> button_delegate_;
  CefRefPtr<BrowserViewDelegateImpl> browser_view_delegate_;
  CefRefPtr<FixedPanelDelegate> header_delegate_;
  CefRefPtr<HeaderSurfaceDelegate> header_surface_delegate_;
  CefRefPtr<TabItemSurfaceDelegate> tab_item_surface_delegate_;

  CefRefPtr<AppClient> client_;
  struct RetiredUi {
    std::chrono::steady_clock::time_point created;
    CefRefPtr<CefPanel> panel;
    CefRefPtr<CefLabelButton> title;
    CefRefPtr<CefLabelButton> close;
  };
  // Native circular badge assets; lazily rasterized once per color-state.
  std::array<std::array<std::array<std::array<std::array<CefRefPtr<CefImage>, 8>, 8>, 3>, 2>, 2> tab_badges_{};
  std::array<std::array<std::array<CefRefPtr<CefImage>, 16>, 2>, 2> chrome_badges_{};
  std::vector<RetiredUi> retired_ui_;
  std::vector<Tab> tabs_;
  int next_tab_id_ = 1;
  int active_tab_id_ = 0;
  std::string saved_session_snapshot_;
  rc_ui::TabDragIntent tab_drag_;
  bool fullscreen_ = false;
  bool vertical_tabs_ = false;
  bool sidebar_compact_ = false;
  bool fullscreen_sidebar_expanded_ = false;
  bool companion_visible_ = false;
  bool companion_tick_scheduled_ = false;
  unsigned companion_binding_poll_ = 0;
  bool closing_ = false;
  bool window_created_ = false;
  int self_test_tab_id_ = 0;
  int self_test_browser_id_ = -1;
  size_t self_test_base_tabs_ = 0;
  int self_test_last_closed_tab_id_ = 0;
  int self_test_last_replacement_tab_id_ = 0;
};
