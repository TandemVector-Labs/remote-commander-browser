#include "native_session_state.h"
#include <iostream>
#include <sstream>
#include <string>

static int checked = 0;
static bool Check(bool value, const char* name) {
  if (!value) { std::cerr << "NATIVE_SESSION_RESTORE_FAIL " << name << "\n"; return false; }
  ++checked;
  return true;
}
int main() {
  using rc_ui::ReadSessionState;
  std::ostringstream data;
  data << "active=15\n";
  for (int i = 0; i < 17; ++i)
    data << "https://chatgpt.com/c/" << i << "\n";
  std::istringstream sixteen(data.str());
  const auto full = ReadSessionState(sixteen);
  if (!Check(full.urls.size() == 16, "restore_16_not_8")) return 2;
  if (!Check(full.active_index == 15, "restore_last_active")) return 3;
  if (!Check(full.urls.front() == "https://chatgpt.com/c/0" &&
             full.urls.back() == "https://chatgpt.com/c/15", "preserve_order_and_cap_17")) return 4;

  std::istringstream legacy("https://chatgpt.com/c/a\nhttps://chatgpt.com/c/b\n");
  const auto old = ReadSessionState(legacy);
  if (!Check(old.urls.size() == 2 && old.active_index == 0 &&
             old.urls[0] == "https://chatgpt.com/c/a", "legacy_without_header")) return 5;

  std::istringstream eight("active=7\nhttps://chatgpt.com/c/1\nhttps://chatgpt.com/c/2\n");
  const auto clamped = ReadSessionState(eight);
  if (!Check(clamped.urls.size() == 2 && clamped.active_index == 1,
             "out_of_range_active_matches_prior_last_tab")) return 6;

  std::istringstream garbage("active=9999999999999999999999999\n"
                             "http://chatgpt.com/c/insecure\n"
                             "https://chatgpt.com/c/safe\n");
  const auto safe = ReadSessionState(garbage);
  if (!Check(safe.urls.size() == 1 && safe.active_index == 0 &&
             safe.urls[0] == "https://chatgpt.com/c/safe",
             "overlong_active_and_insecure_scheme")) return 7;

  std::istringstream empty;
  const auto fallback = ReadSessionState(empty);
  if (!Check(fallback.urls.size() == 1 && fallback.urls[0] == "https://chatgpt.com/",
             "fallback_if_missing_session")) return 8;

  std::istringstream malformed("untrusted-before-first-url\nhttps://chatgpt.com/c/safe\n");
  const auto filtered = ReadSessionState(malformed);
  if (!Check(filtered.urls.size() == 1 &&
             filtered.urls[0] == "https://chatgpt.com/c/safe",
             "reject_malformed_first_line")) return 9;

  std::cout << "NATIVE_SESSION_RESTORE16_CONTRACT_PASS checks=" << checked << "\n";
}
