'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const home=read('src/first_party_home.cc');
const main=read('src/main.cc');
const app=read('src/app.cc');
const controller=read('src/controller.cc');
const page=home.match(/constexpr char kPage\[\] = R"RC_HOME_HTML\(([\s\S]*?)\)RC_HOME_HTML";/)?.[1];
test('first-party Home exists as an immutable, zero-JavaScript RTL page',()=>{
 assert.ok(page,'embedded HTML missing');
 assert.match(page,/<html lang="fa" dir="rtl">/);
 assert.match(page,/<meta name="viewport"/);
 assert.match(page,/<title>Remote Commander — Home<\/title>/);
 assert.match(page,/<a class="primary" href="https:\/\/chatgpt\.com\/"/);
 assert.doesNotMatch(page,/<script\b|<iframe\b|<form\b|on(?:load|click|error)=/i);
 assert.doesNotMatch(page,/https?:\/\/(?!chatgpt\.com\/)/i);
 assert.match(page,/@media \(max-width:780px\)/);
 assert.match(page,/@media \(prefers-reduced-motion:reduce\)/);
 assert.match(page, /@media \(prefers-color-scheme:dark\)/);
});
test('custom scheme is strict, not filesystem or cross-origin bridge',()=>{
 const header=read('src/first_party_home.h');
 assert.match(header,/kUrl\[\] = "rc-home:\/\/app\/"/);
 assert.match(home,/CEF_SCHEME_OPTION_DISPLAY_ISOLATED/);
 assert.match(home,/CEF_SCHEME_OPTION_STANDARD/);
 assert.match(home,/CEF_SCHEME_OPTION_SECURE/);
 assert.doesNotMatch(home,/CEF_SCHEME_OPTION_CSP_BYPASSING[^\n]*\|/);
 assert.doesNotMatch(home,/CEF_SCHEME_OPTION_FETCH_ENABLED[^\n]*\|/);
 assert.match(home,/request->GetMethod\(\)\.ToString\(\) != "GET"/);
 assert.match(home,/request->GetURL\(\)\.ToString\(\) != rc_home::kUrl/);
 for(const constraint of ["default-src 'none'","script-src 'none'","connect-src 'none'","form-action 'none'","frame-ancestors 'none'"])
   assert.ok(home.includes(constraint),constraint);
 assert.match(home,/Referrer-Policy/);
 assert.match(home,/no-store/);
});
test('only explicit preview switch enables handler; new saved sessions win',()=>{
 assert.match(app,/HasSwitch\("rc-home-preview"\)/);
 assert.match(app,/!rc_home::RegisterHandler\(\)/);
 assert.match(controller,/if \(urls.empty\(\)\)\s*urls.push_back\(command_line && command_line->HasSwitch\("rc-home-preview"\)/);
 assert.match(controller,/rc_home::IsReady\(\)/);
 assert.match(controller,/HasSwitch\("rc-home-narrow-test"\)/);
 assert.match(controller,/SetBounds\(CefRect\(100, 80, 650, 700\)\)/);
 assert.match(controller,/if \(in && std::getline\(in, line\)/);
 assert.match(controller,/if \(StartsWith\(line, "https:\/\/"\)\)/);
 assert.match(controller,/if \(IsChatGPTURL\(url\)\) payload << url/);
 assert.match(controller,/if \(id == kBrandButton\)[\s\S]*?HasSwitch\("rc-home-preview"\)/);
 assert.equal((controller.match(/CreateBrowserView\(/g)||[]).length,1);
});
test('Home remains outside page-script / RTL injection and permission trust',()=>{
 const client=read('src/client.cc');
 assert.match(client,/controller->IsChatGPTURL\(frame->GetURL\(\)\.ToString\(\)\)/);
 assert.match(client,/if \(controller && controller->IsChatGPTURL[\s\S]*?InjectRTL\(frame\)/);
 assert.match(client,/origin == "https:\/\/chatgpt\.com"/);
 assert.doesNotMatch(client,/origin == "rc-home:\/\/app"/);
});
test('child and browser processes register identical scheme options',()=>{
 assert.match(app,/BrowserApp::OnRegisterCustomSchemes/);
 assert.match(app,/rc_home::RegisterScheme\(registrar\)/);
 assert.match(main,/class HomeSubprocessApp final : public CefApp/);
 assert.match(main,/rc_home::RegisterScheme\(registrar\)/);
 assert.match(main,/IsSubprocess\(command_line\)/);
});
