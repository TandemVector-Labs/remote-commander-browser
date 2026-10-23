#pragma once
// Native browser chrome layout policy. Pure C++17: no CEF, IO or user state.
#include <algorithm>

namespace rc_ui {
// Bounded multi-chat workspace: up to 16 native CEF tabs; visual strip remains virtualized.
constexpr int kMaxNativeTabs = 16;
inline bool CanCreateTab(int tab_count) { return tab_count >= 0 && tab_count < kMaxNativeTabs; }
struct TabWindow {
  int first;
  int visible;
  int capacity;
};

// Bounds are CEF logical pixels, not physical HiDPI pixels. The reserved
// area includes title/overflow, new tab, companion and window controls.
inline TabWindow SelectTabs(int logical_width, int count, int active) {
  const int total = std::max(0, count);
  const int width = std::max(0, logical_width);
  const int chrome_reserved = width < 900 ? 464 : 700; // Includes two 40-DIP tab-order buttons.
  const int capacity = std::clamp((width - chrome_reserved) / 171, 1, 4);
  if (!total) return {0, 0, capacity};
  const int visible = std::min(total, capacity);
  const int selected = std::clamp(active, 0, total - 1);
  const int first = std::clamp(selected - visible / 2, 0,
                               std::max(0, total - visible));
  return {first, visible, capacity};
}

// Vertical sidebar shows native tab rows without truncating the active tab.
// Hidden overflow remains reachable through the existing switcher/shortcuts.
inline TabWindow SelectVerticalTabs(int usable_height, int count, int active) {
  const int total = std::max(0, count);
  const int capacity = std::clamp((std::max(0, usable_height) - 16) / 40, 1, 8);
  if (!total) return {0, 0, capacity};
  const int visible = std::min(total, capacity);
  const int selected = std::clamp(active, 0, total - 1);
  const int first = std::clamp(selected - visible / 2, 0,
                               std::max(0, total - visible));
  return {first, visible, capacity};
}

// Reserve space for refresh and padding. If rows overflow, the final row
// is a non-interactive summary; no row silently disappears as "healthy".
inline int CompanionRowSlots(int viewport_height, int row_count) {
  if (row_count <= 0) return 0;
  return std::clamp((std::max(0, viewport_height) - 72) / 30,
                    1, std::min(row_count, 18));
}
}  // namespace rc_ui
