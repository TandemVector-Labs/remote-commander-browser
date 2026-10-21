#include "controller.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "client.h"
#include "ui_chrome_layout.h"
#include "ui_tab_order.h"
#include "ui_tab_drag.h"
#include "include/views/cef_display.h"
#include "ui_chrome_palette.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/base/cef_logging.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_request_context.h"
#include "include/cef_task.h"
#include "include/views/cef_fill_layout.h"
#include "include/internal/cef_types_wrappers.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_closure_task.h"

namespace {
constexpr int kBrandButton = 10;
constexpr int kNewTabButton = 20;
constexpr int kFullscreenButton = 21;
constexpr int kMaximizeButton = 22;
constexpr int kMinimizeButton = 23;
constexpr int kCloseWindowButton = 24;
constexpr int kCompanionButton = 25;
constexpr int kTabSwitcherButton = 26;
constexpr int kThemeButton = 27;
constexpr int kMoveTabLeftButton = 28;
constexpr int kMoveTabRightButton = 29;
constexpr int kCompanionRefreshButton = 30;
constexpr int kCompanionRowBase = 300;
constexpr int kCompanionRowCount = 18;
constexpr int kCompanionWidth = 360;
constexpr int kTabButtonBase = 1000;
constexpr int kTabCloseBase = 2000;

constexpr int kHeaderHeight = 52;
constexpr int kBrandWidth = 164;

constexpr int kAccelNewTab = 100;
constexpr int kAccelCloseTab = 101;
constexpr int kAccelNextTab = 102;
constexpr int kAccelPrevTab = 103;
constexpr int kAccelFullscreen = 104;
constexpr int kAccelCompanion = 105;
constexpr int kAccelMoveTabLeft = 106;
constexpr int kAccelMoveTabRight = 107;
constexpr int kAccelTab1 = 111;

constexpr int kVKeyTab = 0x09;
constexpr int kVKey1 = 0x31;
constexpr int kVKeyT = 0x54;
constexpr int kVKeyW = 0x57;
constexpr int kVKeyC = 0x43;
constexpr int kVKeyF11 = 0x7A;
constexpr int kVKeyPageUp = 0x21;
constexpr int kVKeyPageDown = 0x22;

// R98 user-selected private browser theme, updated only on the CEF UI thread.
rc_ui::ThemeMode g_ui_theme = rc_ui::ThemeMode::Light;
const rc_ui::ThemePalette& UiPalette() { return rc_ui::Palette(g_ui_theme); }

constexpr char kUIFont[] = "Vazirmatn, Noto Sans Arabic, DejaVu Sans, 12px";
constexpr char kUIBoldFont[] = "Vazirmatn, Noto Sans Arabic, DejaVu Sans, Bold 12px";

std::string PathUtf8(const std::filesystem::path& path) {
#if defined(__cpp_lib_char8_t)
  const auto value = path.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
  return path.u8string();
#endif
}

std::string StatePath() {
  if (const char* override_root = std::getenv("CGWA_PROFILE_ROOT");
      override_root && *override_root)
    return PathUtf8(std::filesystem::u8path(override_root) / "tabs.state");
#if defined(_WIN32)
  const char* local = std::getenv("LOCALAPPDATA");
  if (!local || !*local) local = std::getenv("TEMP");
  std::filesystem::path base =
      local && *local ? std::filesystem::u8path(local)
                      : std::filesystem::temp_directory_path();
  return PathUtf8(base / "chatgpt-cef-v2" / "tabs.state");
#else
  const char* home = std::getenv("HOME");
  std::string base = home ? home : "/tmp";
  return base + "/.config/chatgpt-cef-v2/tabs.state";
#endif
}

void RemoveStateFile(const std::string& path) {
  std::error_code ec;
  std::filesystem::remove(std::filesystem::u8path(path), ec);
}

bool CommitStateFile(const std::string& temp, const std::string& path) {
#if defined(_WIN32)
  const std::wstring temp_w = std::filesystem::u8path(temp).wstring();
  const std::wstring path_w = std::filesystem::u8path(path).wstring();
  return ::MoveFileExW(temp_w.c_str(), path_w.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  if (::chmod(temp.c_str(), 0600) != 0) return false;
  return ::rename(temp.c_str(), path.c_str()) == 0;
#endif
}

std::string ThemeStatePath() {
  return PathUtf8(std::filesystem::u8path(StatePath()).parent_path() /
                  "ui-theme.state");
}

rc_ui::ThemeMode ReadThemePreference() {
  std::ifstream input(std::filesystem::u8path(ThemeStatePath()));
  std::string value;
  if (std::getline(input, value) && value == "dark")
    return rc_ui::ThemeMode::Dark;
  return rc_ui::ThemeMode::Light;
}

bool SaveThemePreference(rc_ui::ThemeMode mode) {
  const std::string path = ThemeStatePath(), tmp = path + ".tmp";
  std::error_code error;
  const auto parent = std::filesystem::u8path(path).parent_path();
  std::filesystem::create_directories(parent, error);
  if (error) return false;
  // symlink_status() returns ENOENT for an initially absent theme file.
  // Absence is legitimate on first launch; other I/O failures stay closed.
  const auto safe_file = [](const std::string& candidate, bool must_be_absent) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(
        std::filesystem::u8path(candidate), ec);
    if (ec == std::errc::no_such_file_or_directory) return true;
    if (ec) return false;
    if (!std::filesystem::exists(status)) return true;
    if (std::filesystem::is_symlink(status)) return false;
    return !must_be_absent && std::filesystem::is_regular_file(status);
  };
  if (!safe_file(path, false)) {
    LOG(ERROR) << "CGWA_THEME_PREF_DENIED existing_destination";
    return false;
  }
  if (!safe_file(tmp, true)) {
    LOG(ERROR) << "CGWA_THEME_PREF_DENIED temporary_destination";
    return false;
  }
  std::ofstream out(std::filesystem::u8path(tmp), std::ios::trunc);
  if (!out) return false;
  out << (mode == rc_ui::ThemeMode::Dark ? "dark\n" : "light\n");
  out.flush();
  const bool good = out.good();
  out.close();
  if (!good || !CommitStateFile(tmp, path)) {
    RemoveStateFile(tmp);
    return false;
  }
  return true;
}

void ApplyChromeColorScheme() {
  if (auto context = CefRequestContext::GetGlobalContext()) {
    context->SetChromeColorScheme(
        g_ui_theme == rc_ui::ThemeMode::Dark
            ? CEF_COLOR_VARIANT_DARK : CEF_COLOR_VARIANT_LIGHT, 0);
  }
}

bool StartsWith(const std::string& s, const std::string& p) {
  return s.rfind(p, 0) == 0;
}

bool IsHttpsHost(const std::string& url, const std::string& host) {
  const std::string prefix = "https://" + host;
  if (!StartsWith(url, prefix)) return false;
  if (url.size() == prefix.size()) return true;
  const char boundary = url[prefix.size()];
  return boundary == '/' || boundary == '?' || boundary == '#';
}

std::string Utf8Ellipsize(const std::string& input, size_t max_chars,
                          size_t keep_chars) {
  size_t i = 0;
  size_t chars = 0;
  size_t keep_bytes = input.size();
  while (i < input.size()) {
    if (chars == keep_chars) keep_bytes = i;
    const unsigned char c = static_cast<unsigned char>(input[i]);
    size_t len = 1;
    if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    if (i + len > input.size()) len = 1;
    i += len;
    ++chars;
  }
  if (chars <= max_chars) return input;
  if (keep_bytes > input.size()) keep_bytes = input.size();
  return input.substr(0, keep_bytes) + "…";
}

#if defined(_WIN32) || defined(__linux__)
// UI-thread only, activated by a native CEF tab-title press. Never monitor
// mouse position in the background or when no tab gesture is active.
bool NativeDragDiagnosticEnabled() {
  const auto cl = CefCommandLine::GetGlobalCommandLine();
  return cl && cl->HasSwitch("native-tab-drag-diagnostic");
}
bool ReadNativeDragPointer(CefPoint* out, bool* left_held) {
  if (!out || !left_held) return false;
#if defined(_WIN32)
  POINT p{};
  if (!GetCursorPos(&p)) return false;
  *left_held = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  *out = CefDisplay::ConvertScreenPointFromPixels(CefPoint(p.x, p.y));
  return true;
#elif defined(__linux__)
  Display* display = XOpenDisplay(nullptr);
  if (!display) return false;  // Wayland without Xwayland: fail closed.
  ::Window root_ret = 0, child_ret = 0;
  int root_x=0, root_y=0, window_x=0, window_y=0;
  unsigned int mask=0;
  const Bool ok = XQueryPointer(display, DefaultRootWindow(display),
                 &root_ret, &child_ret, &root_x, &root_y,
                 &window_x, &window_y, &mask);
  XCloseDisplay(display);
  if (!ok) return false;
  *left_held = (mask & Button1Mask) != 0;
  *out = CefPoint(root_x, root_y); // X11 logical coords, scale=1 checked below.
  return true;
#else
  return false;
#endif
}

#endif  // _WIN32 || __linux__ native input only

void SetX11WindowClass(CefRefPtr<CefWindow> window) {
#if defined(__linux__)
  if (!window) return;
  Display* display = XOpenDisplay(nullptr);
  if (!display) return;
  const ::Window xid = static_cast<::Window>(window->GetWindowHandle());
  XClassHint hint{};
  char res_name[] = "chatgpt-cef-v2";
  char res_class[] = "ChatGPT-CEF-V2";
  hint.res_name = res_name;
  hint.res_class = res_class;
  XSetClassHint(display, xid, &hint);
  XFlush(display);
  XCloseDisplay(display);
#else
  (void)window;
#endif
}

}  // namespace

