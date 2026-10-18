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
#include <array>
#include <cmath>
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
#include "first_party_home.h"
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
constexpr int kTabsOrientationButton = 31;
constexpr int kSidebarWidth = 246;
constexpr int kSidebarCompactWidth = 54;
constexpr int kSidebarToggleButton = 32;
constexpr int kSidebarNewTabButton = 33;
constexpr int kSidebarOrientationButton = 34;
constexpr int kSidebarCompanionButton = 35;
// Sidebar and header share coordinated light/dark color roles.
constexpr int kSidebarMinWindowWidth = 550;
constexpr int kCompanionRowBase = 300;
constexpr int kCompanionRowCount = 18;
constexpr int kCompanionWidth = 360;
constexpr int kTabButtonBase = 1000;
constexpr int kTabCloseBase = 2000;

constexpr int kHeaderHeight = 52;
constexpr int kBrandWidth = 108;

constexpr int kAccelNewTab = 100;
constexpr int kAccelCloseTab = 101;
constexpr int kAccelNextTab = 102;
constexpr int kAccelPrevTab = 103;
constexpr int kAccelFullscreen = 104;
constexpr int kAccelCompanion = 105;
constexpr int kAccelMoveTabLeft = 106;
constexpr int kAccelMoveTabRight = 107;
constexpr int kAccelTab1 = 111;
constexpr int kAccelOrientation = 119;
constexpr int kAccelExitFullscreen = 120;

constexpr int kVKeyTab = 0x09;
constexpr int kVKey1 = 0x31;
constexpr int kVKeyT = 0x54;
constexpr int kVKeyW = 0x57;
constexpr int kVKeyC = 0x43;
constexpr int kVKeyE = 0x45;
constexpr int kVKeyF11 = 0x7A;
constexpr int kVKeyEscape = 0x1B;
constexpr int kVKeyPageUp = 0x21;
constexpr int kVKeyPageDown = 0x22;

// R98 user-selected private browser theme, updated only on the CEF UI thread.
rc_ui::ThemeMode g_ui_theme = rc_ui::ThemeMode::Dark;
const rc_ui::ThemePalette& UiPalette() { return rc_ui::Palette(g_ui_theme); }
const rc_ui::SidebarPalette& UiSidebarPalette() {
  return rc_ui::Sidebar(g_ui_theme);
}

constexpr char kUIFont[] = "Vazirmatn, Inter, Noto Sans Arabic, DejaVu Sans, 13px";
constexpr char kUIBoldFont[] = "Vazirmatn, Inter, Noto Sans Arabic, DejaVu Sans, Bold 13px";
constexpr char kCompactTabFont[] = "DejaVu Sans, Bold 15px";

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

std::string TabsOrientationStatePath() {
  return PathUtf8(std::filesystem::u8path(StatePath()).parent_path() /
                  "ui-tabs-orientation.state");
}

bool ReadVerticalTabsPreference() {
  const auto path = std::filesystem::u8path(TabsOrientationStatePath());
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(path, ec);
  // New ChatGPT workspaces show the native conversation rail by default.
  // Existing users' saved horizontal/vertical preference is authoritative.
  if (ec == std::errc::no_such_file_or_directory) return true;
  if (ec || !std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status)) return false;
  std::ifstream input(path);
  std::string value;
  return std::getline(input, value) && value == "vertical";
}

bool SaveVerticalTabsPreference(bool vertical) {
  const std::string path = TabsOrientationStatePath(), temp = path + ".tmp";
  std::error_code ec;
  const auto parent = std::filesystem::u8path(path).parent_path();
  std::filesystem::create_directories(parent, ec);
  if (ec) return false;
  const auto safe = [](const std::string& name, bool must_be_absent) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(
        std::filesystem::u8path(name), error);
    if (error == std::errc::no_such_file_or_directory) return true;
    if (error) return false;
    if (!std::filesystem::exists(status)) return true;
    if (std::filesystem::is_symlink(status)) return false;
    return !must_be_absent && std::filesystem::is_regular_file(status);
  };
  if (!safe(path, false) || !safe(temp, true)) return false;
  std::ofstream output(std::filesystem::u8path(temp), std::ios::trunc);
  if (!output) return false;
  output << (vertical ? "vertical\n" : "horizontal\n");
  output.flush();
  const bool valid = output.good();
  output.close();
  if (!valid || !CommitStateFile(temp, path)) {
    RemoveStateFile(temp);
    return false;
  }
  return true;
}

std::string SidebarCompactStatePath() {
  return PathUtf8(std::filesystem::u8path(StatePath()).parent_path() /
                  "ui-tabs-compact.state");
}
bool ReadSidebarCompactPreference() {
  const auto path = std::filesystem::u8path(SidebarCompactStatePath());
  std::error_code error;
  const auto stat = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_regular_file(stat) ||
      std::filesystem::is_symlink(stat)) return false;
  std::ifstream input(path);
  std::string mode;
  return std::getline(input, mode) && mode == "compact";
}
bool SaveSidebarCompactPreference(bool compact) {
  const std::string path = SidebarCompactStatePath();
  const std::string temp = path + ".tmp";
  std::error_code error;
  std::filesystem::create_directories(
      std::filesystem::u8path(path).parent_path(), error);
  if (error) return false;
  auto safe = [](const std::string& name, bool absent) {
    std::error_code ec;
    const auto stat = std::filesystem::symlink_status(
        std::filesystem::u8path(name), ec);
    if (ec == std::errc::no_such_file_or_directory) return true;
    if (ec) return false;
    if (std::filesystem::is_symlink(stat)) return false;
    if (!std::filesystem::exists(stat)) return true;
    return !absent && std::filesystem::is_regular_file(stat);
  };
  if (!safe(path, false) || !safe(temp, true)) return false;
  std::ofstream out(std::filesystem::u8path(temp), std::ios::trunc);
  if (!out) return false;
  out << (compact ? "compact\n" : "expanded\n");
  out.flush();
  const bool valid = out.good();
  out.close();
  if (!valid || !CommitStateFile(temp, path)) {
    RemoveStateFile(temp);
    return false;
  }
  return true;
}

std::string ThemeStatePath() {
  return PathUtf8(std::filesystem::u8path(StatePath()).parent_path() /
                  "ui-theme.state");
}

rc_ui::ThemeMode ReadThemePreference() {
  std::ifstream input(std::filesystem::u8path(ThemeStatePath()));
  std::string value;
  if (std::getline(input, value)) {
    if (value == "light") return rc_ui::ThemeMode::Light;
    if (value == "dark") return rc_ui::ThemeMode::Dark;
  }
  // Default for new profiles only; never overwrite an existing saved choice.
  return rc_ui::ThemeMode::Dark;
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

// Overlay-hosted native panels need a theme-restoring delegate so Chromium's
// theme changes cannot reset the unused sidebar surface to the OS white.
class AppController::SidebarSurfaceDelegate : public CefPanelDelegate {
 public:
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    if (view) view->SetBackgroundColor(UiSidebarPalette().background);
  }
  IMPLEMENT_REFCOUNTING(SidebarSurfaceDelegate);
};

