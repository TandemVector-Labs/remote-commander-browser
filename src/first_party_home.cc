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
/* Quiet glass design tokens: no remote assets, scripts or intrusive page CSS. */
:root {
  color-scheme:light;
  --page:#f7f9fc; --ink:#19263b; --muted:#5d6d84; --line:rgba(116,139,171,.15);
  --glass:rgba(255,255,255,.67); --glass-strong:rgba(255,255,255,.85);
  --subtle:rgba(235,243,253,.68); --blue:#295caa; --blue-deep:#23477f;
  --ring:rgba(49,103,182,.18); --shadow:0 14px 48px rgba(31,55,87,.07);
  --orb-a:rgba(210,225,245,.42); --orb-b:rgba(219,234,249,.38);
}
@media (prefers-color-scheme:dark) {
  :root { color-scheme:dark; --page:#101927; --ink:#eef2f8; --muted:#b2bfd1;
    --line:rgba(200,218,242,.12); --glass:rgba(30,42,60,.61);
    --glass-strong:rgba(34,48,68,.84); --subtle:rgba(54,75,100,.38);
    --blue:#9bbff3; --blue-deep:#c1d8f6; --ring:rgba(129,169,220,.21);
    --shadow:0 14px 48px rgba(0,0,0,.19);
    --orb-a:rgba(51,78,110,.23); --orb-b:rgba(46,65,94,.26); }
}
* {box-sizing:border-box}
html {min-height:100%; background:var(--page)}
body {margin:0;min-height:100vh;overflow-x:hidden;
  font:400 15px/1.85 "Vazirmatn","Noto Sans Arabic",Tahoma,system-ui,sans-serif;
  color:var(--ink);background:radial-gradient(ellipse 56% 55% at 86% 0%,var(--orb-a),transparent 92%),
  radial-gradient(ellipse 56% 45% at 5% 94%,var(--orb-b),transparent 95%),var(--page)}
a {color:inherit;text-decoration:none;-webkit-tap-highlight-color:transparent}
a:focus-visible {outline:3px solid var(--blue);outline-offset:4px}
.wrap {width:min(1040px,calc(100% - 52px));margin:0 auto;padding:26px 0 30px}
.top {display:flex;align-items:center;justify-content:space-between;gap:14px;margin-bottom:48px;
  padding:12px 16px 12px 20px;border:1px solid var(--line);border-radius:999px;
  background:var(--glass);box-shadow:0 8px 28px rgba(33,64,100,.035);
  -webkit-backdrop-filter:blur(16px);backdrop-filter:blur(16px)}
.identity {display:flex;align-items:center;gap:12px;direction:ltr;min-width:0}
.mark {width:42px;height:42px;flex:none;border-radius:50%;display:grid;place-items:center;
  color:var(--blue);font:bold 17px/1 system-ui;background:var(--subtle);
  border:1px solid var(--line)}
.brand {display:flex;flex-direction:column;align-items:flex-start;direction:ltr;min-width:0}
.brand strong {font:750 14px/1.4 system-ui,sans-serif;letter-spacing:-.03em}
.brand small {color:var(--muted);font:500 9px/1.5 system-ui;letter-spacing:.16em;margin-top:2px}
.private {display:inline-flex;align-items:center;gap:8px;border:1px solid var(--line);
  border-radius:999px;background:var(--subtle);color:var(--muted);font-size:11px;
  padding:8px 14px;white-space:nowrap}