AppController* AppController::instance_ = nullptr;

class AppController::FixedPanelDelegate : public CefPanelDelegate {
 public:
  FixedPanelDelegate(int width, int height) : width_(width), height_(height) {}
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    return CefSize(width_, height_);
  }
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    if (view) view->SetBackgroundColor(UiPalette().header);
  }
 private:
  int width_;
  int height_;
  IMPLEMENT_REFCOUNTING(FixedPanelDelegate);
};

class AppController::ButtonDelegateImpl : public CefButtonDelegate {
 public:
  explicit ButtonDelegateImpl(AppController* owner) : owner_(owner) {}
  void OnButtonPressed(CefRefPtr<CefButton> button) override {
    if (owner_) owner_->OnButtonPressed(button);
  }
  void OnButtonStateChanged(CefRefPtr<CefButton> button) override {
    if (owner_) owner_->OnButtonStateChanged(button);
  }
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    if (owner_ && view) {
      auto button = view->AsButton();
      if (button) owner_->OnButtonStateChanged(button);
    }
  }
  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    const int id = view->GetID();
    if (id == kBrandButton) return CefSize(kBrandWidth, 40);
    if (id == kCompanionButton) return CefSize(108, 40);
    if (id == kThemeButton) return CefSize(80, 40);
    if (id == kTabSwitcherButton) return CefSize(58, 40);
    if (id == kCompanionRefreshButton) return CefSize(kCompanionWidth - 24, 40);
    if (id >= kCompanionRowBase && id < kCompanionRowBase + kCompanionRowCount)
      return CefSize(kCompanionWidth - 24, 30);
    if (id >= kTabCloseBase) return CefSize(28, 38);
    if (id >= kTabButtonBase) return CefSize(135, 38);
    if (id >= kNewTabButton && id <= kCloseWindowButton) return CefSize(36, 40);
    return CefSize(80, 34);
  }
 private:
  AppController* owner_;
  IMPLEMENT_REFCOUNTING(ButtonDelegateImpl);
};

class AppController::BrowserViewDelegateImpl : public CefBrowserViewDelegate {
 public:
  explicit BrowserViewDelegateImpl(AppController* owner) : owner_(owner) {}
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
  bool OnPopupBrowserViewCreated(CefRefPtr<CefBrowserView> browser_view,
                                 CefRefPtr<CefBrowserView> popup_browser_view,
                                 bool is_devtools) override {
    return false;
  }
 private:
  AppController* owner_;
  IMPLEMENT_REFCOUNTING(BrowserViewDelegateImpl);
};

class AppController::WindowDelegateImpl : public CefWindowDelegate {
 public:
  explicit WindowDelegateImpl(AppController* owner) : owner_(owner) {}
  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    if (owner_) owner_->OnWindowCreated(window);
  }
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override {
    if (owner_) owner_->OnWindowBoundsChanged(new_bounds);
  }
  void OnWindowFullscreenTransition(CefRefPtr<CefWindow> window,
                                    bool is_completed) override {
    if (owner_) owner_->OnWindowFullscreenTransition(is_completed);
  }
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override {
    if (owner_) owner_->OnWindowDestroyed();
  }
  CefRect GetInitialBounds(CefRefPtr<CefWindow> window) override {
    return CefRect(90, 56, 1240, 760);
  }
  cef_show_state_t GetInitialShowState(CefRefPtr<CefWindow> window) override {
    auto command_line = CefCommandLine::GetGlobalCommandLine();
    if (command_line && command_line->HasSwitch("background-test"))
      return CEF_SHOW_STATE_MINIMIZED;
    return CEF_SHOW_STATE_NORMAL;
  }
  bool IsFrameless(CefRefPtr<CefWindow> window) override { return true; }
  bool CanResize(CefRefPtr<CefWindow> window) override { return true; }
  bool CanMaximize(CefRefPtr<CefWindow> window) override { return true; }
  bool CanMinimize(CefRefPtr<CefWindow> window) override { return true; }
  bool CanClose(CefRefPtr<CefWindow> window) override {
    return owner_ ? owner_->CanWindowClose() : true;
  }
  bool OnAccelerator(CefRefPtr<CefWindow> window, int command_id) override {
    return owner_ ? owner_->OnAccelerator(command_id) : false;
  }
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }
 private:
  AppController* owner_;
  IMPLEMENT_REFCOUNTING(WindowDelegateImpl);
};

AppController::AppController() {
  DCHECK(!instance_);
  instance_ = this;
  client_ = new AppClient();
  window_delegate_ = new WindowDelegateImpl(this);
  button_delegate_ = new ButtonDelegateImpl(this);
  browser_view_delegate_ = new BrowserViewDelegateImpl(this);
  header_delegate_ = new FixedPanelDelegate(1240, kHeaderHeight);
  g_ui_theme = ReadThemePreference();
}

AppController::~AppController() {
  if (instance_ == this) instance_ = nullptr;
}

AppController* AppController::Get() { return instance_; }

void AppController::CreateMainWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (window_created_ && window_) {
    ActivateMainWindow();
    return;
  }
  CefWindow::CreateTopLevelWindow(window_delegate_);
}

void AppController::ActivateMainWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || window_->IsClosed()) return;
  if (window_->IsMinimized()) window_->Restore();
  window_->Show();
  window_->BringToTop();
  window_->Activate();
}

void AppController::OnWindowCreated(CefRefPtr<CefWindow> window) {
  CEF_REQUIRE_UI_THREAD();
  window_ = window;
  window_created_ = true;
  window_->SetTitle("Remote Commander Browser");
  ApplyChromeColorScheme();
  SetX11WindowClass(window_);
  BuildWindowUI();
  window_->CenterWindow(CefSize(1240, 760));
  window_->Show();
  auto command_line = CefCommandLine::GetGlobalCommandLine();
  const bool background_test =
      command_line && command_line->HasSwitch("background-test");
  if (background_test)
    window_->Minimize();
  else
    window_->Activate();

  // Restore simple URL session state. Each line is a URL. The first line may be
  // "active=N". Never restore more than 8 tabs.
  std::ifstream in(StatePath());
  std::vector<std::string> urls;
  int active_index = 0;
  std::string line;
  if (in && std::getline(in, line) && StartsWith(line, "active=")) {
    active_index = std::max(0, std::atoi(line.substr(7).c_str()));
  } else if (!line.empty()) {
    urls.push_back(line);
  }
  while (in && std::getline(in, line) && urls.size() < 8) {
    if (StartsWith(line, "https://")) urls.push_back(line);
  }
  if (urls.empty()) urls.push_back("https://chatgpt.com/");
  for (const auto& url : urls) NewTab(url);
  if (active_index >= 0 && active_index < static_cast<int>(tabs_.size()))
    SetActiveTabInternal(tabs_[active_index].id);

  if (command_line && command_line->HasSwitch("companion-self-test"))
    RunCompanionSelfTest();

  if (command_line && command_line->HasSwitch("self-test"))
    RunBackgroundSelfTest();

  if (command_line && command_line->HasSwitch("shutdown-self-test")) {
    LOG(WARNING) << "CGWA_SHUTDOWN SELFTEST_SCHEDULED";
    CefPostDelayedTask(
        TID_UI,
        base::BindOnce(&AppController::RequestCloseWindow,
                       base::Unretained(this)),
        7000);
  }
}

void AppController::RunBackgroundSelfTest() {
  CEF_REQUIRE_UI_THREAD();
  self_test_base_tabs_ = tabs_.size();
  NewTab("https://chatgpt.com/");
  self_test_tab_id_ = active_tab_id_;
  Tab* tab = FindTabById(self_test_tab_id_);
  if (tab && tab->view && tab->view->GetBrowser())
    self_test_browser_id_ = tab->view->GetBrowser()->GetIdentifier();
  LOG(WARNING) << "CGWA_SELFTEST TAB_CREATE before=" << self_test_base_tabs_
               << " after=" << tabs_.size()
               << " browser=" << self_test_browser_id_;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&AppController::BackgroundSelfTestClose,
                     base::Unretained(this)),
      1800);
}

void AppController::BackgroundSelfTestClose() {
  CEF_REQUIRE_UI_THREAD();
  Tab* tab = FindTabById(self_test_tab_id_);
  if (tab && tab->view && tab->view->GetBrowser())
    self_test_browser_id_ = tab->view->GetBrowser()->GetIdentifier();
  CloseTab(self_test_tab_id_);
  LOG(WARNING) << "CGWA_SELFTEST TAB_CLOSE_REQUEST current=" << tabs_.size()
               << " browser=" << self_test_browser_id_;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&AppController::BackgroundSelfTestVerify,
                     base::Unretained(this)),
      2500);
}