// Unused native chrome areas must paint a palette color instead of showing
// CEF's default black surface, even after theme/fullscreen transitions.
class AppController::HeaderSurfaceDelegate : public CefPanelDelegate {
 public:
  void OnThemeChanged(CefRefPtr<CefView> view) override {
    if (view) view->SetBackgroundColor(UiPalette().header);
  }
  IMPLEMENT_REFCOUNTING(HeaderSurfaceDelegate);
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
    // Preserve recognizable ChatGPT identity down to 650 DIP, not an empty
    // leading header. The icon-only brand is 44 DIP and costs no extra View.
    if (id == kBrandButton)
      return CefSize(owner_ && owner_->narrow_toolbar_cached_ ? 44 : kBrandWidth, 36);
    if (id == kCompanionButton)
      return CefSize(owner_ && owner_->compact_toolbar_cached_ ? 40 : 112, 36);
    if (id == kTabsOrientationButton) return CefSize(42, 36);
    if (id == kSidebarToggleButton || id == kSidebarNewTabButton ||
        id == kSidebarOrientationButton || id == kSidebarCompanionButton)
      return CefSize(owner_ && owner_->SidebarCompactEffective() ? 34 : 214, 36);
    if (id == kThemeButton) return CefSize(42, 36);
    // Native ChatGPT quick action: readable when wide, 40-DIP icon on narrow.
    if (id == kNewTabButton)
      return CefSize(owner_ && owner_->compact_toolbar_cached_ ? 40 : 116, 36);
    if (id == kTabSwitcherButton) return CefSize(60, 36);
    if (id == kMoveTabLeftButton || id == kMoveTabRightButton)
      return CefSize(34, 36);
    if (id == kCompanionRefreshButton) return CefSize(kCompanionWidth - 24, 36);
    if (id >= kCompanionRowBase && id < kCompanionRowBase + kCompanionRowCount)
      return CefSize(kCompanionWidth - 24, 30);
    if (id >= kTabCloseBase) return CefSize(28, 36);
    if (id >= kTabButtonBase)
      return CefSize(owner_ && owner_->VerticalTabsVisible()
                         ? (owner_->SidebarCompactEffective() ? 34 : 184)
                         : 135, 36);
    if (id >= kNewTabButton && id <= kCloseWindowButton) return CefSize(34, 36);
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
  header_surface_delegate_ = new HeaderSurfaceDelegate();
  g_ui_theme = ReadThemePreference();
  vertical_tabs_ = ReadVerticalTabsPreference();
  sidebar_compact_ = ReadSidebarCompactPreference();
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
  window_->SetTitle("ChatGPT Commander");
  ApplyChromeColorScheme();
  SetX11WindowClass(window_);
  // CefCommandLine is available at window creation, not in AppController's
  // pre-initialization constructor. Only brand-new opt-in Home profiles use
  // this quiet compact rail; explicit saved choices and normal launches win.
  const auto preview_cmd = CefCommandLine::GetGlobalCommandLine();
  if (preview_cmd && preview_cmd->HasSwitch("rc-home-preview")) {
    std::error_code ec;
    const auto pref = std::filesystem::u8path(SidebarCompactStatePath());
    const auto status = std::filesystem::symlink_status(pref, ec);
    if (ec == std::errc::no_such_file_or_directory ||
        (!ec && !std::filesystem::exists(status)))
      sidebar_compact_ = true;
  }
  BuildWindowUI();
  window_->CenterWindow(CefSize(1240, 760));
  // Explicit local QA viewport. Never change a normal or opted-in user's
  // window size unless they also requested this narrow test dimension.
  const auto preview_line = CefCommandLine::GetGlobalCommandLine();
  if (preview_line && preview_line->HasSwitch("rc-home-preview") &&
      preview_line->HasSwitch("rc-home-narrow-test"))
    window_->SetBounds(CefRect(100, 80, 650, 700));
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
  // Preview opt-in never discards existing saved ChatGPT URLs. A fresh
  // disposable profile gets the local Home; ordinary users still get ChatGPT.
  if (urls.empty())
    urls.push_back(command_line && command_line->HasSwitch("rc-home-preview") &&
                       rc_home::IsReady()
                       ? rc_home::kUrl : "https://chatgpt.com/");
  for (const auto& url : urls) NewTab(url);
  if (active_index >= 0 && active_index < static_cast<int>(tabs_.size()))
    SetActiveTabInternal(tabs_[active_index].id);

  if (command_line && command_line->HasSwitch("companion-self-test"))
    RunCompanionSelfTest();
  if (command_line && command_line->HasSwitch("vertical-tabs-self-test"))
    RunVerticalTabsSelfTest();
  if (command_line && command_line->HasSwitch("tab-lifetime-soak-test")) {
    if (!background_test) {
      LOG(ERROR) << "CGWA_LIFETIME_SOAK_REQUIRES_BACKGROUND";
    } else {
      CefPostDelayedTask(TID_UI, base::BindOnce([]() {
        if (auto* app = AppController::Get()) app->TabLifetimeSoakStep(0);
      }), 2500);
    }
  }
  if (command_line && command_line->HasSwitch("fullscreen-tabs-self-test")) {
    CefPostDelayedTask(TID_UI, base::BindOnce([]() {
      if (auto* app = AppController::Get()) app->RunFullscreenTabsSelfTest();
    }), 800);
  }
  if (command_line && command_line->HasSwitch("live-tab-counter-self-test")) {
    if (!background_test) {
      LOG(ERROR) << "CGWA_LIVE_TAB_COUNTER_REQUIRES_BACKGROUND";
    } else {
      CefPostDelayedTask(TID_UI, base::BindOnce([]() {
        if (auto* app = AppController::Get()) app->RunLiveTabCounterSelfTest();
      }), 1100);
    }
  }

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
  const bool use_vertical = VerticalTabsVisible();
  const auto current = use_vertical
      ? rc_ui::SelectVerticalTabs(
            new_bounds.height -
                ((header_ && header_->IsVisible()) ? kHeaderHeight : 0),
            static_cast<int>(tabs_.size()), active_index)
      : visible;
  if (current.capacity != visible_tab_capacity_ ||
      (new_bounds.width < 930) != compact_toolbar_cached_ ||
      (new_bounds.width < 690) != narrow_toolbar_cached_ ||
      (vertical_tab_strip_ && vertical_tab_strip_->IsVisible() != use_vertical))
    RebuildTabStrip();
  LayoutTabOverlays();
  UpdateDraggableRegions();
  if (companion_visible_) UpdateCompanionPanel();
}

