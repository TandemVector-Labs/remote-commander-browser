#pragma once

#include <memory>

#include "include/cef_app.h"

class AppController;

class BrowserApp : public CefApp, public CefBrowserProcessHandler {
 public:
  BrowserApp();
  ~BrowserApp() override;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
  void OnContextInitialized() override;
  bool OnAlreadyRunningAppRelaunch(
      CefRefPtr<CefCommandLine> command_line,
      const CefString& current_directory) override;

 private:
  std::unique_ptr<AppController> controller_;
  IMPLEMENT_REFCOUNTING(BrowserApp);
};
