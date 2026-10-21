#include "client.h"

#include <string>

#include "controller.h"
#include "include/base/cef_logging.h"
#include "include/wrapper/cef_helpers.h"

void AppClient::OnTitleChange(CefRefPtr<CefBrowser> browser,
                              const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (auto* c = AppController::Get())
    c->OnTitleChange(browser, title.ToString());
}

void AppClient::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (frame && frame->IsMain())
    if (auto* controller = AppController::Get())
      controller->OnNativeAddressChange(browser, url.ToString());
}

void AppClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  LOG(WARNING) << "CGWA_LIFE OnAfterCreated browser=" << browser->GetIdentifier();
  if (auto* c = AppController::Get())
    c->OnBrowserCreated(browser);
}

bool AppClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> frame,
                              int popup_id,
                              const CefString& target_url,
                              const CefString& target_frame_name,
                              cef_window_open_disposition_t target_disposition,
                              bool user_gesture,
                              const CefPopupFeatures& popupFeatures,
                              CefWindowInfo& windowInfo,
                              CefRefPtr<CefClient>& client,
                              CefBrowserSettings& settings,
                              CefRefPtr<CefDictionaryValue>& extra_info,
                              bool* no_javascript_access) {
  CEF_REQUIRE_UI_THREAD();
  if (auto* c = AppController::Get())
    c->HandlePopupURL(target_url.ToString());
  // Always suppress top-level popup windows. ChatGPT URLs become native tabs;
  // external URLs are sent to the system browser by the controller.
  return true;
}

bool AppClient::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  LOG(WARNING) << "CGWA_LIFE DoClose browser=" << browser->GetIdentifier();
  if (auto* c = AppController::Get())
    return c->OnBrowserDoClose(browser);
  return false;
}

void AppClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  LOG(WARNING) << "CGWA_LIFE OnBeforeClose browser=" << browser->GetIdentifier();
  if (auto* c = AppController::Get())
    c->OnBrowserClosed(browser);
}

void AppClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                                     bool isLoading,
                                     bool canGoBack,
                                     bool canGoForward) {
  CEF_REQUIRE_UI_THREAD();
  if (auto* c = AppController::Get())
    c->OnLoadingStateChange(browser, isLoading);
}

void AppClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          int httpStatusCode) {
  CEF_REQUIRE_UI_THREAD();
  if (frame && frame->IsMain())
    InjectRTL(frame);
}

void AppClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            ErrorCode errorCode,
                            const CefString& errorText,
                            const CefString& failedUrl) {
  CEF_REQUIRE_UI_THREAD();
  if (errorCode == ERR_ABORTED)
    return;
  LOG(WARNING) << "Load error " << static_cast<int>(errorCode) << " for "
               << failedUrl.ToString() << ": " << errorText.ToString();
}

bool AppClient::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> frame,
                                 const CefString& target_url,
                                 cef_window_open_disposition_t target_disposition,
                                 bool user_gesture) {
  CEF_REQUIRE_UI_THREAD();
  const std::string url = target_url.ToString();
  if (auto* c = AppController::Get()) {
    // Keep ChatGPT and its authentication flow inside the app. Route normal
    // external links to the user's system browser even when a page asks to
    // replace the current tab. This prevents the ChatGPT shell from turning
    // into a general-purpose browser.
    if (target_disposition == CEF_WOD_CURRENT_TAB ||
        target_disposition == CEF_WOD_UNKNOWN) {
      if (c->IsInternalNavigationURL(url)) return false;
      return c->OpenExternal(url);
    }
    c->HandlePopupURL(url);
    return true;
  }
  return false;
}

void AppClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                          TerminationStatus status,
                                          int error_code,
                                          const CefString& error_string) {
  CEF_REQUIRE_UI_THREAD();
  LOG(WARNING) << "Renderer terminated status=" << static_cast<int>(status)
               << " code=" << error_code << " " << error_string.ToString();
  if (browser && browser->GetMainFrame())
    browser->Reload();
}

bool AppClient::IsTrustedOrigin(const std::string& origin) const {
  // requesting_origin is an origin, not a full URL. Match exactly so a host
  // such as "chatgpt.com.example" can never inherit ChatGPT permissions.
  return origin == "https://chatgpt.com" || origin == "https://chatgpt.com/";
}