void AppController::BackgroundSelfTestVerify() {
  CEF_REQUIRE_UI_THREAD();
  auto browser = self_test_browser_id_ > 0
                     ? CefBrowserHost::GetBrowserByIdentifier(self_test_browser_id_)
                     : nullptr;
  const bool pass = tabs_.size() == self_test_base_tabs_ && !browser;
  LOG(WARNING) << "CGWA_SELFTEST " << (pass ? "PASS" : "FAIL")
               << " final_tabs=" << tabs_.size()
               << " browser=" << self_test_browser_id_
               << " still_exists=" << (browser ? 1 : 0);
  // A normal two-tab close is insufficient to detect the last-tab window
  // shutdown regression. The CI profile starts with exactly one tab.
  if (!pass || self_test_base_tabs_ != 1 || closing_ || !window_) {
    LOG(WARNING) << "CGWA_LAST_TAB_SELFTEST FAIL preconditions";
    return;
  }
  self_test_last_closed_tab_id_ = active_tab_id_;
  CloseTab(self_test_last_closed_tab_id_);
  self_test_last_replacement_tab_id_ = active_tab_id_;
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&AppController::BackgroundLastTabSelfTestVerify,
                     base::Unretained(this)),
      850);
}

void AppController::BackgroundLastTabSelfTestVerify() {
  CEF_REQUIRE_UI_THREAD();
  const bool pass = !closing_ && window_ && !window_->IsClosed() &&
                    tabs_.size() == 1 &&
                    self_test_last_closed_tab_id_ != self_test_last_replacement_tab_id_ &&
                    active_tab_id_ == self_test_last_replacement_tab_id_ &&
                    !FindTabById(self_test_last_closed_tab_id_) &&
                    FindTabById(self_test_last_replacement_tab_id_);
  LOG(WARNING) << "CGWA_LAST_TAB_SELFTEST " << (pass ? "PASS" : "FAIL")
               << " remaining=" << tabs_.size()
               << " closing=" << closing_
               << " replaced_id=" << self_test_last_replacement_tab_id_;
  if (pass) {
    const auto initial = g_ui_theme;
    ToggleTheme();
    const bool toggled = g_ui_theme != initial &&
                         ReadThemePreference() == g_ui_theme;
    ToggleTheme();
    const bool restored = g_ui_theme == initial &&
                          ReadThemePreference() == initial;
    const auto context = CefRequestContext::GetGlobalContext();
    const bool chrome = context &&
        context->GetChromeColorSchemeMode() ==
            (initial == rc_ui::ThemeMode::Dark
                ? CEF_COLOR_VARIANT_DARK : CEF_COLOR_VARIANT_LIGHT);
    LOG(WARNING) << "CGWA_THEME_SELFTEST "
                 << (toggled && restored && chrome ? "PASS" : "FAIL")
                 << " toggle=" << toggled
                 << " restored=" << restored
                 << " chromium=" << chrome;
  } else {
    LOG(WARNING) << "CGWA_THEME_SELFTEST FAIL tab_precondition";
  }
  // The original last-tab self-test invoked CloseTab() directly and missed
  // the native tab-X button event route that the owner observed failing.
  if (!pass) {
    LOG(WARNING) << "CGWA_UI_TAB_CLOSE_SELFTEST FAIL tab_precondition";
    return;
  }
  Tab* current = FindTabById(active_tab_id_);
  if (!current || !current->close_button) {
    LOG(WARNING) << "CGWA_UI_TAB_CLOSE_SELFTEST FAIL no_close_button";
    return;
  }
  self_test_last_closed_tab_id_ = current->id;
  OnButtonPressed(current->close_button);
  CefPostDelayedTask(
      TID_UI,
      base::BindOnce(&AppController::BackgroundUiTabCloseSelfTestVerify,
                     base::Unretained(this)),
      650);
}

void AppController::BackgroundUiTabCloseSelfTestVerify() {
  CEF_REQUIRE_UI_THREAD();
  const bool pass = !closing_ && window_ && !window_->IsClosed() &&
                    tabs_.size() == 1 &&
                    active_tab_id_ != self_test_last_closed_tab_id_ &&
                    !FindTabById(self_test_last_closed_tab_id_) &&
                    FindTabById(active_tab_id_) != nullptr;
  LOG(WARNING) << "CGWA_UI_TAB_CLOSE_SELFTEST " << (pass ? "PASS" : "FAIL")
               << " remaining=" << tabs_.size()
               << " replacement_id=" << active_tab_id_
               << " closing=" << closing_;
}

void AppController::OnWindowBoundsChanged(const CefRect& new_bounds) {
  CEF_REQUIRE_UI_THREAD();
  int active_index = 0;
  for (size_t i = 0; i < tabs_.size(); ++i)
    if (tabs_[i].id == active_tab_id_) active_index = static_cast<int>(i);
  const auto visible = rc_ui::SelectTabs(
      new_bounds.width, static_cast<int>(tabs_.size()), active_index);
  if (visible.capacity != visible_tab_capacity_) RebuildTabStrip();
  LayoutTabOverlays();
  UpdateDraggableRegions();
  if (companion_visible_) UpdateCompanionPanel();
}

void AppController::OnWindowFullscreenTransition(bool is_completed) {
  CEF_REQUIRE_UI_THREAD();
  if (!is_completed || !window_) return;
  fullscreen_ = window_->IsFullscreen();
  if (header_) header_->SetVisible(!fullscreen_);
  window_->Layout();
  LayoutTabOverlays();
  UpdateDraggableRegions();
}

void AppController::BuildWindowUI() {
  CefBoxLayoutSettings vertical;
  vertical.horizontal = false;
  vertical.between_child_spacing = 0;
  vertical.inside_border_horizontal_spacing = 0;
  vertical.inside_border_vertical_spacing = 0;
  vertical.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  vertical.default_flex = 0;
  window_layout_ = window_->SetToBoxLayout(vertical);

  header_ = CefPanel::CreatePanel(header_delegate_);
  header_->SetBackgroundColor(UiPalette().header);
  CefBoxLayoutSettings horizontal;
  horizontal.horizontal = true;
  horizontal.between_child_spacing = 2;
  horizontal.inside_border_horizontal_spacing = 8;
  horizontal.inside_border_vertical_spacing = 4;
  horizontal.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
  horizontal.default_flex = 0;
  header_layout_ = header_->SetToBoxLayout(horizontal);

  tab_strip_ = CefPanel::CreatePanel(nullptr);
  tab_strip_->SetBackgroundColor(UiPalette().header);
  CefBoxLayoutSettings tabs_layout;
  tabs_layout.horizontal = true;
  tabs_layout.between_child_spacing = 2;
  tabs_layout.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
  tabs_layout.default_flex = 0;
  tab_layout_ = tab_strip_->SetToBoxLayout(tabs_layout);

  content_ = CefPanel::CreatePanel(nullptr);
  content_->SetToFillLayout();
  content_->SetBackgroundColor(UiPalette().content);

  window_->AddChildView(header_);
  window_->AddChildView(content_);
  window_layout_->SetFlexForView(content_, 1);
  BuildHeaderControls();
  BuildCompanionPanel();

  UpdateDraggableRegions();

  // High-priority native accelerators work regardless of page focus.
  window_->SetAccelerator(kAccelNewTab, kVKeyT, false, true, false, true);
  window_->SetAccelerator(kAccelCloseTab, kVKeyW, false, true, false, true);
  window_->SetAccelerator(kAccelNextTab, kVKeyTab, false, true, false, true);
  window_->SetAccelerator(kAccelPrevTab, kVKeyTab, true, true, false, true);
  // Chrome-compatible move shortcuts, handled before focused web content.
  window_->SetAccelerator(kAccelMoveTabLeft, kVKeyPageUp, true, true, false, true);
  window_->SetAccelerator(kAccelMoveTabRight, kVKeyPageDown, true, true, false, true);
  window_->SetAccelerator(kAccelFullscreen, kVKeyF11, false, false, false, true);
  window_->SetAccelerator(kAccelCompanion, kVKeyC, true, true, false, true);
  for (int i = 0; i < 8; ++i)
    window_->SetAccelerator(kAccelTab1 + i, kVKey1 + i, false, true, false, true);
}

