#pragma once

#include <map>
#include <string>
#include <vector>

#include "client.h"
#include "commander_companion.h"
#include "include/cef_browser.h"
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
  void ApplyNativeTheme();
  void ToggleMaximize();
  void Minimize();
  void RequestCloseWindow();
  void CycleTab(int delta);
  void MoveActiveTab(int direction);

  void OnWindowCreated(CefRefPtr<CefWindow> window);
  void OnWindowBoundsChanged(const CefRect& new_bounds);
  void OnWindowFullscreenTransition(bool is_completed);
  void OnWindowDestroyed();
  bool CanWindowClose();
  bool OnAccelerator(int command_id);
  void OnButtonPressed(CefRefPtr<CefButton> button);
  void OnButtonStateChanged(CefRefPtr<CefButton> button);

  void OnBrowserCreated(CefRefPtr<CefBrowser> browser);
  bool OnBrowserDoClose(CefRefPtr<CefBrowser> browser);
  void OnBrowserClosed(CefRefPtr<CefBrowser> browser);
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const std::string& title);
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading);

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

  void BuildWindowUI();
  void BuildHeaderControls();
  void UpdateDraggableRegions();
  void RebuildTabStrip();
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
  CefRefPtr<CefPanel> content_;
  CefRefPtr<CefPanel> companion_panel_;
  CefRefPtr<CefOverlayController> companion_overlay_;
  std::vector<CefRefPtr<CefLabelButton>> companion_rows_;
  std::vector<CefRefPtr<CefLabelButton>> header_buttons_;
  CefRefPtr<CefLabelButton> tab_switcher_button_;
  CefRefPtr<CefLabelButton> theme_button_;
  int visible_tab_capacity_ = -1;
  companion::Monitor companion_monitor_;
  companion::Observation companion_observation_;
  companion::NativeBinding companion_binding_;
  CefRefPtr<CefBoxLayout> window_layout_;
  CefRefPtr<CefBoxLayout> header_layout_;
  CefRefPtr<CefBoxLayout> tab_layout_;

  CefRefPtr<WindowDelegateImpl> window_delegate_;
  CefRefPtr<ButtonDelegateImpl> button_delegate_;
  CefRefPtr<BrowserViewDelegateImpl> browser_view_delegate_;
  CefRefPtr<FixedPanelDelegate> header_delegate_;

  CefRefPtr<AppClient> client_;
  std::vector<Tab> tabs_;
  int next_tab_id_ = 1;
  int active_tab_id_ = 0;
  bool fullscreen_ = false;
  bool companion_visible_ = false;
  bool companion_tick_scheduled_ = false;
  bool closing_ = false;
  bool window_created_ = false;
  int self_test_tab_id_ = 0;
  int self_test_browser_id_ = -1;
  size_t self_test_base_tabs_ = 0;
  int self_test_last_closed_tab_id_ = 0;
  int self_test_last_replacement_tab_id_ = 0;
};