bool AppClient::OnRequestMediaAccessPermission(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    const CefString& requesting_origin,
    uint32_t requested_permissions,
    CefRefPtr<CefMediaAccessCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  const std::string origin = requesting_origin.ToString();
  if (IsTrustedOrigin(origin)) {
    callback->Continue(requested_permissions);
  } else {
    callback->Cancel();
  }
  return true;
}

bool AppClient::OnShowPermissionPrompt(
    CefRefPtr<CefBrowser> browser,
    uint64_t prompt_id,
    const CefString& requesting_origin,
    uint32_t requested_permissions,
    CefRefPtr<CefPermissionPromptCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  const std::string origin = requesting_origin.ToString();
  // Alloy runtime has no built-in permission UI. Grant only the minimum set
  // required for ChatGPT voice/video and clipboard features; deny broader
  // persistent capabilities such as notifications or filesystem access.
  const uint32_t safe =
      CEF_PERMISSION_TYPE_CAMERA_STREAM |
      CEF_PERMISSION_TYPE_MIC_STREAM |
      CEF_PERMISSION_TYPE_CLIPBOARD;
  if (IsTrustedOrigin(origin) && (requested_permissions & ~safe) == 0) {
    callback->Continue(CEF_PERMISSION_RESULT_ACCEPT);
  } else {
    callback->Continue(CEF_PERMISSION_RESULT_DENY);
  }
  return true;
}

bool AppClient::OnBeforeDownload(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  // Empty path + show_dialog=true uses the native save dialog.
  callback->Continue(CefString(), true);
  return true;
}

