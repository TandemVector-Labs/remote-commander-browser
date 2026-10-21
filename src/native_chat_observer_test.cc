#include "native_chat_observer.h"
#include "ui_chrome_layout.h"
#include <cassert>
#include <iostream>
int main() {
  using namespace rc_browser;
  const std::string A="11111111-1111-4111-8111-111111111111";
  const std::string B="22222222-2222-4222-8222-222222222222";
  assert(CanonicalChatId("https://chatgpt.com/c/"+A)==A);
  assert(CanonicalChatId("https://chatgpt.com/g/group/c/"+B+"?ref=hidden")==B);
  for(const auto& bad:{"https://chatgpt.com.evil.test/c/","http://chatgpt.com/c/","https://user@chatgpt.com/c/","https://chatgpt.com:443/c/"}) {
    assert(CanonicalChatId(std::string(bad)+A).empty());
  }
  NativeChatObserver core;
  core.Observe(1,"https://chatgpt.com/c/"+A);
  core.Observe(2,"https://chatgpt.com/c/"+A);
  core.Observe(3,"https://chatgpt.com/c/"+B);
  core.SetActive(1);
  const auto initial = core.Snapshot();
  assert(initial.open_tabs == 3 && initial.conversations == 2);
  assert(initial.visible_tabs == 1 && !initial.authenticated);
  core.Closed(2);
  const auto one_closed = core.Snapshot();
  assert(one_closed.open_tabs == 2 && one_closed.conversations == 2);
  core.Observe(3,"https://chatgpt.com/");
  const auto one_chat = core.Snapshot();
  assert(one_chat.conversations == 1);
  core.Closed(1);
  const auto all_closed = core.Snapshot();
  assert(all_closed.open_tabs == 0);
  assert(all_closed.closed_events == 2);
  assert(rc_ui::CanCreateTab(8));
  assert(rc_ui::CanCreateTab(15));
  assert(!rc_ui::CanCreateTab(16));
  assert(!rc_ui::CanCreateTab(-1));
  for (int selected = 0; selected < 16; ++selected) {
    const auto window = rc_ui::SelectTabs(1280, 16, selected);
    assert(window.visible > 0 && window.visible <= window.capacity);
    assert(window.first <= selected && selected < window.first + window.visible);
  }
  std::cout<<"CEF_NATIVE_CORE_CHAT_OBSERVER_PASS\n";
}
