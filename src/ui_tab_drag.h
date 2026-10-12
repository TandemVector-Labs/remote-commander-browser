#pragma once
// R247: independent native-pointer drag intent and stable tab-reference relocation.
// No CEF, browser profile, cookie, OS hooks, or filesystem access in this module.
#include <algorithm>
#include <cstddef>
#include <vector>

namespace rc_ui {
struct TabDragIntent {
  int source_id = -1;
  int origin_x = 0;
  int origin_y = 0;
  bool pressed = false;
  bool moved = false;
  void Cancel() { source_id = -1; pressed = false; moved = false; }
  bool Begin(int source, int x, int y) {
    Cancel();
    if (source < 0) return false;
    source_id = source;
    origin_x = x; origin_y = y;
    pressed = true;
    return true;
  }
  void Observe(int x, int y) {
    if (!pressed) return;
    const long long dx = static_cast<long long>(x) - origin_x;
    const long long dy = static_cast<long long>(y) - origin_y;
    // Avoid squared-distance overflow for extreme/untrusted OS coordinates.
    if (dx <= -8 || dx >= 8 || dy <= -8 || dy >= 8) moved = true;
  }
  int Complete(int destination) {
    const int out = pressed && moved && destination >= 0 &&
                    destination != source_id ? source_id : -1;
    Cancel();
    return out;
  }
};
inline bool TabTitleContains(int mouse_x, int mouse_y, int rect_x, int rect_y,
                             int width, int height) {
  if (width <= 0 || height <= 0) return false;
  const long long x = mouse_x, y = mouse_y;
  return x >= rect_x && y >= rect_y &&
         x < static_cast<long long>(rect_x) + width &&
         y < static_cast<long long>(rect_y) + height;
}
template<class Tab>
bool MoveTabTo(std::vector<Tab>& tabs, int source_id, int target_id) {
  const auto source = std::find_if(tabs.begin(), tabs.end(),
                  [source_id](const Tab& t){return t.id == source_id;});
  const auto target = std::find_if(tabs.begin(), tabs.end(),
                  [target_id](const Tab& t){return t.id == target_id;});
  if (source == tabs.end() || target == tabs.end() || source == target) return false;
  const auto from = std::distance(tabs.begin(), source);
  const auto to = std::distance(tabs.begin(), target);
  for (auto index=std::min(from,to); index<=std::max(from,to); ++index) {
    if (tabs[static_cast<std::size_t>(index)].closing) return false;
  }
  // Rotate references/overlays with the tab struct; never construct browsers.
  if (from < to)
    std::rotate(tabs.begin()+from, tabs.begin()+from+1, tabs.begin()+to+1);
  else
    std::rotate(tabs.begin()+to, tabs.begin()+from, tabs.begin()+from+1);
  return true;
}
}  // namespace rc_ui
