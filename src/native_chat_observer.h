#pragma once
// Remote Commander Browser native tab observer. In-process, ephemeral, read-only.
// No Brave extension, Native Messaging, network, cookies, DOM or action bridge.
#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <string>

namespace rc_browser {
struct ChatCounts {
  size_t open_tabs = 0;
  size_t conversations = 0;
  size_t visible_tabs = 0;
  size_t closed_events = 0;
  bool authenticated = false;
};
inline std::string CanonicalChatId(const std::string& url) {
  if (url.size() > 4096) return {};
  // Exact HTTPS ChatGPT origins and a canonical UUID. Do not return query/fragment.
  static const std::regex re(
      R"(^https://(?:chatgpt\.com|chat\.openai\.com)/(?:g/[^/?#]{1,120}/)?c/([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})/?(?:[?#].*)?$)");
  std::smatch match;
  if (!std::regex_match(url, match, re)) return {};
  std::string id = match[1].str();
  // UUID hex digits are case-insensitive; normalize before deduplication.
  std::transform(id.begin(), id.end(), id.begin(), [](char ch) {
    return ch >= 'A' && ch <= 'F' ? static_cast<char>(ch - 'A' + 'a') : ch;
  });
  return id;
}
class NativeChatObserver {
 public:
  void Observe(int tab_id, const std::string& url) {
    if (tab_id <= 0) return;
    const auto chat = CanonicalChatId(url);
    if (chat.empty()) tabs_.erase(tab_id);
    else tabs_[tab_id] = chat;
  }
  void SetActive(int tab_id) { active_ = tab_id; }
  void Closed(int tab_id) {
    if (tabs_.erase(tab_id) != 0) ++closed_events_;
    if (active_ == tab_id) active_ = 0;
  }
  ChatCounts Snapshot() const {
    ChatCounts result;
    result.open_tabs = tabs_.size();
    result.closed_events = closed_events_;
    result.visible_tabs = tabs_.count(active_);
    std::set<std::string> ids;
    for (const auto& [tab, id] : tabs_) ids.insert(id);
    result.conversations = ids.size();
    return result;
  }
 private:
  std::map<int, std::string> tabs_;  // Process-local only, never serialized.
  int active_ = 0;
  size_t closed_events_ = 0;
};
}  // namespace rc_browser