void AppController::OnWindowFullscreenTransition(bool is_completed) {
  CEF_REQUIRE_UI_THREAD();
  if (!is_completed || !window_) return;
  fullscreen_ = window_->IsFullscreen();
  if (header_) header_->SetVisible(!fullscreen_ || !vertical_tabs_);
  RebuildTabStrip();
  window_->Layout();
  LayoutTabOverlays();
  UpdateDraggableRegions();
  const bool selectable =
      (vertical_tabs_ && sidebar_overlay_ && sidebar_overlay_->IsValid() &&
       sidebar_overlay_->IsVisible()) ||
      (!vertical_tabs_ && header_ && header_->IsVisible() &&
       tab_strip_ && tab_strip_->IsVisible());
  LOG(WARNING) << "CGWA_FULLSCREEN_LAYOUT active=" << fullscreen_
               << " vertical=" << vertical_tabs_
               << " tab_selector_visible=" << selectable;
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
  // Controls within the same native title bar should meet edge-to-edge.
  horizontal.between_child_spacing = 0;
  horizontal.inside_border_horizontal_spacing = 10;
  horizontal.inside_border_vertical_spacing = 4;
  horizontal.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
  horizontal.default_flex = 0;
  header_layout_ = header_->SetToBoxLayout(horizontal);

  tab_strip_ = CefPanel::CreatePanel(header_surface_delegate_);
  tab_strip_->SetBackgroundColor(UiPalette().header);
  CefBoxLayoutSettings tabs_layout;
  tabs_layout.horizontal = true;
  tabs_layout.between_child_spacing = 2;
  tabs_layout.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
  tabs_layout.default_flex = 0;
  tab_layout_ = tab_strip_->SetToBoxLayout(tabs_layout);

  // Native sidebar stays outside the web BrowserView overlays. It never
  // injects into ChatGPT content or touches the shared request context.
  sidebar_surface_delegate_ = new SidebarSurfaceDelegate();
  sidebar_ = CefPanel::CreatePanel(sidebar_surface_delegate_);
  sidebar_->SetBackgroundColor(UiSidebarPalette().background);
  CefBoxLayoutSettings sidebar_settings;
  sidebar_settings.horizontal = false;
  // Brave-inspired narrow rail: keep ample touch targets, less blank space.
  sidebar_settings.between_child_spacing = 5;
  sidebar_settings.inside_border_horizontal_spacing = 10;
  sidebar_settings.inside_border_vertical_spacing = 10;
  sidebar_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  sidebar_settings.default_flex = 0;
  sidebar_layout_ = sidebar_->SetToBoxLayout(sidebar_settings);
  vertical_tab_strip_ = CefPanel::CreatePanel(sidebar_surface_delegate_);
  vertical_tab_strip_->SetBackgroundColor(UiSidebarPalette().background);
  CefBoxLayoutSettings vertical_tabs;
  vertical_tabs.horizontal = false;
  vertical_tabs.between_child_spacing = 4;
  vertical_tabs.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  vertical_tabs.default_flex = 0;
  vertical_tab_strip_->SetToBoxLayout(vertical_tabs);
  auto sidebar_toggle = CefLabelButton::CreateLabelButton(
      button_delegate_, sidebar_compact_ ? "≡" : "≡  Workspace tabs");
  sidebar_toggle->SetID(kSidebarToggleButton);
  sidebar_toggle->SetBackgroundColor(UiSidebarPalette().background);
  sidebar_toggle->SetEnabledTextColors(UiSidebarPalette().text);
  sidebar_toggle->SetFontList(kUIBoldFont);
  sidebar_toggle->SetInkDropEnabled(false);
  sidebar_toggle->SetTooltipText("Expand / collapse vertical tabs");
  sidebar_toggle->SetAccessibleName("Collapse or expand tab sidebar");
  sidebar_toggle_button_ = sidebar_toggle;
  sidebar_->AddChildView(sidebar_toggle);
  auto sidebar_new = CefLabelButton::CreateLabelButton(
      button_delegate_, sidebar_compact_ ? "+" : "＋  New tab");
  sidebar_new->SetID(kSidebarNewTabButton);
  sidebar_new->SetBackgroundColor(UiPalette().softAccent);
  sidebar_new->SetEnabledTextColors(UiPalette().activeText);
  sidebar_new->SetFontList(kUIFont);
  sidebar_new->SetInkDropEnabled(false);
  sidebar_new->SetTooltipText("Open Browser tab (Ctrl+T); this does not start a new ChatGPT conversation");
  sidebar_new->SetAccessibleName("Open new Browser tab");
  sidebar_new_tab_button_ = sidebar_new;
  sidebar_->AddChildView(sidebar_new);
  // Fullscreen vertical mode hides the top header; orientation and Companion
  // must still be available by mouse on the LEFT native rail.
  auto sidebar_orientation = CefLabelButton::CreateLabelButton(
      button_delegate_, "↔  Horizontal tabs");
  sidebar_orientation->SetID(kSidebarOrientationButton);
  sidebar_orientation->SetBackgroundColor(UiSidebarPalette().background);
  sidebar_orientation->SetEnabledTextColors(UiSidebarPalette().text);
  sidebar_orientation->SetFontList(kUIFont);
  sidebar_orientation->SetInkDropEnabled(false);
  sidebar_orientation->SetTooltipText("Switch to horizontal tabs (Ctrl+Shift+E)");
  sidebar_orientation->SetAccessibleName("Use horizontal tabs");
  sidebar_orientation_button_ = sidebar_orientation;
  sidebar_->AddChildView(sidebar_orientation);
  auto sidebar_companion = CefLabelButton::CreateLabelButton(
      button_delegate_, "◈  Commander");
  sidebar_companion->SetID(kSidebarCompanionButton);
  sidebar_companion->SetBackgroundColor(UiPalette().softAccent);
  sidebar_companion->SetEnabledTextColors(UiPalette().activeText);
  sidebar_companion->SetFontList(kUIFont);
  sidebar_companion->SetInkDropEnabled(false);
  sidebar_companion->SetTooltipText("Toggle right-hand Commander panel (Ctrl+Shift+C)");
  sidebar_companion->SetAccessibleName("Toggle Commander on the right");
  sidebar_companion_button_ = sidebar_companion;
  sidebar_->AddChildView(sidebar_companion);
  sidebar_->AddChildView(vertical_tab_strip_);
  // An explicitly painted flexible native child fills the unused rail area.
  // Unlike a parent's transparent overlay, this keeps dark mode dark down
  // the *entire height* of the window, even after theme or F11 changes.
  sidebar_fill_ = CefPanel::CreatePanel(sidebar_surface_delegate_);
  sidebar_fill_->SetBackgroundColor(UiSidebarPalette().background);
  sidebar_->AddChildView(sidebar_fill_);
  sidebar_layout_->SetFlexForView(sidebar_fill_, 1);

  content_ = CefPanel::CreatePanel(nullptr);
  content_->SetToFillLayout();
  content_->SetBackgroundColor(UiPalette().content);

  window_->AddChildView(header_);
  window_->AddChildView(content_);
  window_layout_->SetFlexForView(content_, 1);
  BuildHeaderControls();
  sidebar_overlay_ = window_->AddOverlayView(
      sidebar_, CEF_DOCKING_MODE_CUSTOM, true);
  if (sidebar_overlay_ && sidebar_overlay_->IsValid())
    sidebar_overlay_->SetVisible(false);
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
  // Esc exits native fullscreen, even when a ChatGPT BrowserView has focus.
  window_->SetAccelerator(kAccelExitFullscreen, kVKeyEscape, false, false, false, true);
  window_->SetAccelerator(kAccelCompanion, kVKeyC, true, true, false, true);
  window_->SetAccelerator(kAccelOrientation, kVKeyE, true, true, false, true);
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
    button->SetInkDropEnabled(false);
    return button;
  };

  // Compact product mark; identity remains available to screen readers.
  auto brand = make_button(kBrandButton, "◈  RC Browser", UiPalette().header);
  brand->SetFontList(kUIBoldFont);
  brand->SetEnabledTextColors(UiPalette().text);
  brand->SetAccessibleName("Remote Commander Browser native workspace");
  brand->SetTooltipText("Remote Commander Browser — native tabs around ChatGPT");
  header_buttons_.push_back(brand);
  header_->AddChildView(brand);

  // The tab layout control is the first actionable header control on the
  // left, not hidden among Commander and window buttons on the right.
  auto orientation = make_button(
      kTabsOrientationButton, vertical_tabs_ ? "⇥" : "☰",
      UiPalette().softAccent);
  orientation->SetTooltipText(
      "Switch between horizontal and vertical native tab layouts");
  orientation->SetAccessibleName(
      vertical_tabs_ ? "Use horizontal tabs" : "Use vertical tabs");
  orientation_button_ = orientation;
  header_buttons_.push_back(orientation);
  header_->AddChildView(orientation);
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

  auto add = make_button(kNewTabButton, "＋ New tab", UiPalette().softAccent);
  add->SetEnabledTextColors(UiPalette().activeText);
  add->SetTooltipText("Open Browser tab (Ctrl+T); this does not start a new ChatGPT conversation");
  add->SetAccessibleName("Open new Browser tab");
  header_buttons_.push_back(add);
  header_->AddChildView(add);

  auto companion = make_button(kCompanionButton, "◈ Commander", UiPalette().primary);
  companion->SetEnabledTextColors(UiPalette().content);
  companion->SetTooltipText("Commander / پنل فرمانده (Ctrl+Shift+C)");
  companion->SetAccessibleName("Toggle read-only Commander companion");
  header_buttons_.push_back(companion);
  header_->AddChildView(companion);

  auto theme = make_button(
      kThemeButton, g_ui_theme == rc_ui::ThemeMode::Dark ? "☾" : "☼",
      UiPalette().softAccent);
  theme->SetTooltipText("Switch ChatGPT workspace appearance (private setting)");
  theme->SetAccessibleName("Switch ChatGPT workspace theme");
  theme_button_ = theme;
  header_buttons_.push_back(theme);
  header_->AddChildView(theme);

  // The trailing flexible region anchors window controls to the *right edge*
  // in both orientations. The hidden horizontal strip must not leave them
  // floating in the middle of the window.
  header_spacer_ = CefPanel::CreatePanel(header_surface_delegate_);
  header_spacer_->SetBackgroundColor(UiPalette().header);
  header_->AddChildView(header_spacer_);
  header_layout_->SetFlexForView(header_spacer_, 1);

  // Group the OS controls with zero gap; no stray separator between the
  // fullscreen, maximize, minimize and close surfaces.
  window_controls_ = CefPanel::CreatePanel(header_surface_delegate_);
  window_controls_->SetBackgroundColor(UiPalette().header);
  CefBoxLayoutSettings controls;
  controls.horizontal = true;
  controls.between_child_spacing = 0;
  controls.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
  window_controls_->SetToBoxLayout(controls);
  header_->AddChildView(window_controls_);

  auto full = make_button(kFullscreenButton, "⛶", UiPalette().button);
  full->SetTooltipText("Fullscreen (F11)");
  full->SetAccessibleName("Toggle fullscreen");
  header_buttons_.push_back(full);
  window_controls_->AddChildView(full);

  auto max = make_button(kMaximizeButton, "▢", UiPalette().button);
  max->SetTooltipText("Maximize / Restore");
  max->SetAccessibleName("Maximize or restore");
  header_buttons_.push_back(max);
  window_controls_->AddChildView(max);

  auto min = make_button(kMinimizeButton, "−", UiPalette().button);
  min->SetTooltipText("Minimize");
  min->SetAccessibleName("Minimize");
  header_buttons_.push_back(min);
  window_controls_->AddChildView(min);

  auto close = make_button(kCloseWindowButton, "×", UiPalette().danger);
  close->SetEnabledTextColors(UiPalette().muted);
  close->SetTooltipText("Close");
  close->SetAccessibleName("Close application");
  header_buttons_.push_back(close);
  window_controls_->AddChildView(close);
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
  if (window_controls_) window_controls_->Layout();

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
    if (!button || !button->IsVisible()) continue;
    const bool group = window_controls_ && button->GetParentView() &&
        button->GetParentView()->IsSame(window_controls_);
    if (group) {
      const CefRect parent = window_controls_->GetBounds();
      subtract(button->GetBounds(), header_bounds.x + parent.x,
               header_bounds.y + parent.y);
    } else {
      subtract(button->GetBounds(), header_bounds.x, header_bounds.y);
    }
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
  companion_panel_ = CefPanel::CreatePanel(header_surface_delegate_);
  companion_panel_->SetBackgroundColor(UiPalette().header);
  CefBoxLayoutSettings settings;
  settings.horizontal = false;
  settings.between_child_spacing = 2;
  settings.inside_border_horizontal_spacing = 12;
  settings.inside_border_vertical_spacing = 10;
  settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_STRETCH;
  companion_layout_ = companion_panel_->SetToBoxLayout(settings);
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
  // Paint the otherwise unoccupied Companion column to the bottom, not black.
  companion_fill_ = CefPanel::CreatePanel(header_surface_delegate_);
  companion_fill_->SetBackgroundColor(UiPalette().header);
  companion_panel_->AddChildView(companion_fill_);
  if (companion_layout_) companion_layout_->SetFlexForView(companion_fill_, 1);
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
  companion_binding_poll_ = 0;
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
  const auto automation_rows = companion::DescribeAutomation(companion_automation_, companion_monitor_, now);
  rows.insert(rows.end(), automation_rows.begin(), automation_rows.end());
  const auto binding_rows = companion::Describe(companion_observation_, companion_binding_, active_url, now);
  rows.insert(rows.end(), binding_rows.begin(), binding_rows.end());
  const int available_height =
      window_ ? window_->GetClientAreaBoundsInScreen().height -
                    ((header_ && header_->IsVisible()) ? kHeaderHeight : 0)
              : 568;
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
                         rows[i].text.find("BLOCKED") != std::string::npos ||
                         rows[i].text.find("UNCERTAIN") != std::string::npos;
    row->SetEnabledTextColors(caution ? UiPalette().dangerText : UiPalette().text);
  }
  companion_panel_->Layout();
}