.private i {display:inline-block;width:7px;height:7px;background:#78ad96;border-radius:50%;flex:none}
.hero {text-align:center;position:relative;padding:17px 8px 0}
.kicker {display:inline-flex;align-items:center;padding:7px 18px;border-radius:999px;
  background:var(--glass);border:1px solid var(--line);color:var(--muted);font-size:12px;font-weight:650}
h1 {font-size:clamp(32px,4.1vw,52px);line-height:1.45;letter-spacing:-.05em;
  margin:25px auto 13px;font-weight:800}
h1 em {font-style:normal;color:var(--ink)}
.subtitle {margin:0 auto 29px;max-width:590px;color:var(--muted);
  font-size:clamp(13px,1.4vw,15px);line-height:2.1}
.actions {display:flex;justify-content:center;align-items:center;gap:18px;flex-wrap:wrap}
.primary {display:inline-flex;align-items:center;justify-content:center;gap:17px;
  padding:12px 28px;min-height:57px;min-width:224px;
  background:var(--glass-strong);color:var(--ink);border:1px solid var(--line);
  border-radius:999px;box-shadow:0 8px 28px rgba(39,65,91,.08),0 0 0 5px var(--ring);
  font-size:15px;font-weight:750;
  -webkit-backdrop-filter:blur(18px);backdrop-filter:blur(18px);
  transition:transform .18s ease,box-shadow .18s ease,border-color .18s ease}
.primary:hover {transform:translateY(-2px);border-color:var(--blue);
  box-shadow:0 14px 30px rgba(39,65,91,.11),0 0 0 5px var(--ring)}
.arrow {direction:ltr;font:650 19px/1 system-ui;color:var(--blue)}
.info {font-size:12px;color:var(--muted);text-align:right;max-width:220px}
.info strong {display:block;color:var(--ink);font-size:13px}
.group {margin:57px auto 0}
.grouphead {display:flex;justify-content:space-between;align-items:center;gap:16px;margin-bottom:15px}
.grouphead h2 {font-size:15px;margin:0;font-weight:750;letter-spacing:-.01em}
.grouphead span {color:var(--muted);font-size:12px}
.cards {display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:16px}
.card {border-radius:29px;padding:25px 24px 23px;min-height:174px;
  border:1px solid var(--line);background:var(--glass);box-shadow:var(--shadow);
  -webkit-backdrop-filter:blur(20px);backdrop-filter:blur(20px);
  transition:transform .18s ease,border-color .18s ease}
.card:hover {transform:translateY(-2px);border-color:var(--ring)}
.cardicon {display:grid;place-items:center;width:44px;height:44px;border-radius:50%;
  background:var(--subtle);border:1px solid var(--line);color:var(--blue);
  font:750 19px/1 system-ui}
.card h3 {font-size:15px;font-weight:750;margin:16px 0 6px}
.card p {margin:0;color:var(--muted);font-size:12px;line-height:2}
kbd {display:inline-block;direction:ltr;font:600 11px/1.1 system-ui;
  border:1px solid var(--line);background:var(--subtle);color:var(--ink);
  padding:6px 9px;border-radius:999px;white-space:nowrap;vertical-align:baseline}
.footer {display:flex;justify-content:space-between;gap:15px;color:var(--muted);
  font-size:11px;margin-top:32px;padding:18px 6px 0;
  border-top:1px solid var(--line)}
.footer b {font-weight:600;color:var(--ink)}
@media (max-width:780px) {
  .wrap {width:calc(100% - 28px);padding-top:14px}
  .top {margin-bottom:30px;padding:9px 12px}
  .hero {padding:4px 0 0}.group {margin-top:38px}
  .cards {grid-template-columns:1fr;gap:10px}
  .card {min-height:0;padding:17px 20px;border-radius:23px}
  .card h3 {margin:10px 0 4px}.footer {flex-wrap:wrap}
  .info {text-align:center}
}
@media (max-width:490px) {
  .mark {height:34px;width:34px}.brand strong {font-size:12px}
  .private {font-size:10px;padding:6px 9px}
  h1 {font-size:31px;margin-top:20px}
  .actions {flex-direction:column}.primary {width:100%}
  .grouphead span {display:none}
}
@media (prefers-reduced-motion:reduce) {
  .primary,.card {transition:none}.primary:hover,.card:hover {transform:none}
}
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
