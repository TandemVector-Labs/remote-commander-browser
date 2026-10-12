#include "../src/ui_chrome_layout.h"
#include <cassert>

int main() {
  for (int width : {320, 640, 880, 900, 1180, 1440, 2400}) {
    for (int count = 0; count <= 8; ++count) {
      for (int active = -2; active <= count + 2; ++active) {
        auto slice = rc_ui::SelectTabs(width, count, active);
        assert(slice.capacity >= 1 && slice.capacity <= 4);
        assert(slice.visible >= 0 && slice.visible <= count);
        assert(slice.first >= 0 && slice.first + slice.visible <= count);
        if (count) {
          const int safe = std::clamp(active, 0, count - 1);
          assert(safe >= slice.first);
          assert(safe < slice.first + slice.visible);
        }
      }
    }
  }
  for (int height : {100, 280, 400, 568, 760, 1800}) {
    for (int count = 0; count <= 18; ++count) {
      auto rows = rc_ui::CompanionRowSlots(height, count);
      assert(rows >= 0 && rows <= count && rows <= 18);
      assert((count == 0) == (rows == 0));
    }
  }
  assert(rc_ui::SelectTabs(1180, 8, 7).visible == 3);
  assert(rc_ui::SelectTabs(640, 8, 0).visible == 1);
  assert(rc_ui::CompanionRowSlots(568, 18) < 18);
}