void AppController::BuildHeaderControls() {
  auto make_button = [&](int id, const std::string& text, cef_color_t bg) {
    auto button = CefLabelButton::CreateLabelButton(button_delegate_, text);
    button->SetID(id);
    button->SetBackgroundColor(bg);
    button->SetEnabledTextColors(UiPalette().text);
    button->SetFontList(kUIFont);
    button->SetInkDropEnabled(true);
    return button;
  };

  auto brand = make_button(kBrandButton, "RC  /  Browser", UiPalette().header);
  brand->SetFontList(kUIBoldFont);
  brand->SetEnabledTextColors(UiPalette().text);
  brand->SetAccessibleName("Remote Commander Browser application");
  brand->SetTooltipText("Remote Commander Browser — private native window");
  header_buttons_.push_back(brand);
  header_->AddChildView(brand);

  header_->AddChildView(tab_strip_);
  header_layout_->SetFlexForView(tab_strip_, 1);

  auto switcher = make_button(kTabSwitcherButton, "1 / 1", UiPalette().softAccent);
  switcher->SetEnabledTextColors(UiPalette().activeText);
  switcher->SetTooltipText("Next tab (Ctrl+Tab); Ctrl+1..8 selects any tab");
  switcher->SetAccessibleName("Switch tabs");
  tab_switcher_button_ = switcher;
  header_buttons_.push_back(switcher);
  header_->AddChildView(switcher);

  // Mouse-accessible order controls; only the selected tab is moved.
  auto move_left = make_button(kMoveTabLeftButton, "←", UiPalette().button);
  move_left->SetTooltipText("Move selected tab left (Ctrl+Shift+PgUp)");
  move_left->SetAccessibleName("Move selected tab left");
  header_buttons_.push_back(move_left);
  header_->AddChildView(move_left);
  auto move_right = make_button(kMoveTabRightButton, "→", UiPalette().button);
  move_right->SetTooltipText("Move selected tab right (Ctrl+Shift+PgDn)");
  move_right->SetAccessibleName("Move selected tab right");
  header_buttons_.push_back(move_right);
  header_->AddChildView(move_right);

  auto add = make_button(kNewTabButton, "＋", UiPalette().button);
  add->SetEnabledTextColors(UiPalette().activeText);
  add->SetTooltipText("New tab (Ctrl+T)");
  add->SetAccessibleName("New tab");
  header_buttons_.push_back(add);
  header_->AddChildView(add);

  auto companion = make_button(kCompanionButton, "Commander", UiPalette().button);
  companion->SetTooltipText("پنل مشاهده Commander (Ctrl+Shift+C)");
  companion->SetAccessibleName("Toggle read-only Commander companion");
  header_buttons_.push_back(companion);
  header_->AddChildView(companion);

  auto theme = make_button(
      kThemeButton, g_ui_theme == rc_ui::ThemeMode::Dark ? "Dark" : "Light",
      UiPalette().softAccent);
  theme->SetTooltipText("Switch Light/Dark appearance (private browser setting)");
  theme->SetAccessibleName("Switch browser theme");
  theme_button_ = theme;
  header_buttons_.push_back(theme);
  header_->AddChildView(theme);

  auto full = make_button(kFullscreenButton, "⛶", UiPalette().button);
  full->SetTooltipText("Fullscreen (F11)");
  full->SetAccessibleName("Toggle fullscreen");
  header_buttons_.push_back(full);
  header_->AddChildView(full);

  auto max = make_button(kMaximizeButton, "▢", UiPalette().button);
  max->SetTooltipText("Maximize / Restore");
  max->SetAccessibleName("Maximize or restore");
  header_buttons_.push_back(max);
  header_->AddChildView(max);

  auto min = make_button(kMinimizeButton, "−", UiPalette().button);
  min->SetTooltipText("Minimize");
  min->SetAccessibleName("Minimize");
  header_buttons_.push_back(min);
  header_->AddChildView(min);

  auto close = make_button(kCloseWindowButton, "×", UiPalette().danger);
  close->SetEnabledTextColors(UiPalette().muted);
  close->SetTooltipText("Close");
  close->SetAccessibleName("Close application");
  header_buttons_.push_back(close);
  header_->AddChildView(close);
}


void AppController::UpdateDraggableRegions() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_) return;
  if (fullscreen_ || !header_ || !header_->IsVisible()) {
    window_->SetDraggableRegions({});
    return;
  }

  window_->Layout();
  header_->Layout();
  if (tab_strip_) tab_strip_->Layout();

  const CefRect header_bounds = header_->GetBounds();
  if (header_bounds.width <= 0 || header_bounds.height <= 0) {
    window_->SetDraggableRegions({});
    return;
  }

  std::vector<CefDraggableRegion> regions;
  // Start with the full visible top bar. Subtract only actual clickable
  // controls so unused header/tab-strip space behaves like a normal title bar.
  regions.emplace_back(
      CefRect(header_bounds.x, header_bounds.y,
              header_bounds.width, header_bounds.height),
      true);

  auto subtract = [&](const CefRect& local, int parent_x, int parent_y) {
    if (local.width <= 0 || local.height <= 0) return;
    regions.emplace_back(
        CefRect(parent_x + local.x, parent_y + local.y,
                local.width, local.height),
        false);
  };

  for (const auto& button : header_buttons_) {
    if (button && button->IsVisible())
      subtract(button->GetBounds(), header_bounds.x, header_bounds.y);
  }

  if (tab_strip_ && tab_strip_->IsVisible()) {
    const CefRect strip = tab_strip_->GetBounds();
    const int strip_x = header_bounds.x + strip.x;
    const int strip_y = header_bounds.y + strip.y;
    for (const auto& tab : tabs_) {
      if (tab.ui_panel && tab.ui_panel->IsVisible())
        subtract(tab.ui_panel->GetBounds(), strip_x, strip_y);
    }
  }

  window_->SetDraggableRegions(regions);
}

void AppController::BuildCompanionPanel() {
  companion_panel_ = CefPanel::CreatePanel(nullptr);
  companion_panel_->SetBackgroundColor(UiPalette().header);
  CefBoxLayoutSettings settings;
  settings.horizontal = false;
  settings.between_child_spacing = 2;
  settings.inside_border_horizontal_spacing = 12;
  settings.inside_border_vertical_spacing = 10;
  settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  companion_panel_->SetToBoxLayout(settings);
  for (int i = 0; i < kCompanionRowCount; ++i) {
    auto row = CefLabelButton::CreateLabelButton(button_delegate_, "");
    row->SetID(kCompanionRowBase + i);
    row->SetFontList(i == 0 ? kUIBoldFont : kUIFont);
    row->SetEnabledTextColors(UiPalette().text);
    row->SetBackgroundColor(UiPalette().header);
    row->SetFocusable(false);
    // This is a native display row, not an action. Full bounded details are
    // accessible via its tooltip/name, without injecting anything into a page.
    companion_panel_->AddChildView(row);
    companion_rows_.push_back(row);
  }
  auto refresh = CefLabelButton::CreateLabelButton(button_delegate_, "تازه‌سازی مانیتور محلی");
  refresh->SetID(kCompanionRefreshButton);
  refresh->SetFontList(kUIFont);
  refresh->SetEnabledTextColors(UiPalette().activeText);
  refresh->SetBackgroundColor(UiPalette().softAccent);
  refresh->SetTooltipText("Reads private local Commander monitor/binding files only; no network, model or command execution.");
  refresh->SetAccessibleName("Refresh read-only local observation");
  companion_panel_->AddChildView(refresh);
  companion_overlay_ = window_->AddOverlayView(companion_panel_, CEF_DOCKING_MODE_CUSTOM, true);
  if (companion_overlay_ && companion_overlay_->IsValid()) companion_overlay_->SetVisible(false);
  UpdateCompanionPanel();
}

void AppController::ToggleCompanionPanel() {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || !companion_overlay_ || !companion_overlay_->IsValid()) return;
  companion_visible_ = !companion_visible_;
  if (companion_visible_) RefreshCompanion();
  LayoutTabOverlays();
}

void AppController::RefreshCompanion() {
  CEF_REQUIRE_UI_THREAD();
  if (closing_) return;
  const std::string root = companion::ProfileRoot();
  companion_monitor_ = companion::ReadMonitor(root);
  companion_automation_ = companion::ReadAutomation(root);
  companion_observation_ = companion::ReadObservation(root);
  companion_binding_ = companion::ReadNativeBinding(root);
  UpdateCompanionPanel();
  if (companion_visible_ && !companion_tick_scheduled_) CompanionTick();
}

void AppController::UpdateCompanionPanel() {
  if (!companion_panel_) return;
  std::string active_url;
  Tab* active = FindTabById(active_tab_id_);
  // Read the actual native main-frame URL, never a cached restored/tab title.
  // During navigation/loading the chat binding deliberately remains unknown.
  if (active && !active->loading && active->view && active->view->GetBrowser()) {
    auto frame = active->view->GetBrowser()->GetMainFrame();
    if (frame) active_url = frame->GetURL().ToString();
  }
  const uint64_t now=companion::NowEpochMs();
  auto rows = companion::DescribeMonitor(companion_monitor_, now);
  const auto native = native_chat_observer_.Snapshot();
  rows.insert(rows.begin(), companion::DisplayRow{
      "Browser Core | Chat tabs " + std::to_string(native.open_tabs) +
          " | Conversations " + std::to_string(native.conversations),
      "CEF-owned in-process observation only; no extension, Native Host, account attestation or chat automation"});
  const auto automation_rows = companion::DescribeAutomation(companion_automation_, companion_monitor_, now);
  rows.insert(rows.end(), automation_rows.begin(), automation_rows.end());
  const auto binding_rows = companion::Describe(companion_observation_, companion_binding_, active_url, now);
  rows.insert(rows.end(), binding_rows.begin(), binding_rows.end());
  const int available_height =
      window_ ? window_->GetClientAreaBoundsInScreen().height -
                    (fullscreen_ ? 0 : kHeaderHeight) : 568;
  const int slots = rc_ui::CompanionRowSlots(
      available_height, static_cast<int>(rows.size()));
  const bool overflow = static_cast<int>(rows.size()) > slots;
  for (size_t i = 0; i < companion_rows_.size(); ++i) {
    const bool visible = static_cast<int>(i) < slots;
    companion_rows_[i]->SetVisible(visible);
    if (!visible) continue;
    auto row = companion_rows_[i];
    if (overflow && static_cast<int>(i) == slots - 1) {
      const int remaining = static_cast<int>(rows.size()) - slots + 1;
      const std::string summary = "… +" + std::to_string(remaining) +
                                  " more • enlarge window";
      row->SetText(summary);
      row->SetTooltipText("Additional local monitor fields are hidden at this window height");
      row->SetAccessibleName(summary);
      row->SetEnabledTextColors(UiPalette().muted);
      continue;
    }
    row->SetText(Utf8Ellipsize(rows[i].text, 42, 39));
    row->SetTooltipText(rows[i].detail);
    row->SetAccessibleName(rows[i].text + " | " + rows[i].detail);
    const bool caution = rows[i].text.find("UNVERIFIED") != std::string::npos ||
                         rows[i].text.find("DISCONNECTED") != std::string::npos ||
                         rows[i].text.find("BLOCKED") != std::string::npos;
    row->SetEnabledTextColors(caution ? UiPalette().dangerText : UiPalette().text);
  }
  companion_panel_->Layout();
}