void AppClient::InjectRTL(CefRefPtr<CefFrame> frame) {
  if (!frame) return;
  const std::string js = R"JS(
(() => {
  'use strict';
  if (window.__cgwaR07Modern) return;
  window.__cgwaR07Modern = true;

  const STYLE_ID='cgwa-r07-modern';
  if (!document.getElementById(STYLE_ID)) {
    const style=document.createElement('style');
    style.id=STYLE_ID;
    style.textContent=`
      :root{
        --cgwa-accent:#2563eb;
        --cgwa-accent-hover:#1d4ed8;
        --cgwa-accent-soft:rgba(37,99,235,.08);
        --cgwa-ring:rgba(37,99,235,.20);
        --cgwa-border:rgba(100,116,139,.18);
        --cgwa-shadow:0 8px 30px rgba(15,23,42,.07);
        --cgwa-font:"Vazirmatn","Noto Sans Arabic","Noto Sans",ui-sans-serif,system-ui,sans-serif;
      }
      html,body,button,input,textarea,[contenteditable="true"]{font-family:var(--cgwa-font)!important;}
      body{font-feature-settings:"kern" 1,"liga" 1;}
      .markdown,[data-markdown-text-style="assistant-message"],[data-user-message-bubble="true"]{
        font-family:var(--cgwa-font)!important;
        line-height:1.86!important;
        letter-spacing:0!important;
      }
      .cgwa-rtl{direction:rtl!important;text-align:right!important;unicode-bidi:plaintext;}
      .cgwa-rtl :where(p,li,blockquote,h1,h2,h3,h4,h5,h6,dd,dt){text-align:right!important;}
      .cgwa-rtl :where(ul,ol){direction:rtl!important;padding-inline-start:0!important;padding-inline-end:1.45rem!important;}
      .cgwa-rtl :where(table,thead,tbody,tr,th,td){direction:rtl;text-align:start;}
      pre,code,kbd,samp,.katex,.katex-display,.math,[data-language]{
        direction:ltr!important;text-align:left!important;unicode-bidi:isolate!important;
        font-family:"DejaVu Sans Mono",ui-monospace,SFMono-Regular,monospace!important;
      }
      #prompt-textarea,[contenteditable="true"][role="textbox"]{
        font-family:var(--cgwa-font)!important;font-size:16px!important;line-height:1.78!important;
        caret-color:var(--cgwa-accent)!important;
      }
      .cgwa-composer-shell{
        border-radius:24px!important;
        box-shadow:0 1px 2px rgba(15,23,42,.04),0 0 0 1px var(--cgwa-border)!important;
        transition:box-shadow .16s ease,transform .16s ease!important;
      }
      .cgwa-composer-shell:focus-within{
        box-shadow:0 0 0 3px var(--cgwa-ring),0 8px 28px rgba(15,23,42,.06)!important;
      }
      nav :where(a,button),aside :where(a,button){
        border-radius:10px!important;
        transition:background-color .14s ease,color .14s ease!important;
      }
      :where(a,button)[aria-current="page"]{background:var(--cgwa-accent-soft)!important;}
      :where(button,a,[role="button"]):focus-visible{
        outline:2px solid var(--cgwa-accent)!important;outline-offset:2px!important;
        box-shadow:0 0 0 4px var(--cgwa-ring)!important;
      }
      button{transition:background-color .14s ease,color .14s ease,box-shadow .14s ease,transform .08s ease!important;}
      button:active{transform:translateY(1px);}
      button[data-testid="send-button"],button[aria-label*="send" i]{background:var(--cgwa-accent)!important;color:white!important;}
      button[data-testid="send-button"]:hover,button[aria-label*="send" i]:hover{background:var(--cgwa-accent-hover)!important;}
      ::selection{background:rgba(37,99,235,.18);}
      *{scrollbar-width:thin;scrollbar-color:rgba(100,116,139,.42) transparent;}
    `;
    (document.head||document.documentElement).appendChild(style);
  }

  const MSG=".markdown,[data-markdown-text-style='assistant-message'],[data-user-message-bubble='true']";
  const INPUT="#prompt-textarea,[contenteditable='true'][role='textbox']";
  const RTL_RE=/[\u0600-\u06FF\u0750-\u077F\u08A0-\u08FF\uFB50-\uFDFF\uFE70-\uFEFF]/g;
  const LATIN_RE=/[A-Za-z]/g;
  const STRONG_RE=/[A-Za-z\u0600-\u06FF\u0750-\u077F\u08A0-\u08FF\uFB50-\uFDFF\uFE70-\uFEFF]/;
  const RTL_ONE=/[\u0600-\u06FF\u0750-\u077F\u08A0-\u08FF\uFB50-\uFDFF\uFE70-\uFEFF]/;
  const isRTL=(text)=>{
    const s=(text||'').trim(); if(!s) return true;
    const r=(s.match(RTL_RE)||[]).length,l=(s.match(LATIN_RE)||[]).length;
    const first=s.match(STRONG_RE)?.[0]||'';
    return RTL_ONE.test(first)||(r>=2&&r>=l*.15);
  };
  const setDir=(el,rtl)=>{
    if(!el)return;
    el.setAttribute('dir',rtl?'rtl':'ltr');
    el.classList.toggle('cgwa-rtl',rtl);
    el.style.textAlign=rtl?'right':'left';
  };
  const markComposer=()=>{
    const input=document.querySelector(INPUT); if(!input)return;
    let shell=input.closest('form');
    if(!shell){
      let p=input.parentElement;
      for(let i=0;i<4&&p;i++,p=p.parentElement){
        const r=p.getBoundingClientRect();
        if(r.width>320&&r.height>48&&r.height<240){shell=p;break;}
      }
    }
    if(shell){
      const r=shell.getBoundingClientRect();
      if(r.height<260)shell.classList.add('cgwa-composer-shell');
    }
  };
  const apply=()=>{
    document.querySelectorAll(MSG).forEach(el=>setDir(el,isRTL(el.innerText||el.textContent||'')));
    document.querySelectorAll(INPUT).forEach(el=>setDir(el,isRTL(el.innerText||el.textContent||el.value||'')));
    document.querySelectorAll('pre,code,kbd,samp,.katex,.katex-display,.math,[data-language]').forEach(el=>{
      el.setAttribute('dir','ltr');el.classList.remove('cgwa-rtl');el.style.textAlign='left';el.style.unicodeBidi='isolate';
    });
    markComposer();
  };
  let queued=false;
  const schedule=()=>{if(queued)return;queued=true;requestAnimationFrame(()=>{queued=false;apply();});};
  document.addEventListener('input',schedule,true);
  document.addEventListener('change',schedule,true);
  new MutationObserver(schedule).observe(document.documentElement,{subtree:true,childList:true,characterData:true});
  window.addEventListener('pageshow',schedule,{passive:true});
  apply();
})();
)JS";
  frame->ExecuteJavaScript(js, frame->GetURL(), 0);
}
