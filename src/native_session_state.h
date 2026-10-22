#pragma once
// Private native browser URL-session restoration. Pure C++17: no CEF or IO side effects.
#include "ui_chrome_layout.h"
#include <charconv>
#include <istream>
#include <string>
#include <vector>

namespace rc_ui {
struct RestoredSession {
  std::vector<std::string> urls;
  int active_index = 0;
};

// The persisted format is "active=N" followed by URL lines. Older profiles
// without a header remain supported. Never instantiate more than the runtime
// native tab capacity; do not treat a malformed header as a navigation URL.
inline RestoredSession ReadSessionState(std::istream& in) {
  RestoredSession restored;
  int requested_active = 0;
  const auto append_url = [&restored](const std::string& url) {
    if (restored.urls.size() < static_cast<std::size_t>(kMaxNativeTabs) &&
        url.rfind("https://", 0) == 0)
      restored.urls.push_back(url);
  };
  std::string line;
  if (std::getline(in, line)) {
    if (line.rfind("active=", 0) == 0) {
      const char* start = line.data() + 7;
      const char* end = line.data() + line.size();
      int parsed = 0;
      const auto result = std::from_chars(start, end, parsed);
      if (result.ec == std::errc{} && result.ptr == end && parsed >= 0)
        requested_active = parsed;
    } else {
      append_url(line);
    }
  }
  while (restored.urls.size() < static_cast<std::size_t>(kMaxNativeTabs) &&
         std::getline(in, line))
    append_url(line);

  if (restored.urls.empty()) restored.urls.push_back("https://chatgpt.com/");
  restored.active_index = requested_active < static_cast<int>(restored.urls.size())
                              ? requested_active
                              : static_cast<int>(restored.urls.size()) - 1;
  return restored;
}
}  // namespace rc_ui