void AppController::CompanionTick() {
  CEF_REQUIRE_UI_THREAD();
  companion_tick_scheduled_ = false;
  if (closing_ || !companion_visible_) return;
  companion_monitor_ = companion::ReadMonitor(companion::ProfileRoot());
  companion_automation_ = companion::ReadAutomation(companion::ProfileRoot());
  UpdateCompanionPanel();
  companion_tick_scheduled_ = true;
  // No captured controller pointer: a queued tick cannot dereference a
  // destroyed owner. It only rereads one bounded private local monitor file.
  CefPostDelayedTask(TID_UI, base::BindOnce([]() {
    if (auto* controller = AppController::Get()) controller->CompanionTick();
  }), 1000);
}

void AppController::RunCompanionSelfTest() {
  CEF_REQUIRE_UI_THREAD();
  ToggleCompanionPanel();
  Tab* active = FindTabById(active_tab_id_);
  const bool shown = companion_overlay_ && companion_overlay_->IsValid() && companion_overlay_->IsVisible();
  const bool separate = shown && active && active->overlay && active->overlay->IsValid() &&
    active->overlay->GetBounds().width == companion_overlay_->GetBounds().x &&
    companion_overlay_->GetBounds().width == kCompanionWidth;
#if defined(_WIN32)
  const bool companion_safe =
      !companion_monitor_.valid &&
      companion_monitor_.error != "WINDOWS_PRIVATE_FILE_GUARD_UNAVAILABLE" &&
      !companion_observation_.valid &&
      companion_observation_.error != "WINDOWS_PRIVATE_FILE_GUARD_UNAVAILABLE" &&
      !companion_binding_.valid &&
      companion_binding_.error != "WINDOWS_PRIVATE_FILE_GUARD_UNAVAILABLE";
  const char* companion_check = "windows_private_file_guard_active";
#else
  const bool companion_safe =
      !companion_monitor_.valid && companion_monitor_.error == "MONITOR_MISSING" &&
      !companion_observation_.valid && companion_observation_.error == "SNAPSHOT_MISSING" &&
      !companion_binding_.valid;
  const char* companion_check = "missing_monitor_snapshot_unproven";
#endif
  ToggleCompanionPanel();
  const bool hidden = companion_overlay_ && companion_overlay_->IsValid() && !companion_overlay_->IsVisible();
  LOG(WARNING) << "CGWA_COMPANION_SELFTEST " << (shown && separate && companion_safe && hidden ? "PASS" : "FAIL")
    << " native_overlay=" << shown << " separate_bounds=" << separate
    << " " << companion_check << "=" << companion_safe << " toggle_hidden=" << hidden;
}

void AppController::NewTab(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || !rc_ui::CanCreateTab(static_cast<int>(tabs_.size()))) return;

  const std::string initial_url = url.empty() ? "https://chatgpt.com/" : url;
  CefBrowserSettings settings;
  auto view = CefBrowserView::CreateBrowserView(
      client_, initial_url, settings, nullptr, nullptr, browser_view_delegate_);

  Tab tab;
  tab.id = next_tab_id_++;
  tab.view = view;
  tab.url = initial_url;
  tab.loading = true;
  tabs_.push_back(tab);

  Tab& added = tabs_.back();
  added.overlay = window_->AddOverlayView(
      added.view, CEF_DOCKING_MODE_CUSTOM, true);
  if (!added.overlay || !added.overlay->IsValid()) {
    LOG(ERROR) << "Failed to create overlay tab id=" << added.id;
    tabs_.pop_back();
    return;
  }
  native_chat_observer_.Observe(added.id, initial_url);
  // Overlay BrowserViews are hidden by default. Only the active tab is shown.
  added.overlay->SetVisible(false);
  LayoutTabOverlays();
  SetActiveTabInternal(added.id);
}

void AppController::ActivateTab(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  SetActiveTabInternal(tab_id);
}

void AppController::SetActiveTabInternal(int tab_id) {
  Tab* target = FindTabById(tab_id);
  if (!target) return;
  active_tab_id_ = tab_id;
  native_chat_observer_.SetActive(tab_id);
  RebuildTabStrip();
  for (auto& tab : tabs_) {
    const bool active = tab.id == tab_id;
    if (tab.overlay && tab.overlay->IsValid())
      tab.overlay->SetVisible(active);
    UpdateTabButton(tab);
  }
  if (target->view) target->view->RequestFocus();
  SaveSessionState();
  UpdateCompanionPanel();
}

void AppController::LayoutTabOverlays() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || window_->IsClosed()) return;
  const CefRect client_bounds = window_->GetClientAreaBoundsInScreen();
  const bool header_visible = header_ && header_->IsVisible();
  const int top = header_visible ? kHeaderHeight : 0;
  const int width = std::max(1, client_bounds.width);
  const int height = std::max(1, client_bounds.height - top);
  const bool show_companion = companion_visible_ && !fullscreen_ && width >= kCompanionWidth + 320;
  const int browser_width = show_companion ? width - kCompanionWidth : width;
  const CefRect bounds(0, top, browser_width, height);
  for (auto& tab : tabs_) {
    if (tab.overlay && tab.overlay->IsValid())
      tab.overlay->SetBounds(bounds);
  }
  if (companion_overlay_ && companion_overlay_->IsValid()) {
    companion_overlay_->SetBounds(CefRect(browser_width, top, kCompanionWidth, height));
    companion_overlay_->SetVisible(show_companion);
  }
}

void AppController::SaveSessionState() {
  if (tabs_.empty()) return;
  const std::string path = StatePath();
  const std::string temp = path + ".tmp";
  std::error_code directory_error;
  const auto parent = std::filesystem::u8path(path).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, directory_error);
  if (directory_error) return;

  std::ofstream out(std::filesystem::u8path(temp), std::ios::trunc);
  if (!out) return;
  int active_index = 0;
  for (size_t i = 0; i < tabs_.size(); ++i)
    if (tabs_[i].id == active_tab_id_) active_index = static_cast<int>(i);
  out << "active=" << active_index << "\n";
  for (auto& tab : tabs_) {
    std::string url = tab.url.empty() ? "https://chatgpt.com/" : tab.url;
    if (tab.view && tab.view->GetBrowser() && tab.view->GetBrowser()->GetMainFrame()) {
      const std::string current = tab.view->GetBrowser()->GetMainFrame()->GetURL().ToString();
      if (IsChatGPTURL(current)) {
        url = current;
        tab.url = current;
      }
    }
    if (IsChatGPTURL(url)) out << url << "\n";
  }
  out.flush();
  const bool good = out.good();
  out.close();
  if (!good || !CommitStateFile(temp, path))
    RemoveStateFile(temp);
}

void AppController::CloseTab(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  auto it = std::find_if(tabs_.begin(), tabs_.end(),
                         [tab_id](const Tab& t) { return t.id == tab_id; });
  if (it == tabs_.end() || it->closing || closing_) return;
  if (tabs_.size() == 1) {
    // Preserve the parent CEF window: replace the last tab before removing it.
    // The dedicated window-close button still uses RequestCloseWindow().
    NewTab();
    if (tabs_.size() != 2) {
      LOG(ERROR) << "CGWA_TAB_LAST_REPLACEMENT_FAILED tab=" << tab_id;
      return; // Fail closed: do not destroy the only working tab.
    }
    // NewTab() can reallocate the tabs vector. Never reuse the old iterator.
    it = std::find_if(tabs_.begin(), tabs_.end(),
                      [tab_id](const Tab& t) { return t.id == tab_id; });
    if (it == tabs_.end() || it->closing) return;
    LOG(WARNING) << "CGWA_TAB_LAST_REPLACED tab=" << tab_id;
  }

  const size_t index = static_cast<size_t>(std::distance(tabs_.begin(), it));
  const bool was_active = it->id == active_tab_id_;
  const int browser_id = it->browser_id > 0
                             ? it->browser_id
                             : (it->view && it->view->GetBrowser()
                                    ? it->view->GetBrowser()->GetIdentifier()
                                    : -1);
  auto overlay = it->overlay;
  it->closing = true;
  if (overlay && overlay->IsValid()) overlay->SetVisible(false);

  tabs_.erase(it);
  if (was_active && !tabs_.empty()) {
    const size_t next = std::min(index, tabs_.size() - 1);
    SetActiveTabInternal(tabs_[next].id);
  } else if (!tabs_.empty()) {
    SetActiveTabInternal(active_tab_id_);
  }

  LOG(WARNING) << "CGWA_TAB DESTROY tab=" << tab_id
               << " browser=" << browser_id
               << " remaining=" << tabs_.size();
  if (overlay && overlay->IsValid()) overlay->Destroy();
}

