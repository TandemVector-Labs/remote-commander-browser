#include "first_party_home.h"

#include <atomic>
#include <cstring>
#include <string>

#include "include/cef_request.h"
#include "include/cef_response.h"
#include "include/cef_scheme.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_stream_resource_handler.h"

namespace {
std::atomic_bool g_ready{false};

// This immutable page runs at rc-home://app/ (NOT https://chatgpt.com).
// Never add executable scripts, remote assets, filesystem or Commander APIs.
// The page's sole hyperlink enters the original ChatGPT origin.
constexpr char kPage[] = R"RC_HOME_HTML(<!doctype html>
<html lang="fa" dir="rtl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'none'; style-src 'unsafe-inline'; img-src 'none'; connect-src 'none'; font-src 'none'; object-src 'none'; frame-src 'none'; form-action 'none'; base-uri 'none'">
<title>Remote Commander — Home</title>
<style>
:root {
  color-scheme: light dark;
  --page: #f7f9fd; --ink: #192b48; --muted: #596c87; --line: #dde6f2;
  --glass: rgba(255,255,255,.89); --subtle: #eff5fe;
  --blue: #245ab9; --blue-deep: #174688; --shadow: 0 24px 80px rgba(26,61,112,.09);
  --orb-a: #dae9ff; --orb-b: #edf5ff;
}
@media (prefers-color-scheme:dark) {
  :root { color-scheme: dark; --page: #0b1628; --ink: #eef5ff;
    --muted: #b5c5db; --line: #2c405d; --glass: rgba(20,36,59,.91);
    --subtle: #1b304d; --blue: #93c3ff; --blue-deep: #c2deff;
    --shadow: 0 24px 80px rgba(0,0,0,.25); --orb-a: #1b4679; --orb-b: #142945; }
}
* { box-sizing: border-box }
html { min-height:100%; background:var(--page) }
body { margin:0; min-height:100vh; overflow-x:hidden;
  font: 400 15px/1.85 "Vazirmatn","Noto Sans Arabic",Tahoma,system-ui,sans-serif;
  color:var(--ink); background:
  radial-gradient(ellipse 46% 38% at 78% 9%,var(--orb-a),transparent 99%),
  radial-gradient(ellipse 38% 45% at 8% 98%,var(--orb-b),transparent 95%),
  var(--page); }
a { color:inherit; text-decoration:none }
a:focus-visible { outline:3px solid #3d91ef; outline-offset:4px }
.wrap { width:min(1040px,calc(100% - 48px)); margin:0 auto; padding:40px 0 28px }
.top { display:flex; align-items:center; justify-content:space-between; gap:16px; margin-bottom:54px }
.identity { display:flex; align-items:center; gap:13px; direction:ltr }
.mark { width:42px; height:42px; border-radius:14px; display:grid; place-items:center;
  color:#fff; font:bold 18px/1 system-ui; background:linear-gradient(140deg,#265dbb,#4c90eb);
  box-shadow:0 10px 24px rgba(37,91,185,.21) }
.brand { display:flex; flex-direction:column; align-items:flex-start; direction:ltr }
.brand strong { font:700 14px/1.4 system-ui,sans-serif; letter-spacing:-.03em }
.brand small { color:var(--muted); font:500 10px/1.5 system-ui; letter-spacing:.17em; margin-top:3px }
.private { border:1px solid var(--line); border-radius:99px; background:var(--glass);
  color:var(--muted); font-size:12px; padding:7px 14px; white-space:nowrap }
.private i { display:inline-block; width:7px;height:7px;background:#5aa985;
  border-radius:50%;margin-left:8px }
.hero { text-align:center; position:relative }
.kicker { display:inline-block; padding:7px 16px; border-radius:90px;
  background:var(--subtle); border:1px solid var(--line); color:var(--blue-deep);
  font-size:12px; font-weight:700; letter-spacing:.01em }
h1 { font-size:clamp(33px,4.4vw,55px); line-height:1.45; letter-spacing:-.055em;
  margin:27px auto 13px; font-weight:800 }
h1 em { font-style:normal; color:var(--blue) }
.subtitle { margin:0 auto 29px; max-width:590px; color:var(--muted);
  font-size:clamp(13px,1.45vw,16px); line-height:2.1 }
.actions { display:flex; justify-content:center; align-items:center; gap:16px; flex-wrap:wrap }
.primary { display:inline-flex; align-items:center; justify-content:center; gap:14px;
  padding:13px 25px; min-height:52px; min-width:208px;
  background:linear-gradient(130deg,#235abd,#377bd9); color:white;
  box-shadow:0 12px 30px rgba(34,88,180,.25); border:1px solid #4c8ce0;
  border-radius:17px; font-size:15px; font-weight:700;
  transition:transform .18s ease,box-shadow .18s ease }
.primary:hover { transform:translateY(-2px); box-shadow:0 16px 34px rgba(34,88,180,.29) }
.arrow { direction:ltr; font:600 22px/1 system-ui }
.info { font-size:12px; color:var(--muted); text-align:right; max-width:220px }
.info strong { display:block; color:var(--ink); font-size:13px }
.group { margin:66px auto 0 }
.grouphead { display:flex; justify-content:space-between; align-items:center; gap:16px;
  margin-bottom:16px }
.grouphead h2 { font-size:15px; margin:0; font-weight:750; letter-spacing:-.01em }
.grouphead span { color:var(--muted); font-size:12px }
.cards { display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); gap:14px }
.card { border-radius:23px; padding:25px 23px 22px; min-height:168px;
  border:1px solid var(--line); background:var(--glass);
  box-shadow:var(--shadow); backdrop-filter:none }
.cardicon { display:grid; place-items:center; width:40px;height:40px;
  border-radius:14px;background:var(--subtle); border:1px solid var(--line);
  color:var(--blue); font:800 19px/1 system-ui }
.card h3 { font-size:15px; font-weight:750; margin:15px 0 6px }
.card p { margin:0;color:var(--muted);font-size:12px;line-height:2 }
kbd { display:inline-block; direction:ltr; font:600 11px/1.1 system-ui;
  border:1px solid var(--line);background:var(--subtle);color:var(--ink);
  padding:5px 7px;border-radius:7px;white-space:nowrap;vertical-align:baseline }
.footer { display:flex; justify-content:space-between; gap:15px; color:var(--muted);
  font-size:11px; margin-top:31px; border-top:1px solid var(--line); padding-top:17px }
.footer b { font-weight:600;color:var(--ink) }
@media (max-width:780px) {
  .wrap { width:calc(100% - 32px); padding-top:22px }
  .top { margin-bottom:33px } .group { margin-top:44px }
  .cards { grid-template-columns:1fr; gap:9px }
  .card { min-height:0; padding:16px 20px }
  .card h3 { margin:9px 0 2px }
  .footer { flex-wrap:wrap } .info { text-align:center }
}
@media (max-width:490px) {
  .top { align-items:flex-start } .private { font-size:10px; padding:5px 9px }
  h1 { font-size:32px; margin-top:20px }
  .actions { flex-direction:column } .primary { width:100% }
  .grouphead span { display:none }
}
@media (prefers-reduced-motion:reduce) { .primary { transition:none } .primary:hover {transform:none} }
</style>
</head>
<body>
<main class="wrap">
  <header class="top">
    <div class="identity">
      <span class="mark" aria-hidden="true">◆</span>
      <div class="brand"><strong>Remote Commander</strong><small>YOUR WORKSPACE</small></div>
    </div>
    <span class="private"><i aria-hidden="true"></i>صفحهٔ آغاز محلی</span>
  </header>
  <section class="hero" aria-labelledby="welcome">
    <span class="kicker">مرتب، آرام، متمرکز</span>
    <h1 id="welcome">فضایی بهتر برای <em>فکر کردن</em></h1>
    <p class="subtitle">یک شروع ساده و مستقل برای کار با ChatGPT و ابزارهای Remote Commander.
      گفتگوها، پروژه‌ها و اطلاعات حساب شما همچنان در وب‌سایت اصلی ChatGPT باقی می‌مانند.</p>
    <div class="actions">
      <a class="primary" href="https://chatgpt.com/" rel="noreferrer">ورود به ChatGPT <span class="arrow" aria-hidden="true">↗</span></a>
      <p class="info"><strong>همان حساب و پروژه‌های شما</strong>بدون کپی‌کردن گفتگوها یا اطلاعات ورود</p>
    </div>
  </section>
  <section class="group" aria-labelledby="tools">
    <div class="grouphead"><h2 id="tools">ابزارها، بدون شلوغی</h2><span>میانبرهای واقعی Browser</span></div>
    <div class="cards">
      <article class="card">
        <span class="cardicon" aria-hidden="true">◎</span>
        <h3>تب‌های مرتب</h3>
        <p>برای بازکردن یک تب جدید <kbd>Ctrl + T</kbd> و برای جابه‌جایی بین تب‌ها <kbd>Ctrl + Tab</kbd>.</p>
      </article>
      <article class="card">
        <span class="cardicon" aria-hidden="true">⌘</span>
        <h3>پنل Commander</h3>
        <p>با <kbd>Ctrl + Shift + C</kbd> پنل محلی Commander را باز یا بسته کنید.</p>
      </article>
      <article class="card">
        <span class="cardicon" aria-hidden="true">◧</span>
        <h3>چیدمان دلخواه</h3>
        <p>با <kbd>Ctrl + Shift + E</kbd> نوار تب‌ها را عمودی یا افقی کنید.</p>
      </article>
    </div>
  </section>
  <footer class="footer">
    <span><b>RC Browser / Home Preview</b> — یک صفحهٔ داخلی، بدون اتصال یا ثبت اطلاعات</span>
    <span>برای برگشت به این صفحه از عنوان RC Browser در نوار بالا استفاده کنید.</span>
  </footer>
</main>
</body>
</html>)RC_HOME_HTML";

class HomeSchemeFactory final : public CefSchemeHandlerFactory {
 public:
  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    if (!request || scheme_name.ToString() != rc_home::kScheme ||
        request->GetMethod().ToString() != "GET" ||
        request->GetURL().ToString() != rc_home::kUrl)
      return nullptr; // No dynamic content or path-based resource access.
    CefResponse::HeaderMap headers;
    headers.insert({"Content-Security-Policy",
        "default-src 'none'; script-src 'none'; style-src 'unsafe-inline'; "
        "img-src 'none'; connect-src 'none'; font-src 'none'; object-src 'none'; "
        "frame-src 'none'; form-action 'none'; base-uri 'none'; frame-ancestors 'none'"});
    headers.insert({"X-Content-Type-Options", "nosniff"});
    headers.insert({"Referrer-Policy", "no-referrer"});
    headers.insert({"Cache-Control", "no-store"});
    headers.insert({"Cross-Origin-Resource-Policy", "same-origin"});
    auto stream = CefStreamReader::CreateForData(
        const_cast<char*>(kPage), std::strlen(kPage));
    return new CefStreamResourceHandler(200, "OK", "text/html", headers, stream);
  }
 private:
  IMPLEMENT_REFCOUNTING(HomeSchemeFactory);
};
} // namespace

namespace rc_home {
void RegisterScheme(CefRawPtr<CefSchemeRegistrar> registrar) {
  // DISPLAY_ISOLATED prevents pages from other schemes embedding this Home.
  // Do not add CORS, FETCH, CSP_BYPASSING or LOCAL scheme privileges.
  registrar->AddCustomScheme(kScheme, CEF_SCHEME_OPTION_STANDARD |
                                        CEF_SCHEME_OPTION_SECURE |
                                        CEF_SCHEME_OPTION_DISPLAY_ISOLATED);
}

bool RegisterHandler() {
  // Only the global browser request context serves the one allowed URL.
  const bool ready =
      CefRegisterSchemeHandlerFactory(kScheme, "app", new HomeSchemeFactory());
  g_ready.store(ready);
  return ready;
}
bool IsReady() { return g_ready.load(); }
} // namespace rc_home
