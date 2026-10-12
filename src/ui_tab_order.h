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
}  // namespace rc_ui