void AppController::QueueTabClose(int tab_id) {
  CEF_REQUIRE_UI_THREAD();
  // A button's own OnButtonPressed event must return before the tab-X view
  // that generated it can be destroyed. Capture only the immutable tab id.
  if (closing_ || !FindTabById(tab_id)) return;
  LOG(WARNING) << "CGWA_TAB_CLOSE_UI_QUEUED tab=" << tab_id;
  CefPostTask(
      TID_UI,
      base::BindOnce(&AppController::CloseTab,
                     base::Unretained(this), tab_id));
}

void AppController::CloseActiveTab() { QueueTabClose(active_tab_id_); }

void AppController::CycleTab(int delta) {
  if (tabs_.empty()) return;
  int index = 0;
  for (size_t i = 0; i < tabs_.size(); ++i)
    if (tabs_[i].id == active_tab_id_) index = static_cast<int>(i);
  const int n = static_cast<int>(tabs_.size());
  index = (index + delta + n) % n;
  SetActiveTabInternal(tabs_[index].id);
}

void AppController::BeginTabDrag(CefRefPtr<CefButton> button) {
#if defined(_WIN32) || defined(__linux__)
  CEF_REQUIRE_UI_THREAD();
  if (NativeDragDiagnosticEnabled())
    LOG(WARNING) << "CGWA_NATIVE_DRAG_DIAG stage=try id="
                 << (button ? button->GetID() : -1) << " count=" << tabs_.size()
                 << " closing=" << closing_ << " active=" << tab_drag_.pressed;
  const bool guarded = !button || tab_drag_.pressed || closing_ ||
      !window_ || window_->IsClosed() || tabs_.size() < 2;
  if (NativeDragDiagnosticEnabled())
    LOG(WARNING) << "CGWA_NATIVE_DRAG_DIAG stage=pre_guard blocked=" << guarded
                 << " hasWindow=" << static_cast<bool>(window_)
                 << " windowClosed=" << (window_ ? window_->IsClosed() : true);
  if (guarded) return;
  // On X11 the native root coordinates equal CEF DIPs only at scale 1.
  // Fail closed at fractional-scale Wayland/Xwayland instead of misdropping.
#if defined(__linux__)
  const auto display = window_->GetDisplay();
  if (NativeDragDiagnosticEnabled())
    LOG(WARNING) << "CGWA_NATIVE_DRAG_DIAG stage=display present="
                 << static_cast<bool>(display) << " scale="
                 << (display ? display->GetDeviceScaleFactor() : 0.0f);
  if (!display || display->GetDeviceScaleFactor() != 1.0f) return;
#endif
  const int source_id = button->GetID() - kTabButtonBase;
  const Tab* tab = FindTabById(source_id);
  // CefRefPtr wrapper identity is not CEF View identity. The hosted X11
  // press callback and tracked title had matching stable IDs but different
  // wrapper pointers. Keep fail-closed same-underlying-view verification.
  const bool same_title_view = tab && tab->title_button &&
                               tab->title_button->IsSame(button);
  if (NativeDragDiagnosticEnabled())
    LOG(WARNING) << "CGWA_NATIVE_DRAG_DIAG stage=source source=" << source_id
                 << " found=" << static_cast<bool>(tab)
                 << " closing=" << (tab ? tab->closing : true)
                 << " sameCEFView=" << same_title_view;
  if (!tab || tab->closing || !same_title_view) return;
  CefPoint pointer;
  bool held = false;
  const bool sampled = ReadNativeDragPointer(&pointer, &held);
  const CefRect r = button->GetBoundsInScreen();
  const bool hit = sampled && rc_ui::TabTitleContains(
      pointer.x, pointer.y, r.x, r.y, r.width, r.height);
  if (NativeDragDiagnosticEnabled())
    LOG(WARNING) << "CGWA_NATIVE_DRAG_DIAG stage=press_sample ok=" << sampled
                 << " held=" << held << " hit=" << hit << " pointer="
                 << pointer.x << "," << pointer.y << " bounds=" << r.x << ","
                 << r.y << "," << r.width << "," << r.height;
  if (!sampled || !held || !hit) return;
  if (!tab_drag_.Begin(source_id, pointer.x, pointer.y)) return;
  CefPostDelayedTask(TID_UI, base::BindOnce([]() {
    if (auto* owner = AppController::Get()) owner->PollTabDrag();
  }), 20);
#else
  (void)button;  // Unsupported platform: native gesture disabled.
#endif
}

int AppController::TabUnderPointer(const CefPoint& point) const {
  for (const auto& tab : tabs_) {
    if (tab.closing || !tab.title_button || !tab.title_button->IsVisible())
      continue;
    const CefRect r = tab.title_button->GetBoundsInScreen();
    if (rc_ui::TabTitleContains(point.x, point.y, r.x, r.y,
                                r.width, r.height))
      return tab.id;
  }
  return -1;
}

void AppController::PollTabDrag() {
#if defined(_WIN32) || defined(__linux__)
  CEF_REQUIRE_UI_THREAD();
  if (!tab_drag_.pressed) return;
  if (closing_ || !window_ || window_->IsClosed()) {
    tab_drag_.Cancel(); return;
  }
  CefPoint pointer;
  bool held = false;
  if (!ReadNativeDragPointer(&pointer, &held)) {
    tab_drag_.Cancel(); return;
  }
  if (held) {
    tab_drag_.Observe(pointer.x, pointer.y);
    CefPostDelayedTask(TID_UI, base::BindOnce([]() {
      if (auto* owner = AppController::Get()) owner->PollTabDrag();
    }), 20);
    return;
  }
  const int target_id = TabUnderPointer(pointer);
  if (NativeDragDiagnosticEnabled())
    LOG(WARNING) << "CGWA_NATIVE_DRAG_DIAG stage=release source="
                 << tab_drag_.source_id << " target=" << target_id
                 << " moved=" << tab_drag_.moved;
  const int source_id = tab_drag_.Complete(target_id);
  if (source_id < 0 || !rc_ui::MoveTabTo(tabs_, source_id, target_id))
    return;  // Cancelled gesture, outside strip, or closed tab: no mutation.
  // Defer until native button callback has exited; keep BrowserViews unchanged.
  SetActiveTabInternal(source_id);
  LOG(WARNING) << "CGWA_NATIVE_TAB_DRAG_REORDER source=" << source_id
               << " target=" << target_id;
#else
  tab_drag_.Cancel();
#endif
}

void AppController::MoveActiveTab(int direction) {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || !window_ || window_->IsClosed()) return;
  // Only reorder references. Never re-create the BrowserView, request context,
  // overlay, profile, cookies or logged-in session.
  if (!rc_ui::ShiftActiveTab(tabs_, active_tab_id_, direction)) return;
  RebuildTabStrip();
  SaveSessionState();  // Persists the user-selected tab order and active index.
  LOG(WARNING) << "CGWA_TAB_REORDER id=" << active_tab_id_
               << " direction=" << direction;
}

void AppController::ToggleTheme() {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || !window_ || window_->IsClosed()) return;
  const auto next = g_ui_theme == rc_ui::ThemeMode::Light
                        ? rc_ui::ThemeMode::Dark : rc_ui::ThemeMode::Light;
  if (!SaveThemePreference(next)) {
    LOG(ERROR) << "CGWA_THEME_SAVE_FAILED";
    return;  // Do not advertise an unpersisted visual choice.
  }
  g_ui_theme = next;
  ApplyChromeColorScheme();
  ApplyNativeTheme();
  LOG(WARNING) << "CGWA_THEME_APPLIED "
               << (g_ui_theme == rc_ui::ThemeMode::Dark ? "DARK" : "LIGHT");
}