void AppController::CompanionTick() {
  CEF_REQUIRE_UI_THREAD();
  companion_tick_scheduled_ = false;
  if (closing_ || !companion_visible_) return;
  const std::string root = companion::ProfileRoot();
  companion_monitor_ = companion::ReadMonitor(root);
  companion_automation_ = companion::ReadAutomation(root);
  // Monitor counters update every second, but chat/project identity can change
  // independently. Refresh that private binding evidence every third tick.
  if (++companion_binding_poll_ >= 3) {
    companion_binding_poll_ = 0;
    companion_observation_ = companion::ReadObservation(root);
    companion_binding_ = companion::ReadNativeBinding(root);
  }
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
    active->overlay->GetBounds().x + active->overlay->GetBounds().width == companion_overlay_->GetBounds().x &&
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
    << " vertical_tabs=" << VerticalTabsVisible()
    << " compact_rail=" << (VerticalTabsVisible() && SidebarCompactEffective())
    << " " << companion_check << "=" << companion_safe << " toggle_hidden=" << hidden;
}

void AppController::NewTab(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || tabs_.size() >= 8) return;

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
  // Memory policy: exactly one existing CEF BrowserView per workspace tab,
  // no additional UI web renderer; inactive overlay views remain hidden.
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
  // A visible tab uses the fast path (no strip rebuild), but the counter must
  // update on EVERY selection rather than waiting for theme/layout changes.
  UpdateTabSwitcherLabel();
  // Fast path: selecting a visible tab changes only highlights/focus.
  // Avoid detaching/reparenting every native view on Ctrl+Tab or click.
  const bool already_displayed = target->ui_panel &&
      target->ui_panel->IsValid() && target->ui_panel->GetParentView() &&
      target->title_button && target->title_button->IsValid();
  if (!already_displayed) RebuildTabStrip();
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

bool AppController::VerticalTabsVisible() const {
  if (!vertical_tabs_ || !window_) return false;
  return window_->GetClientAreaBoundsInScreen().width >= kSidebarMinWindowWidth;
}

int AppController::SidebarWidth() const {
  return SidebarCompactEffective() ? kSidebarCompactWidth : kSidebarWidth;
}

bool AppController::SidebarCompactEffective() const {
  return fullscreen_ ? !fullscreen_sidebar_expanded_ : sidebar_compact_;
}

void AppController::ToggleSidebarCompact() {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || !VerticalTabsVisible()) return;
  if (fullscreen_) {
    fullscreen_sidebar_expanded_ = !fullscreen_sidebar_expanded_;
  } else {
    const bool next = !sidebar_compact_;
    if (!SaveSidebarCompactPreference(next)) {
      LOG(ERROR) << "CGWA_SIDEBAR_PREF_DENIED";
      return;
    }
    sidebar_compact_ = next;
  }
  tab_drag_.Cancel();
  visible_tab_capacity_ = -1;
  RebuildTabStrip();
  LayoutTabOverlays();
  UpdateDraggableRegions();
  LOG(WARNING) << "CGWA_SIDEBAR_MODE "
               << (SidebarCompactEffective() ? "COMPACT" : "EXPANDED");
}

void AppController::ToggleTabsOrientation() {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || !window_ || window_->IsClosed()) return;
  const bool next = !vertical_tabs_;
  if (!SaveVerticalTabsPreference(next)) {
    LOG(ERROR) << "CGWA_TAB_LAYOUT_PREF_DENIED";
    return;
  }
  vertical_tabs_ = next;
  if (fullscreen_ && header_) header_->SetVisible(!vertical_tabs_);
  tab_drag_.Cancel();
  visible_tab_capacity_ = -1;
  RebuildTabStrip();
  LayoutTabOverlays();
  UpdateDraggableRegions();
  LOG(WARNING) << "CGWA_TAB_LAYOUT_APPLIED "
               << (vertical_tabs_ ? "VERTICAL" : "HORIZONTAL");
}

void AppController::RunVerticalTabsSelfTest() {
  CEF_REQUIRE_UI_THREAD();
  const bool initial_vertical = vertical_tabs_;
  const bool initial_compact = sidebar_compact_;
  const size_t initial_count = tabs_.size();
  const int initial_active = active_tab_id_;
  auto control_right = [&]() -> int {
    if (!window_ || !header_) return -1;
    window_->Layout();
    header_->Layout();
    const auto it = std::find_if(header_buttons_.begin(),
        header_buttons_.end(), [](const CefRefPtr<CefLabelButton>& item) {
          return item && item->GetID() == kCloseWindowButton;
        });
    if (it == header_buttons_.end()) return -1;
    const CefRect r = (*it)->GetBoundsInScreen();
    return r.x + r.width;
  };
  if (vertical_tabs_) ToggleTabsOrientation();
  const int horizontal_right = control_right();
  ToggleTabsOrientation();
  if (sidebar_compact_) ToggleSidebarCompact();
  const Tab* tab = FindTabById(initial_active);
  const int expanded_right = control_right();
  const bool expanded_ok = vertical_tabs_ && VerticalTabsVisible() &&
      sidebar_overlay_ && sidebar_overlay_->IsValid() &&
      sidebar_overlay_->IsVisible() && sidebar_ &&
      sidebar_overlay_->GetBounds().width == kSidebarWidth &&
      tab && tab->overlay && tab->overlay->IsValid() &&
      tab->overlay->GetBounds().x == kSidebarWidth;
  ToggleSidebarCompact();
  tab = FindTabById(initial_active);
  const int compact_right = control_right();
  const bool surface_ok = sidebar_fill_ && sidebar_ &&
      sidebar_fill_->GetBackgroundColor() == UiSidebarPalette().background &&
      sidebar_->GetBackgroundColor() == UiSidebarPalette().background &&
      sidebar_fill_->GetBounds().height > 80;
  const bool compact_ok = sidebar_compact_ && sidebar_overlay_ &&
      sidebar_overlay_->GetBounds().width == kSidebarCompactWidth &&
      tab && tab->overlay && tab->overlay->GetBounds().x == kSidebarCompactWidth &&
      tab->title_button && tab->title_button->IsVisible() &&
      tab->close_button && !tab->close_button->IsVisible();
  ToggleSidebarCompact();
  ToggleTabsOrientation();
  tab = FindTabById(initial_active);
  const int restored_right = control_right();
  const bool horizontal_ok = !vertical_tabs_ && sidebar_overlay_ &&
      !sidebar_overlay_->IsVisible() && tab && tab->overlay &&
      tab->overlay->GetBounds().x == 0 &&
      tabs_.size() == initial_count && active_tab_id_ == initial_active;
  const int screen_right = window_->GetClientAreaBoundsInScreen().x +
                            window_->GetClientAreaBoundsInScreen().width;
  const bool anchored = horizontal_right > 0 &&
      screen_right - horizontal_right <= 40 &&
      horizontal_right == expanded_right &&
      horizontal_right == compact_right &&
      horizontal_right == restored_right;
  if (initial_vertical) ToggleTabsOrientation();
  if (initial_compact) {
    if (!vertical_tabs_) ToggleTabsOrientation();
    ToggleSidebarCompact();
    if (!initial_vertical) ToggleTabsOrientation();
  }
  LOG(WARNING) << "CGWA_VERTICAL_TABS_SELFTEST "
               << (expanded_ok && compact_ok && horizontal_ok && anchored &&
                   surface_ok ? "PASS" : "FAIL")
               << " vertical=" << expanded_ok
               << " compact=" << compact_ok
               << " surface=" << surface_ok
               << " horizontal=" << horizontal_ok
               << " controls_anchored=" << anchored
               << " close_right_h=" << horizontal_right
               << " close_right_v=" << expanded_right
               << " close_right_c=" << compact_right
               << " close_right_restored=" << restored_right
               << " viewport_right=" << screen_right
               << " unchanged_tabs=" << (tabs_.size() == initial_count);
}

