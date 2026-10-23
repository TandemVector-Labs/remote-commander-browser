#include "app.h"

#include <memory>

#include "controller.h"
#include "first_party_home.h"
#include "include/base/cef_bind.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

BrowserApp::BrowserApp() : controller_(std::make_unique<AppController>()) {}
BrowserApp::~BrowserApp() = default;

void BrowserApp::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  if (process_type.empty()) {
#if defined(__linux__)
    // Stay on X11 to match the qualified Linux desktop baseline. Do not
    // disable the Chromium sandbox.
    command_line->AppendSwitchWithValue("ozone-platform", "x11");
    // Respect a caller's explicit GPU-disable choice: a hybrid-GPU Wayland
    // machine had a real CEF GPU-process FATAL in the opt-in preview. This
    // does not claim to fix the driver/CEF crash; it permits a safe fallback.
    if (!command_line->HasSwitch("disable-gpu")) {
      command_line->AppendSwitch("enable-gpu-rasterization");
      command_line->AppendSwitch("enable-zero-copy");
    }
    // Chromium's native overlay scrollbars preserve horizontal scrolling
    // without a permanently painted bottom gutter in the owner preview.
    // Do not override --enable-features provided by the caller and never
    // change the unflagged normal browser or inject CSS into chatgpt.com.
    if (command_line->HasSwitch("rc-colorful-chrome-preview") &&
        !command_line->HasSwitch("enable-features")) {
      command_line->AppendSwitchWithValue("enable-features", "OverlayScrollbar");
    }
#endif
  }
}

void BrowserApp::OnRegisterCustomSchemes(
    CefRawPtr<CefSchemeRegistrar> registrar) {
  rc_home::RegisterScheme(registrar);
}

void BrowserApp::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();
  const auto cmd = CefCommandLine::GetGlobalCommandLine();
  if (cmd && cmd->HasSwitch("rc-home-preview") &&
      !rc_home::RegisterHandler()) {
    LOG(ERROR) << "RC_HOME_PREVIEW_SCHEME_FAILED";
  }
  controller_->CreateMainWindow();
}

bool BrowserApp::OnAlreadyRunningAppRelaunch(
    CefRefPtr<CefCommandLine> command_line,
    const CefString& current_directory) {
  CEF_REQUIRE_UI_THREAD();
  if (controller_ && command_line &&
      command_line->HasSwitch("shutdown-self-test")) {
    controller_->RequestCloseWindow();
    return true;
  }
  const bool silent = command_line &&
      (command_line->HasSwitch("background-test") ||
       command_line->HasSwitch("self-test"));
  if (controller_ && !silent)
    controller_->ActivateMainWindow();
  return true;
}