void AppController::ApplyNativeTheme() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || window_->IsClosed()) return;
  window_->SetBackgroundColor(UiPalette().header);
  if (header_) header_->SetBackgroundColor(UiPalette().header);
  if (tab_strip_) tab_strip_->SetBackgroundColor(UiPalette().header);
  if (content_) content_->SetBackgroundColor(UiPalette().content);
  if (companion_panel_) companion_panel_->SetBackgroundColor(UiPalette().header);
  if (theme_button_) {
    const bool dark = g_ui_theme == rc_ui::ThemeMode::Dark;
    theme_button_->SetText(dark ? "Dark" : "Light");
    theme_button_->SetTooltipText(
        dark ? "Dark appearance. Click to switch to Light."
             : "Light appearance. Click to switch to Dark.");
    theme_button_->SetAccessibleName(
        dark ? "Switch browser theme (Dark selected)"
             : "Switch browser theme (Light selected)");
  }
  for (const auto& button : header_buttons_)
    if (button) OnButtonStateChanged(button);
  for (const auto& row : companion_rows_) {
    if (row) {
      row->SetBackgroundColor(UiPalette().header);
      row->SetEnabledTextColors(UiPalette().text);
    }
  }
  RebuildTabStrip();
  UpdateCompanionPanel();
  window_->Layout();
  UpdateDraggableRegions();
}

void AppController::ToggleFullscreen() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_) return;
  fullscreen_ = !window_->IsFullscreen();
  if (header_) header_->SetVisible(!fullscreen_);
  window_->SetFullscreen(fullscreen_);
  window_->Layout();
  LayoutTabOverlays();
  UpdateDraggableRegions();
}

void AppController::ToggleMaximize() {
  if (!window_) return;
  if (window_->IsMaximized()) window_->Restore(); else window_->Maximize();
}

void AppController::Minimize() {
  if (window_) window_->Minimize();
}

void AppController::BeginShutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (closing_) return;
  tab_drag_.Cancel();

  SaveSessionState();
  closing_ = true;
  companion_visible_ = false;
  if (companion_overlay_ && companion_overlay_->IsValid()) companion_overlay_->Destroy();
  companion_overlay_ = nullptr;
  LOG(WARNING) << "CGWA_SHUTDOWN BEGIN tabs=" << tabs_.size();

  std::vector<CefRefPtr<CefOverlayController>> overlays;
  for (auto& tab : tabs_) {
    tab.closing = true;
    if (tab.browser_id <= 0 && tab.view && tab.view->GetBrowser())
      tab.browser_id = tab.view->GetBrowser()->GetIdentifier();
    if (tab.overlay && tab.overlay->IsValid())
      overlays.push_back(tab.overlay);
    tab.overlay = nullptr;
    tab.view = nullptr;
  }

  // Destroying an overlay that has a live BrowserView causes the browser to
  // complete its normal OnBeforeClose lifecycle. Tabs without a created
  // browser can be removed immediately because no callback will arrive.
  for (auto& overlay : overlays)
    if (overlay && overlay->IsValid()) overlay->Destroy();

  tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(),
                             [](const Tab& t) { return t.browser_id <= 0; }),
              tabs_.end());

  if (tabs_.empty())
    CefPostTask(TID_UI, base::BindOnce(&AppController::FinalizeShutdown,
                                       base::Unretained(this)));
}

void AppController::FinalizeShutdown() {
  CEF_REQUIRE_UI_THREAD();
  if (!closing_ || !tabs_.empty()) return;
  LOG(WARNING) << "CGWA_SHUTDOWN FINALIZE";
  if (window_ && !window_->IsClosed())
    window_->Close();
  else
    CefQuitMessageLoop();
}

void AppController::RequestCloseWindow() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_) return;
  if (!closing_) {
    BeginShutdown();
    return;
  }
  if (tabs_.empty()) FinalizeShutdown();
}

bool AppController::CanWindowClose() {
  CEF_REQUIRE_UI_THREAD();
  if (!closing_) {
    BeginShutdown();
    return false;
  }
  return tabs_.empty();
}

bool AppController::OnAccelerator(int command_id) {
  CEF_REQUIRE_UI_THREAD();
  if (command_id == kAccelNewTab) { NewTab(); return true; }
  if (command_id == kAccelCloseTab) { CloseActiveTab(); return true; }
  if (command_id == kAccelNextTab) { CycleTab(1); return true; }
  if (command_id == kAccelPrevTab) { CycleTab(-1); return true; }
  if (command_id == kAccelMoveTabLeft) { MoveActiveTab(-1); return true; }
  if (command_id == kAccelMoveTabRight) { MoveActiveTab(1); return true; }
  if (command_id == kAccelFullscreen) { ToggleFullscreen(); return true; }
  if (command_id == kAccelCompanion) { ToggleCompanionPanel(); return true; }
  if (command_id >= kAccelTab1 && command_id < kAccelTab1 + 8) {
    const int index = command_id - kAccelTab1;
    if (index < static_cast<int>(tabs_.size())) SetActiveTabInternal(tabs_[index].id);
    return true;
  }
  return false;
}

void AppController::OnButtonStateChanged(CefRefPtr<CefButton> button) {
  CEF_REQUIRE_UI_THREAD();
  if (!button) return;
  const int id = button->GetID();
  if (id >= kCompanionRowBase && id < kCompanionRowBase + kCompanionRowCount) {
    button->SetBackgroundColor(UiPalette().header);
    if (auto label = button->AsLabelButton()) label->SetEnabledTextColors(UiPalette().text);
    return;
  }
  const auto state = button->GetState();
#if defined(_WIN32) || defined(__linux__)
  if (id >= kTabButtonBase && id < kTabCloseBase &&
      NativeDragDiagnosticEnabled())
    LOG(WARNING) << "CGWA_NATIVE_DRAG_DIAG stage=cef_button_state id="
                 << id << " state=" << static_cast<int>(state);
#endif
  if (id >= kTabButtonBase && id < kTabCloseBase &&
      state == CEF_BUTTON_STATE_PRESSED) BeginTabDrag(button);
  const bool hot = state == CEF_BUTTON_STATE_HOVERED ||
                   state == CEF_BUTTON_STATE_PRESSED;

  cef_color_t bg = hot ? UiPalette().buttonHover : UiPalette().button;
  cef_color_t fg = hot ? UiPalette().text : UiPalette().muted;

  if (id == kBrandButton) {
    bg = UiPalette().header;
    fg = UiPalette().text;
  } else if (id == kNewTabButton || id == kTabSwitcherButton) {
    bg = hot ? UiPalette().tabHover : UiPalette().softAccent;
    fg = UiPalette().activeText;
  } else if (id == kThemeButton) {
    bg = hot ? UiPalette().tabHover : UiPalette().softAccent;
    fg = UiPalette().activeText;
  } else if (id == kCloseWindowButton) {
    bg = hot ? UiPalette().dangerHover : UiPalette().danger;
    fg = hot ? UiPalette().dangerText : UiPalette().muted;
  } else if (id >= kTabCloseBase) {
    const int tab_id = id - kTabCloseBase;
    const bool active = tab_id == active_tab_id_;
    bg = hot ? UiPalette().dangerHover : (active ? UiPalette().tabActive : UiPalette().tab);
    fg = hot ? UiPalette().dangerText : UiPalette().muted;
  } else if (id >= kTabButtonBase) {
    const int tab_id = id - kTabButtonBase;
    const bool active = tab_id == active_tab_id_;
    bg = hot ? UiPalette().tabHover : (active ? UiPalette().tabActive : UiPalette().tab);
    fg = active ? UiPalette().activeText : (hot ? UiPalette().text : UiPalette().muted);
  }

  button->SetBackgroundColor(bg);
  if (auto label = button->AsLabelButton())
    label->SetEnabledTextColors(fg);
}

void AppController::OnButtonPressed(CefRefPtr<CefButton> button) {
  CEF_REQUIRE_UI_THREAD();
  const int id = button->GetID();
  if (id == kBrandButton) return;
  if (id == kNewTabButton) { NewTab(); return; }
  if (id == kTabSwitcherButton) { CycleTab(1); return; }
  if (id == kMoveTabLeftButton) { MoveActiveTab(-1); return; }
  if (id == kMoveTabRightButton) { MoveActiveTab(1); return; }
  if (id == kCompanionButton) { ToggleCompanionPanel(); return; }
  if (id == kThemeButton) { ToggleTheme(); return; }
  if (id == kCompanionRefreshButton) { RefreshCompanion(); return; }
  if (id == kFullscreenButton) { ToggleFullscreen(); return; }
  if (id == kMaximizeButton) { ToggleMaximize(); return; }
  if (id == kMinimizeButton) { Minimize(); return; }
  if (id == kCloseWindowButton) { RequestCloseWindow(); return; }
  if (id >= kTabCloseBase) { QueueTabClose(id - kTabCloseBase); return; }
  if (id >= kTabButtonBase) { ActivateTab(id - kTabButtonBase); return; }
}

