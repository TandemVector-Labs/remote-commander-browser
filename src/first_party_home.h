#pragma once
// Opt-in, read-only first-party start page. No HTTP server, cookies, JS or
// privilege bridge; the normal ChatGPT BrowserView is reused.
#include "include/cef_app.h"

namespace rc_home {
inline constexpr char kScheme[] = "rc-home";
inline constexpr char kUrl[] = "rc-home://app/";
void RegisterScheme(CefRawPtr<CefSchemeRegistrar> registrar);
bool RegisterHandler();
bool IsReady();  // false if preview is not enabled or registration failed
}  // namespace rc_home