void AppController::LayoutTabOverlays() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_ || window_->IsClosed()) return;
  const CefRect client_bounds = window_->GetClientAreaBoundsInScreen();
  const bool header_visible = header_ && header_->IsVisible();
  const int top = header_visible ? kHeaderHeight : 0;
  const int width = std::max(1, client_bounds.width);
  const int height = std::max(1, client_bounds.height - top);
  const bool show_sidebar = VerticalTabsVisible();
  const int left = show_sidebar ? SidebarWidth() : 0;
  // Commander remains docked at the RIGHT, including native fullscreen.
  // The vertical left rail has its own visible toggle when header is hidden.
  const bool show_companion = companion_visible_ &&
      width - left >= kCompanionWidth + 320;
  const int browser_width =
      std::max(1, width - left - (show_companion ? kCompanionWidth : 0));
  const CefRect bounds(left, top, browser_width, height);
  if (sidebar_overlay_ && sidebar_overlay_->IsValid()) {
    sidebar_overlay_->SetBounds(CefRect(0, top, left, height));
    sidebar_overlay_->SetVisible(show_sidebar);
    if (show_sidebar && sidebar_) sidebar_->Layout();
  }
  for (auto& tab : tabs_) {
    if (tab.overlay && tab.overlay->IsValid())
      tab.overlay->SetBounds(bounds);
  }
  if (companion_overlay_ && companion_overlay_->IsValid()) {
    companion_overlay_->SetBounds(
        CefRect(left + browser_width, top, kCompanionWidth, height));
    companion_overlay_->SetVisible(show_companion);
  }
}

void AppController::SaveSessionState() {
  if (tabs_.empty()) return;
  // Compose in memory before touching the filesystem. Loading/title callbacks
  // often fire repeatedly without any actual change to the session state.
  std::ostringstream payload;
  int active_index = 0;
  for (size_t i = 0; i < tabs_.size(); ++i)
    if (tabs_[i].id == active_tab_id_) active_index = static_cast<int>(i);
  payload << "active=" << active_index << "\n";
  for (auto& tab : tabs_) {
    std::string url = tab.url.empty() ? "https://chatgpt.com/" : tab.url;
    if (tab.view && tab.view->GetBrowser() && tab.view->GetBrowser()->GetMainFrame()) {
      const std::string current = tab.view->GetBrowser()->GetMainFrame()->GetURL().ToString();
      if (IsChatGPTURL(current)) {
        url = current;
        tab.url = current;
      }
    }
    if (IsChatGPTURL(url)) payload << url << "\n";
  }
  const std::string snapshot = payload.str();
  if (snapshot == saved_session_snapshot_) return;
  const std::string path = StatePath();
  const std::string temp = path + ".tmp";
  std::error_code directory_error;
  const auto parent = std::filesystem::u8path(path).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, directory_error);
  if (directory_error) return;
  std::ofstream out(std::filesystem::u8path(temp), std::ios::trunc);
  if (!out) return;
  out << snapshot;
  out.flush();
  const bool good = out.good();
  out.close();
  if (good && CommitStateFile(temp, path)) {
    saved_session_snapshot_ = snapshot;
  } else {
    RemoveStateFile(temp);
  }
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

  RetireTabUi(*it);
  tabs_.erase(it);
  // A structural deletion must reconcile the strip exactly once.
  RebuildTabStrip();
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

void AppController::RetireTabUi(const Tab& tab) {
  CEF_REQUIRE_UI_THREAD();
  if (!tab.ui_panel && !tab.title_button && !tab.close_button) return;
  // The child views must outlive CEF's in-flight native button callbacks.
  retired_ui_.push_back({std::chrono::steady_clock::now(),
                         tab.ui_panel, tab.title_button, tab.close_button});
  CefPostDelayedTask(TID_UI, base::BindOnce([]() {
    if (auto* owner = AppController::Get()) owner->ReleaseRetiredUi();
  }), 1500);
}
void AppController::ReleaseRetiredUi() {
  CEF_REQUIRE_UI_THREAD();
  // Several closures may overlap. Never release a younger native button just
  // because another tab's retirement task fired earlier.
  const auto limit = std::chrono::steady_clock::now() -
                     std::chrono::milliseconds(1250);
  retired_ui_.erase(
      std::remove_if(retired_ui_.begin(), retired_ui_.end(),
                     [limit](const RetiredUi& item) {
                       return item.created <= limit;
                     }),
      retired_ui_.end());
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
  if (vertical_tab_strip_)
    vertical_tab_strip_->SetBackgroundColor(UiSidebarPalette().background);
  if (sidebar_) sidebar_->SetBackgroundColor(UiSidebarPalette().background);
  if (sidebar_fill_)
    sidebar_fill_->SetBackgroundColor(UiSidebarPalette().background);
  if (header_spacer_) header_spacer_->SetBackgroundColor(UiPalette().header);
  if (window_controls_) window_controls_->SetBackgroundColor(UiPalette().header);
  if (companion_fill_) companion_fill_->SetBackgroundColor(UiPalette().header);
  if (sidebar_orientation_button_) {
    sidebar_orientation_button_->SetBackgroundColor(UiSidebarPalette().background);
    sidebar_orientation_button_->SetEnabledTextColors(UiSidebarPalette().text);
  }
  if (sidebar_companion_button_) {
    sidebar_companion_button_->SetBackgroundColor(UiPalette().softAccent);
    sidebar_companion_button_->SetEnabledTextColors(UiPalette().activeText);
  }
  if (sidebar_toggle_button_) {
    sidebar_toggle_button_->SetBackgroundColor(UiSidebarPalette().background);
    sidebar_toggle_button_->SetEnabledTextColors(UiSidebarPalette().text);
  }
  if (sidebar_new_tab_button_) {
    sidebar_new_tab_button_->SetBackgroundColor(UiPalette().softAccent);
    sidebar_new_tab_button_->SetEnabledTextColors(UiPalette().activeText);
  }
  if (content_) content_->SetBackgroundColor(UiPalette().content);
  if (companion_panel_) companion_panel_->SetBackgroundColor(UiPalette().header);
  if (theme_button_) {
    const bool dark = g_ui_theme == rc_ui::ThemeMode::Dark;
    theme_button_->SetText(dark ? "☾" : "☼");
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

void AppController::TabLifetimeSoakStep(int cycle) {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || !window_) return;
  // 24 full tab create/close + native mode/theme churn cycles. No account data
  // mutation is performed; QA uses an isolated profile and a test-only flag.
  if (cycle >= 24) {
    LOG(WARNING) << "CGWA_NATIVE_TAB_LIFETIME_SOAK PASS cycles=" << cycle
                 << " current_tabs=" << tabs_.size();
    return;
  }
  if (tabs_.size() < 3) NewTab();
  if (cycle % 3 == 0) ToggleTabsOrientation();
  if (cycle % 4 == 0) ToggleSidebarCompact();
  if (cycle % 5 == 0) ToggleTheme();
  const int closed_id = active_tab_id_;
  QueueTabClose(closed_id);
  CefPostDelayedTask(TID_UI, base::BindOnce([](int cycle, int closed_id) {
    if (auto* app = AppController::Get()) {
      if (app->closing_) return;
      app->NewTab();
      LOG(WARNING) << "CGWA_NATIVE_TAB_LIFETIME_STEP cycle=" << cycle
                   << " closed=" << closed_id << " alive=" << app->tabs_.size();
      CefPostDelayedTask(TID_UI, base::BindOnce([](int cycle) {
        if (auto* app2 = AppController::Get())
          app2->TabLifetimeSoakStep(cycle + 1);
      }, cycle), 2000);
    }
  }, cycle, closed_id), 450);
}

void AppController::RunLiveTabCounterSelfTest() {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || !window_ || !tab_switcher_button_) return;
  const int original = active_tab_id_;
  // Disposable background-only profile: no authenticated page, no user tabs.
  while (tabs_.size() < 3) NewTab();
  const int first = tabs_.front().id;
  const int last = tabs_.back().id;
  const std::string count = std::to_string(tabs_.size());
  SetActiveTabInternal(first);
  const bool first_ok = tab_switcher_button_->GetText().ToString() ==
      "1 / " + count;
  // Must take the already-visible fast path; do not force a theme/strip rebuild.
  SetActiveTabInternal(last);
  const bool last_ok = tab_switcher_button_->GetText().ToString() ==
      count + " / " + count;
  SetActiveTabInternal(first);
  const bool restore_ok = tab_switcher_button_->GetText().ToString() ==
      "1 / " + count;
  SetActiveTabInternal(original);
  const bool final_ok = tab_switcher_button_->GetText().ToString() !=
      "0 / " + count;
  LOG(WARNING) << "CGWA_LIVE_TAB_COUNTER_SELFTEST "
               << (first_ok && last_ok && restore_ok && final_ok ?
                       "PASS" : "FAIL")
               << " first=" << first_ok << " last=" << last_ok
               << " restored=" << restore_ok << " tabs=" << tabs_.size();
}

