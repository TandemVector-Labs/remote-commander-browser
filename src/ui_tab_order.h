#pragma once
// Pure C++17 tab-order mutation. No CEF, IO, profile or BrowserView creation.
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace rc_ui {
// Reject boundaries, closing tabs and invalid directions without side effects.
// Tab values carry the existing BrowserView/overlay references by move/swap.
template <typename Tab>
bool ShiftActiveTab(std::vector<Tab>& tabs, int active_id, int direction) {
  if (direction != -1 && direction != 1) return false;
  auto it = std::find_if(tabs.begin(), tabs.end(),
                         [active_id](const Tab& tab) { return tab.id == active_id; });
  if (it == tabs.end() || it->closing) return false;
  const std::ptrdiff_t index = std::distance(tabs.begin(), it);
  const std::ptrdiff_t dest = index + direction;
  if (dest < 0 || dest >= static_cast<std::ptrdiff_t>(tabs.size()) ||
      tabs[static_cast<std::size_t>(dest)].closing) return false;
  std::swap(*it, tabs[static_cast<std::size_t>(dest)]);
  return true;
}
// Platform-independent pointer gesture policy. Native backends must supply
// real press/move/release events in logical DIP; no synthetic browser events.
// Never switch a tab or write a user profile before a validated release.
struct TabDragIntent {
  int tab_id = -1;
  int start_x = 0;
  int start_y = 0;
  bool down = false;
  bool threshold_reached = false;
  void Cancel() { tab_id = -1; down = false; threshold_reached = false; }
  bool Begin(int id, int x, int y) {
    Cancel();
    if (id < 0) return false;
    tab_id = id; start_x = x; start_y = y; down = true;
    return true;
  }
  bool Move(int x, int y) {
    if (!down) return false;
    const long long dx = static_cast<long long>(x) - start_x;
    const long long dy = static_cast<long long>(y) - start_y;
    if (dx*dx + dy*dy >= 64) threshold_reached = true;
    return threshold_reached;
  }
  int Release(int destination_id) {
    const int src = (down && threshold_reached &&
      destination_id >= 0 && destination_id != tab_id) ? tab_id : -1;
    Cancel();
    return src;  // -1 is fail closed; caller separately validates target tab.
  }
};

// Stable-id order relocation, not an object/browser recreation. Reject a
// closing tab anywhere in the traversed interval before any mutation.
template <typename Tab>
bool MoveTabTo(std::vector<Tab>& tabs, int source_id, int target_id) {
  const auto find = [&](int id) {
    return std::find_if(tabs.begin(), tabs.end(),
                        [id](const Tab& v){ return v.id == id; });
  };
  const auto src = find(source_id), dst = find(target_id);
  if (src == tabs.end() || dst == tabs.end() || src == dst) return false;
  const auto from = std::distance(tabs.begin(), src);
  const auto to = std::distance(tabs.begin(), dst);
  for (auto i = std::min(from,to); i <= std::max(from,to); ++i)
    if (tabs[static_cast<std::size_t>(i)].closing) return false;
  if (from < to)
    std::rotate(tabs.begin()+from,tabs.begin()+from+1,tabs.begin()+to+1);
  else
    std::rotate(tabs.begin()+to,tabs.begin()+from,tabs.begin()+from+1);
  return true;
}
}  // namespace rc_ui