void AppController::RebuildTabStrip() {
  if (!tab_strip_) return;
  tab_strip_->SetBackgroundColor(UiPalette().header);
  tab_strip_->RemoveAllChildViews();
  int active_index = 0;
  for (size_t i = 0; i < tabs_.size(); ++i)
    if (tabs_[i].id == active_tab_id_) active_index = static_cast<int>(i);
  const int logical_width = window_ ? window_->GetClientAreaBoundsInScreen().width : 1240;
  const auto visible = rc_ui::SelectTabs(logical_width,
                                         static_cast<int>(tabs_.size()), active_index);
  visible_tab_capacity_ = visible.capacity;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    auto& tab = tabs_[i];
    const bool in_strip = static_cast<int>(i) >= visible.first &&
                          static_cast<int>(i) < visible.first + visible.visible;
    if (!in_strip) {
      tab.ui_panel = nullptr;
      tab.title_button = nullptr;
      tab.close_button = nullptr;
      continue;
    }
    auto item = CefPanel::CreatePanel(nullptr);
    CefBoxLayoutSettings settings;
    settings.horizontal = true;
    settings.between_child_spacing = 1;
    settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    item->SetToBoxLayout(settings);
    item->SetBackgroundColor(UiPalette().header);

    auto title = CefLabelButton::CreateLabelButton(button_delegate_, ShortTitle(tab.title));
    title->SetID(kTabButtonBase + tab.id);
    title->SetBackgroundColor(tab.id == active_tab_id_ ? UiPalette().tabActive : UiPalette().tab);
    title->SetEnabledTextColors(tab.id == active_tab_id_ ? UiPalette().activeText : UiPalette().muted);
    title->SetFontList(tab.id == active_tab_id_ ? kUIBoldFont : kUIFont);
    title->SetInkDropEnabled(true);
    title->SetTooltipText(tab.title);
    title->SetAccessibleName("Activate tab " + std::to_string(i + 1) +
                             ": " + tab.title);

    auto close = CefLabelButton::CreateLabelButton(button_delegate_, "×");
    close->SetID(kTabCloseBase + tab.id);
    close->SetBackgroundColor(tab.id == active_tab_id_ ? UiPalette().tabActive : UiPalette().tab);
    close->SetEnabledTextColors(UiPalette().muted);
    close->SetFontList(kUIFont);
    close->SetTooltipText("Close tab");
    close->SetAccessibleName("Close tab " + std::to_string(i + 1));
    close->SetInkDropEnabled(true);

    item->AddChildView(title);
    item->AddChildView(close);
    tab.ui_panel = item;
    tab.title_button = title;
    tab.close_button = close;
    tab_strip_->AddChildView(item);
  }
  if (tab_switcher_button_) {
    const auto label = tabs_.empty() ? "0 / 0" :
        std::to_string(active_index + 1) + " / " + std::to_string(tabs_.size());
    tab_switcher_button_->SetText(label);
    tab_switcher_button_->SetTooltipText(
        "Tab " + label + " — click for next, Ctrl+1..8 for direct selection");
    tab_switcher_button_->SetAccessibleName(
        "Tab " + label + "; activate next tab");
  }
  tab_strip_->Layout();
  if (header_) {
    header_->Layout();
    UpdateDraggableRegions();
  }
}

void AppController::UpdateTabButton(Tab& tab) {
  if (!tab.title_button) return;
  std::string text = ShortTitle(tab.title);
  if (tab.loading) text = "• " + text;
  tab.title_button->SetText(text);
  const bool active = tab.id == active_tab_id_;
  tab.title_button->SetBackgroundColor(active ? UiPalette().tabActive : UiPalette().tab);
  tab.title_button->SetEnabledTextColors(active ? UiPalette().activeText : UiPalette().muted);
  tab.title_button->SetFontList(active ? kUIBoldFont : kUIFont);
  if (tab.close_button) {
    tab.close_button->SetBackgroundColor(active ? UiPalette().tabActive : UiPalette().tab);
    tab.close_button->SetEnabledTextColors(UiPalette().muted);
  }
}

AppController::Tab* AppController::FindTabById(int tab_id) {
  const auto it = std::find_if(tabs_.begin(), tabs_.end(),
                               [tab_id](const Tab& tab) { return tab.id == tab_id; });
  return it == tabs_.end() ? nullptr : &(*it);
}

AppController::Tab* AppController::FindTabByBrowser(CefRefPtr<CefBrowser> browser) {
  if (!browser) return nullptr;
  const int bid = browser->GetIdentifier();
  for (auto& tab : tabs_) {
    if (tab.browser_id == bid) return &tab;
    if (tab.view && tab.view->GetBrowser() &&
        tab.view->GetBrowser()->GetIdentifier() == bid) return &tab;
  }
  return nullptr;
}

void AppController::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  LOG(WARNING) << "CGWA_LIFE ControllerCreated browser=" << browser->GetIdentifier();
  Tab* tab = FindTabByBrowser(browser);
  if (!tab) {
    for (auto& candidate : tabs_) {
      if (candidate.browser_id < 0 && candidate.view &&
          candidate.view->GetBrowser() &&
          candidate.view->GetBrowser()->GetIdentifier() == browser->GetIdentifier()) {
        tab = &candidate;
        break;
      }
    }
  }
  if (tab) tab->browser_id = browser->GetIdentifier();
}

bool AppController::OnBrowserDoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  LOG(WARNING) << "CGWA_LIFE DoClose passthrough browser="
               << (browser ? browser->GetIdentifier() : -1);
  return false;
}

void AppController::OnBrowserClosed(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  const int bid = browser ? browser->GetIdentifier() : -1;
  LOG(WARNING) << "CGWA_LIFE ControllerClosed browser=" << bid
               << " total_before=" << tabs_.size();

  auto it = std::find_if(tabs_.begin(), tabs_.end(),
                         [bid](const Tab& t) { return t.browser_id == bid; });
  if (it == tabs_.end()) return;

  const bool was_active = it->id == active_tab_id_;
  const size_t index = static_cast<size_t>(std::distance(tabs_.begin(), it));
  native_chat_observer_.Closed(it->id);
  tabs_.erase(it);
  if (closing_) {
    LOG(WARNING) << "CGWA_SHUTDOWN BROWSER_CLOSED remaining=" << tabs_.size();
    if (tabs_.empty())
      CefPostTask(TID_UI, base::BindOnce(&AppController::FinalizeShutdown,
                                         base::Unretained(this)));
    return;
  }

  RebuildTabStrip();
  if (tabs_.empty()) {
    NewTab();
  } else if (was_active) {
    const size_t next = std::min(index, tabs_.size() - 1);
    SetActiveTabInternal(tabs_[next].id);
  } else {
    SetActiveTabInternal(active_tab_id_);
  }
}

void AppController::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const std::string& title) {
  CEF_REQUIRE_UI_THREAD();
  Tab* tab = FindTabByBrowser(browser);
  if (!tab) return;
  tab->title = title.empty() ? "ChatGPT" : title;
  UpdateTabButton(*tab);
}

void AppController::OnNativeAddressChange(CefRefPtr<CefBrowser> browser,
                                           const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  Tab* tab = FindTabByBrowser(browser);
  if (!tab) return;
  native_chat_observer_.Observe(tab->id, url);
  if (tab->id == active_tab_id_) UpdateCompanionPanel();
}

void AppController::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                         bool is_loading) {
  CEF_REQUIRE_UI_THREAD();
  Tab* tab = FindTabByBrowser(browser);
  if (!tab) return;
  tab->loading = is_loading;
  if (browser && browser->GetMainFrame()) {
    const std::string url = browser->GetMainFrame()->GetURL().ToString();
    if (IsChatGPTURL(url)) tab->url = url;
  }
  UpdateTabButton(*tab);
  if (!is_loading) SaveSessionState();
  if (tab->id == active_tab_id_) UpdateCompanionPanel();
}

bool AppController::IsChatGPTURL(const std::string& url) const {
  return IsHttpsHost(url, "chatgpt.com");
}

bool AppController::IsInternalNavigationURL(const std::string& url) const {
  return IsChatGPTURL(url) || IsHttpsHost(url, "auth.openai.com");
}

bool AppController::HandlePopupURL(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  if (url.empty()) return true;
  if (IsInternalNavigationURL(url)) NewTab(url);
  else OpenExternal(url);
  return true;
}

bool AppController::OpenExternal(const std::string& url) {
  if (!(StartsWith(url, "https://") || StartsWith(url, "http://"))) return false;
#if defined(_WIN32)
  const std::wstring wide = CefString(url).ToWString();
  const auto result = reinterpret_cast<INT_PTR>(
      ::ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return result > 32;
#else
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    pid_t grandchild = fork();
    if (grandchild == 0) {
      setsid();
      execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }
    _exit(grandchild < 0 ? 127 : 0);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

void AppController::OnWindowDestroyed() {
  CEF_REQUIRE_UI_THREAD();
  tab_drag_.Cancel();
  LOG(WARNING) << "CGWA_SHUTDOWN WINDOW_DESTROYED";
  tabs_.clear();
  window_ = nullptr;
  header_ = nullptr;
  tab_strip_ = nullptr;
  content_ = nullptr;
  theme_button_ = nullptr;
  companion_overlay_ = nullptr;
  companion_panel_ = nullptr;
  companion_rows_.clear();
  companion_visible_ = false;
  companion_tick_scheduled_ = false;
  window_layout_ = nullptr;
  header_layout_ = nullptr;
  tab_layout_ = nullptr;
  window_created_ = false;
  CefQuitMessageLoop();
}

std::string AppController::ShortTitle(const std::string& title) const {
  std::string s = title.empty() ? "ChatGPT" : title;
  const std::string suffix1 = " - ChatGPT";
  if (s.size() > suffix1.size() &&
      s.compare(s.size() - suffix1.size(), suffix1.size(), suffix1) == 0)
    s.resize(s.size() - suffix1.size());
  return Utf8Ellipsize(s, 28, 25);
}