void AppController::RunFullscreenTabsSelfTest() {
  CEF_REQUIRE_UI_THREAD();
  if (closing_ || !window_) return;
  if (window_->IsFullscreen()) return;
  // Use the native fullscreen API and retain the existing BrowserViews.
  if (!vertical_tabs_) ToggleTabsOrientation();
  ToggleFullscreen();
  CefPostDelayedTask(TID_UI, base::BindOnce([]() {
    auto* app = AppController::Get();
    if (!app || !app->window_) return;
    const auto* active = app->FindTabById(app->active_tab_id_);
    const bool compact = app->window_->IsFullscreen() &&
        app->VerticalTabsVisible() && app->SidebarCompactEffective() &&
        app->sidebar_overlay_ && app->sidebar_overlay_->IsValid() &&
        app->sidebar_overlay_->IsVisible() &&
        app->sidebar_overlay_->GetBounds().width == kSidebarCompactWidth &&
        active && active->title_button && active->title_button->IsVisible();
    // Expand without altering user preference.
    app->ToggleSidebarCompact();
    const bool expanded = app->window_->IsFullscreen() &&
        app->sidebar_overlay_ &&
        app->sidebar_overlay_->GetBounds().width == kSidebarWidth &&
        active && active->title_button && active->title_button->IsVisible();
    const bool rail_access = app->sidebar_orientation_button_ &&
        app->sidebar_orientation_button_->IsVisible() &&
        app->sidebar_companion_button_ &&
        app->sidebar_companion_button_->IsVisible();
    const bool companion_was_visible = app->companion_visible_;
    if (!companion_was_visible) app->ToggleCompanionPanel();
    app->LayoutTabOverlays();
    const CefRect viewport = app->window_->GetClientAreaBoundsInScreen();
    const CefRect companion_bounds = app->companion_overlay_->GetBounds();
    const bool commander_right = app->companion_overlay_ &&
        app->companion_overlay_->IsValid() &&
        app->companion_overlay_->IsVisible() &&
        companion_bounds.x + companion_bounds.width == viewport.width &&
        companion_bounds.width == kCompanionWidth &&
        active && active->overlay && active->overlay->IsValid() &&
        active->overlay->GetBounds().x +
            active->overlay->GetBounds().width == companion_bounds.x;
    if (!companion_was_visible) app->ToggleCompanionPanel();
    app->ToggleTabsOrientation();
    const bool horizontal = app->window_->IsFullscreen() &&
        app->header_ && app->header_->IsVisible() &&
        app->tab_strip_ && app->tab_strip_->IsVisible() &&
        active && active->title_button && active->title_button->IsVisible();
    const bool left_orientation = app->orientation_button_ &&
        app->orientation_button_->IsVisible();
    LOG(WARNING) << "CGWA_FULLSCREEN_TABS_SELFTEST "
                 << (compact && expanded && horizontal &&
                     rail_access && commander_right && left_orientation
                         ? "PASS" : "FAIL")
                 << " compact=" << compact
                 << " expanded=" << expanded
                 << " horizontal=" << horizontal
                 << " rail_toggle=" << rail_access
                 << " commander_right=" << commander_right
                 << " header_toggle=" << left_orientation;
    app->ToggleFullscreen();
  }), 1350);
}

void AppController::ToggleFullscreen() {
  CEF_REQUIRE_UI_THREAD();
  if (!window_) return;
  fullscreen_ = !window_->IsFullscreen();
  if (fullscreen_) fullscreen_sidebar_expanded_ = false;
  if (header_) header_->SetVisible(!fullscreen_ || !vertical_tabs_);
  window_->SetFullscreen(fullscreen_);
  RebuildTabStrip();
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
  if (command_id == kAccelExitFullscreen) {
    if (!window_ || !window_->IsFullscreen()) return false;
    ToggleFullscreen();
    return true;
  }
  if (command_id == kAccelCompanion) { ToggleCompanionPanel(); return true; }
  if (command_id == kAccelOrientation) { ToggleTabsOrientation(); return true; }
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

  if (id == kSidebarToggleButton || id == kSidebarOrientationButton) {
    bg = hot ? UiSidebarPalette().hover : UiSidebarPalette().background;
    fg = UiSidebarPalette().text;
  } else if (id == kSidebarNewTabButton) {
    // First-class ChatGPT action, visually distinct from the passive rail.
    bg = hot ? UiPalette().tabHover : UiPalette().softAccent;
    fg = UiPalette().activeText;
  } else if (id == kSidebarCompanionButton) {
    // Keep the vertical Commander action as prominent as the horizontal CTA.
    bg = hot ? UiPalette().tabHover : UiPalette().softAccent;
    fg = UiPalette().activeText;
  } else if (id == kBrandButton) {
    bg = UiPalette().header;
    fg = UiPalette().primary;
  } else if (id == kCompanionButton) {
    bg = hot ? UiPalette().primaryHover : UiPalette().primary;
    fg = UiPalette().content;
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
    bg = active ? UiPalette().tabActive : (hot ? UiPalette().tabHover : UiPalette().tab);
    fg = active ? UiPalette().activeText : (hot ? UiPalette().text : UiPalette().muted);
  }

  if (VerticalTabsVisible() && id >= kTabButtonBase) {
    const int tab_id = id >= kTabCloseBase
                           ? id - kTabCloseBase : id - kTabButtonBase;
    const bool active = tab_id == active_tab_id_;
    bg = active ? UiSidebarPalette().active : (hot ? UiSidebarPalette().hover : UiSidebarPalette().tab);
    fg = active ? UiSidebarPalette().text : UiSidebarPalette().muted;
  }
  button->SetBackgroundColor(bg);
  if (auto label = button->AsLabelButton())
    label->SetEnabledTextColors(fg);
}

void AppController::OnButtonPressed(CefRefPtr<CefButton> button) {
  CEF_REQUIRE_UI_THREAD();
  const int id = button->GetID();
  if (id == kBrandButton) {
    // Owner-invoked opt-in only. Never replace the current ChatGPT context.
    const auto command_line = CefCommandLine::GetGlobalCommandLine();
    if (command_line && command_line->HasSwitch("rc-home-preview") &&
        rc_home::IsReady())
      NewTab(rc_home::kUrl);
    return;
  }
  if (id == kNewTabButton) { NewTab(); return; }
  if (id == kTabSwitcherButton) { CycleTab(1); return; }
  if (id == kMoveTabLeftButton) { MoveActiveTab(-1); return; }
  if (id == kMoveTabRightButton) { MoveActiveTab(1); return; }
  if (id == kCompanionButton) { ToggleCompanionPanel(); return; }
  if (id == kTabsOrientationButton) { ToggleTabsOrientation(); return; }
  if (id == kSidebarToggleButton) { ToggleSidebarCompact(); return; }
  if (id == kSidebarNewTabButton) { NewTab(); return; }
  if (id == kSidebarOrientationButton) { ToggleTabsOrientation(); return; }
  if (id == kSidebarCompanionButton) { ToggleCompanionPanel(); return; }
  if (id == kThemeButton) { ToggleTheme(); return; }
  if (id == kCompanionRefreshButton) { RefreshCompanion(); return; }
  if (id == kFullscreenButton) { ToggleFullscreen(); return; }
  if (id == kMaximizeButton) { ToggleMaximize(); return; }
  if (id == kMinimizeButton) { Minimize(); return; }
  if (id == kCloseWindowButton) { RequestCloseWindow(); return; }
  if (id >= kTabCloseBase) { QueueTabClose(id - kTabCloseBase); return; }
  if (id >= kTabButtonBase) { ActivateTab(id - kTabButtonBase); return; }
}

CefRefPtr<CefImage> AppController::RoundedTabBadge(int tab_id, bool active) {
  CEF_REQUIRE_UI_THREAD();
  const auto found = std::find_if(tabs_.begin(), tabs_.end(),
      [tab_id](const Tab& tab) { return tab.id == tab_id; });
  const int ordinal = found == tabs_.end() ? 1 :
      std::clamp(static_cast<int>(std::distance(tabs_.begin(), found)) + 1, 1, 8);
  const int theme_slot = g_ui_theme == rc_ui::ThemeMode::Dark ? 1 : 0;
  const int mode = active ? 1 : 0;
  auto& stored = tab_badges_[theme_slot][mode][ordinal - 1];
  if (stored) return stored;

  // Hand-drawn circular native badge, not an additional browser renderer.
  // This is only 26x26 DIP and created once per tab ordinal/color state.
  static constexpr std::uint8_t digits[8][5] = {
      {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7},
      {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
      {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7},
      {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}};
  const cef_color_t base = active ? UiPalette().primary :
                                      UiSidebarPalette().active;
  // Dark CTA is light blue; white-on-light-blue is unreadable. Use the
  // theme's deliberately contrasting content color for the active badge.
  const cef_color_t fg = active ? UiPalette().content : UiSidebarPalette().text;
  auto channel = [](cef_color_t color, int bits) {
    return static_cast<unsigned char>((color >> bits) & 0xFFu);
  };
  auto picture = CefImage::CreateImage();
  if (!picture) return nullptr;
  for (const int scale : {1, 2}) {
    const int n = 26 * scale;
    std::vector<unsigned char> pixels(static_cast<size_t>(n*n*4), 0);
    const float radius = 11.25f * scale;
    const float mid = 13.0f * scale;
    const int width = 2 * scale;
    const int left = 13 * scale - 3 * scale;
    const int top = 13 * scale - 5 * scale;
    for (int y = 0; y < n; ++y) {
      for (int x = 0; x < n; ++x) {
        const float dx = x + 0.5f - mid;
        const float dy = y + 0.5f - mid;
        const float opacity = std::clamp(radius - std::sqrt(dx*dx + dy*dy) +
                                        0.5f, 0.0f, 1.0f);
        if (opacity <= 0) continue;
        const int px = (x - left) / width;
        const int py = (y - top) / width;
        const bool digit = x >= left && y >= top &&
            px >= 0 && px < 3 && py >= 0 && py < 5 &&
            (digits[ordinal - 1][py] & (1 << (2 - px))) != 0;
        const cef_color_t color = digit ? fg : base;
        const size_t at = static_cast<size_t>((y*n + x)*4);
        pixels[at] = channel(color,16);
        pixels[at+1] = channel(color,8);
        pixels[at+2] = channel(color,0);
        pixels[at+3] = static_cast<unsigned char>(opacity * 255.0f);
      }
    }
    if (!picture->AddBitmap(static_cast<float>(scale), n, n,
                            CEF_COLOR_TYPE_RGBA_8888,
                            CEF_ALPHA_TYPE_POSTMULTIPLIED,
                            pixels.data(), pixels.size()))
      return nullptr;
  }
  stored = picture;
  return stored;
}

void AppController::RebuildTabStrip() {
  if (!tab_strip_ || !vertical_tab_strip_) return;
  tab_strip_->SetBackgroundColor(UiPalette().header);
  vertical_tab_strip_->SetBackgroundColor(UiSidebarPalette().background);
  // Detach/reparent existing native tab rows. Do NOT release their buttons:
  // CEF may still dispatch hover/press/renderer callbacks referencing them.
  // Keeping stable CEF Views also avoids heap churn on every active-tab switch.
  tab_strip_->RemoveAllChildViews();
  vertical_tab_strip_->RemoveAllChildViews();
  const int logical_width = window_ ?
      window_->GetClientAreaBoundsInScreen().width : 1240;
  const bool compact_toolbar = logical_width < 930;
  const bool narrow_toolbar = logical_width < 690;
  compact_toolbar_cached_ = compact_toolbar;
  narrow_toolbar_cached_ = narrow_toolbar;
  const bool vertical = VerticalTabsVisible();
  tab_strip_->SetVisible(!vertical);
  vertical_tab_strip_->SetVisible(vertical);
  // CEF BoxLayout retains a view's last preferred width when its flex is
  // reduced to zero. Hide the inactive flex view instead, preventing the
  // controls from drifting outside the window on repeated mode switches.
  if (header_spacer_) header_spacer_->SetVisible(vertical);
  // The native sidebar has its own New Tab and tab switching controls.
  for (const auto& button : header_buttons_) {
    if (!button) continue;
    const int id = button->GetID();
    if (id == kTabSwitcherButton || id == kNewTabButton) {
      if (id == kNewTabButton)
        button->AsLabelButton()->SetText(compact_toolbar ? "＋" : "＋ New tab");
      button->SetVisible(!vertical);
    }
    else if (id == kMoveTabLeftButton || id == kMoveTabRightButton)
      button->SetVisible(!vertical && !compact_toolbar && tabs_.size() > 1);
    else if (id == kCompanionButton) {
      // Vertical rail owns its own Commander action. On a narrow horizontal
      // window keep the icon reachable without crowding the tab strip.
      button->AsLabelButton()->SetText(compact_toolbar ? "◈" : "◈ Commander");
      button->SetVisible(!vertical);
    }
    else if (id == kBrandButton) {
      // No brand disappears just because the user makes the window narrow.
      button->AsLabelButton()->SetText(narrow_toolbar ? "◈" : "◈  RC Browser");
      button->SetVisible(true);
    }
    else if (id == kTabsOrientationButton)
      button->SetVisible(!vertical); // Vertical rail owns the alternate action.
    // Keyboard accelerators remain available in compact and narrow widths.
    // Theme and OS window-management controls are always visible.
  }
  const bool compact = SidebarCompactEffective();
  if (sidebar_toggle_button_) {
    sidebar_toggle_button_->SetText(compact ? "≡" : "≡  Workspace tabs");
    sidebar_toggle_button_->SetAccessibleName(
        compact ? "Expand vertical tabs" : "Collapse vertical tabs");
  }
  if (sidebar_new_tab_button_)
    sidebar_new_tab_button_->SetText(compact ? "+" : "＋  New tab");
  if (sidebar_orientation_button_)
    sidebar_orientation_button_->SetText(compact ? "↔" : "↔  Horizontal tabs");
  if (sidebar_companion_button_)
    sidebar_companion_button_->SetText(compact ? "◈" : "◈  Commander");
  // Align words like navigation entries, while keeping compact icons centered.
  const auto rail_alignment = compact
      ? CEF_HORIZONTAL_ALIGNMENT_CENTER : CEF_HORIZONTAL_ALIGNMENT_LEFT;
  for (const auto& button : {sidebar_toggle_button_, sidebar_new_tab_button_,
                            sidebar_orientation_button_, sidebar_companion_button_}) {
    if (button) button->SetHorizontalAlignment(rail_alignment);
  }
  if (orientation_button_) {
    orientation_button_->SetText(vertical_tabs_ ? "⇥" : "☰");
    orientation_button_->SetAccessibleName(
        vertical_tabs_ ? "Use horizontal tabs" : "Use vertical tabs");
  }
  int active_index = 0;
  for (size_t i = 0; i < tabs_.size(); ++i)
    if (tabs_[i].id == active_tab_id_) active_index = static_cast<int>(i);
  const auto visible = vertical
      ? rc_ui::SelectVerticalTabs(
            window_->GetClientAreaBoundsInScreen().height -
                ((header_ && header_->IsVisible()) ? kHeaderHeight : 0),
            static_cast<int>(tabs_.size()), active_index)
      : rc_ui::SelectTabs(logical_width,
                          static_cast<int>(tabs_.size()), active_index);
  visible_tab_capacity_ = visible.capacity;
  for (size_t i = 0; i < tabs_.size(); ++i) {
    auto& tab = tabs_[i];
    const bool in_strip = static_cast<int>(i) >= visible.first &&
                          static_cast<int>(i) < visible.first + visible.visible;
    if (!in_strip) continue;
    if (tab.ui_panel && tab.title_button && tab.close_button &&
        tab.ui_panel->IsValid() && tab.title_button->IsValid() &&
        tab.close_button->IsValid()) {
      tab.close_button->SetVisible(!vertical || !SidebarCompactEffective());
      UpdateTabButton(tab);
      tab.title_button->SetTooltipText(tab.title);
      tab.title_button->SetAccessibleName(
          "Activate tab " + std::to_string(i + 1) + ": " + tab.title);
      tab.close_button->SetAccessibleName(
          "Close tab " + std::to_string(i + 1));
      if (vertical) vertical_tab_strip_->AddChildView(tab.ui_panel);
      else tab_strip_->AddChildView(tab.ui_panel);
      continue;
    }
    // Create each native control only once, as the tab itself is created.
    auto item = CefPanel::CreatePanel(nullptr);
    CefBoxLayoutSettings settings;
    settings.horizontal = true;
    settings.between_child_spacing = vertical ? 5 : 3;
    settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    item->SetToBoxLayout(settings);
    item->SetBackgroundColor(vertical ? UiSidebarPalette().background
                                      : UiPalette().header);

    auto title = CefLabelButton::CreateLabelButton(
        button_delegate_, vertical && SidebarCompactEffective()
                              ? "" : ShortTitle(tab.title));
    title->SetID(kTabButtonBase + tab.id);
    title->SetImage(CEF_BUTTON_STATE_NORMAL,
                    RoundedTabBadge(tab.id, tab.id == active_tab_id_));
    title->SetBackgroundColor(vertical
        ? (tab.id == active_tab_id_ ? UiSidebarPalette().active : UiSidebarPalette().tab)
        : (tab.id == active_tab_id_ ? UiPalette().tabActive : UiPalette().tab));
    title->SetEnabledTextColors(vertical
        ? (tab.id == active_tab_id_ ? UiSidebarPalette().text : UiSidebarPalette().muted)
        : (tab.id == active_tab_id_ ? UiPalette().activeText : UiPalette().muted));
    title->SetFontList(vertical && SidebarCompactEffective()
                           ? kCompactTabFont :
                       (tab.id == active_tab_id_ ? kUIBoldFont : kUIFont));
    title->SetInkDropEnabled(false);
    title->SetTooltipText(tab.title);
    title->SetAccessibleName("Activate tab " + std::to_string(i + 1) +
                             ": " + tab.title);

    auto close = CefLabelButton::CreateLabelButton(button_delegate_, "×");
    close->SetID(kTabCloseBase + tab.id);
    close->SetBackgroundColor(vertical
        ? (tab.id == active_tab_id_ ? UiSidebarPalette().active : UiSidebarPalette().tab)
        : (tab.id == active_tab_id_ ? UiPalette().tabActive : UiPalette().tab));
    close->SetEnabledTextColors(vertical ? UiSidebarPalette().muted : UiPalette().muted);
    close->SetVisible(!vertical || !SidebarCompactEffective());
    close->SetFontList(kUIFont);
    close->SetTooltipText("Close tab");
    close->SetAccessibleName("Close tab " + std::to_string(i + 1));
    close->SetInkDropEnabled(false);

    item->AddChildView(title);
    item->AddChildView(close);
    tab.ui_panel = item;
    tab.title_button = title;
    tab.close_button = close;
    if (vertical)
      vertical_tab_strip_->AddChildView(item);
    else
      tab_strip_->AddChildView(item);
  }
  UpdateTabSwitcherLabel();
  tab_strip_->Layout();
  vertical_tab_strip_->Layout();
  if (sidebar_) sidebar_->Layout();
  if (header_) {
    header_->Layout();
    UpdateDraggableRegions();
  }
}

void AppController::UpdateTabSwitcherLabel() {
  CEF_REQUIRE_UI_THREAD();
  if (!tab_switcher_button_) return;
  const auto current = std::find_if(tabs_.begin(), tabs_.end(),
      [this](const Tab& tab) { return tab.id == active_tab_id_; });
  const int active_index = current == tabs_.end()
      ? -1 : static_cast<int>(std::distance(tabs_.begin(), current));
  const auto label = tabs_.empty() ? "0 / 0" :
      std::to_string(std::max(0, active_index) + 1) + " / " +
      std::to_string(tabs_.size());
  if (tab_switcher_button_->GetText().ToString() != label)
    tab_switcher_button_->SetText(label);
  tab_switcher_button_->SetTooltipText(
      "Tab " + label + " — click for next, Ctrl+1..8 for direct selection");
  tab_switcher_button_->SetAccessibleName(
      "Tab " + label + "; activate next tab");
}

void AppController::UpdateTabButton(Tab& tab) {
  if (!tab.title_button) return;
  std::string text = VerticalTabsVisible() && SidebarCompactEffective()
                         ? "" : ShortTitle(tab.title);
  if (tab.loading && !(VerticalTabsVisible() && SidebarCompactEffective()))
    text = "• " + text;
  if (tab.title_button->GetText().ToString() != text)
    tab.title_button->SetText(text);
  const bool active = tab.id == active_tab_id_;
  auto badge = RoundedTabBadge(tab.id, active);
  auto shown = tab.title_button->GetImage(CEF_BUTTON_STATE_NORMAL);
  if (badge && (!shown || !shown->IsSame(badge)))
    tab.title_button->SetImage(CEF_BUTTON_STATE_NORMAL, badge);
  const bool vertical = VerticalTabsVisible();
  tab.title_button->SetBackgroundColor(vertical
      ? (active ? UiSidebarPalette().active : UiSidebarPalette().tab)
      : (active ? UiPalette().tabActive : UiPalette().tab));
  tab.title_button->SetEnabledTextColors(vertical
      ? (active ? UiSidebarPalette().text : UiSidebarPalette().muted)
      : (active ? UiPalette().activeText : UiPalette().muted));
  tab.title_button->SetFontList(
      vertical && SidebarCompactEffective()
          ? kCompactTabFont : (active ? kUIBoldFont : kUIFont));
  if (tab.close_button) {
    tab.close_button->SetBackgroundColor(vertical
        ? (active ? UiSidebarPalette().active : UiSidebarPalette().tab)
        : (active ? UiPalette().tabActive : UiPalette().tab));
    tab.close_button->SetEnabledTextColors(vertical ? UiSidebarPalette().muted : UiPalette().muted);
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
  RetireTabUi(*it);
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
  vertical_tab_strip_ = nullptr;
  sidebar_ = nullptr;
  sidebar_overlay_ = nullptr;
  sidebar_fill_ = nullptr;
  sidebar_layout_ = nullptr;
  sidebar_surface_delegate_ = nullptr;
  sidebar_toggle_button_ = nullptr;
  sidebar_new_tab_button_ = nullptr;
  header_spacer_ = nullptr;
  orientation_button_ = nullptr;
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
  // CefImage wrappers must be released while CEF's UI thread is still active.
  // Otherwise the BrowserApp owner may destruct after CefShutdown().
  for (auto& theme : tab_badges_)
    for (auto& mode : theme)
      std::fill(mode.begin(), mode.end(), CefRefPtr<CefImage>{});
  retired_ui_.clear();
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
