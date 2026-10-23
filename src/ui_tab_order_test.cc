#include "ui_tab_order.h"
#include "ui_tab_drag.h"
#include "ui_chrome_layout.h"
#include <iostream>
#include <memory>
#include <vector>

struct Tab {
  int id;
  bool closing;
  std::unique_ptr<int> browser_resource;  // Move-only stand-in for live CEF refs.
};
static int tests = 0;
static bool Check(bool ok, const char* what) {
  if (!ok) { std::cerr << "TAB_ORDER_TEST_FAIL " << what << "\n"; return false; }
  ++tests; return true;
}
int main() {
  std::vector<Tab> tabs;
  for (int i=1; i<=3; ++i)
    tabs.push_back(Tab{i, false, std::make_unique<int>(100+i)});
  const int* const active_browser = tabs[1].browser_resource.get();
  if (!Check(rc_ui::ShiftActiveTab(tabs,2,1),"right")) return 1;
  if (!Check(tabs[2].id==2 && tabs[0].id==1 && tabs[1].id==3,"order_right")) return 1;
  if (!Check(tabs[2].browser_resource.get()==active_browser,"browser_identity")) return 1;
  if (!Check(rc_ui::ShiftActiveTab(tabs,2,-1),"left")) return 1;
  if (!Check(tabs[1].id==2 && tabs[1].browser_resource.get()==active_browser,"order_left")) return 1;
  if (!Check(!rc_ui::ShiftActiveTab(tabs,1,-1),"left_boundary")) return 1;
  if (!Check(!rc_ui::ShiftActiveTab(tabs,3,1),"right_boundary")) return 1;
  if (!Check(!rc_ui::ShiftActiveTab(tabs,99,1),"missing_id")) return 1;
  if (!Check(!rc_ui::ShiftActiveTab(tabs,2,0),"zero_direction")) return 1;
  if (!Check(!rc_ui::ShiftActiveTab(tabs,2,2),"bad_direction")) return 1;
  tabs[1].closing=true;
  if (!Check(!rc_ui::ShiftActiveTab(tabs,2,1),"closing_active")) return 1;
  tabs[1].closing=false; tabs[2].closing=true;
  if (!Check(!rc_ui::ShiftActiveTab(tabs,2,1),"closing_destination")) return 1;
  tabs[2].closing=false;
  if (!Check(tabs[0].id==1 && tabs[1].id==2 && tabs[2].id==3,"rejections_unchanged")) return 1;
  std::vector<Tab> single; single.push_back(Tab{42,false,std::make_unique<int>(42)});
  if (!Check(!rc_ui::ShiftActiveTab(single,42,1),"single")) return 1;
  std::vector<Tab> empty;
  if (!Check(!rc_ui::ShiftActiveTab(empty,42,1),"empty")) return 1;
  std::vector<Tab> eight;
  for (int i=0;i<8;++i) eight.push_back(Tab{i,false,std::make_unique<int>(i)});
  if (!Check(rc_ui::ShiftActiveTab(eight,0,1) && eight[1].id==0,"eight_tabs")) return 1;
  if (!Check(rc_ui::SelectTabs(800,8,3).capacity==1,"compact_chrome_reservation")) return 1;
  if (!Check(rc_ui::SelectTabs(1920,8,3).capacity==4,"wide_chrome_reservation")) return 1;
  // Native vertical-tab capacity tracks available viewport height while
  // preserving the active selection and keeping at most eight live tabs.
  if (!Check(rc_ui::SelectVerticalTabs(800,8,3).capacity==8,"vertical_all_eight")) return 1;
  if (!Check(rc_ui::SelectVerticalTabs(120,8,7).visible==2,"vertical_small_height")) return 1;
  if (!Check(rc_ui::SelectVerticalTabs(120,8,7).first==6,"vertical_active_last_visible")) return 1;
  if (!Check(rc_ui::SelectVerticalTabs(100,0,0).visible==0,"vertical_empty")) return 1;
  if (!Check(rc_ui::SelectVerticalTabs(-99,8,6).capacity==1,"vertical_nonnegative_geometry")) return 1;
  // R247 historical R246 cppcheck failure prevention: prove browser
  // resource identity via mutation of the original heap object, not by
  // cross-vector unique_ptr.get() equality on which cppcheck 2.13 erred.
  struct Resource { int marker; };
  struct DragTab { int id; bool closing; std::unique_ptr<Resource> browser; };
  std::vector<DragTab> held;
  for (int i=0; i<4; ++i)
    held.push_back(DragTab{i,false,std::make_unique<Resource>(Resource{i})});
  const Resource& original = *held[1].browser;
  if (!Check(rc_ui::MoveTabTo(held,1,3),"native_multi_move")) return 1;
  held[3].browser->marker = 879;
  if (!Check(original.marker==879,"native_browser_lifetime")) return 1;
  if (!Check(held[0].id==0 && held[1].id==2 && held[2].id==3 &&
             held[3].id==1,"native_order_stable")) return 1;
  if (!Check(rc_ui::MoveTabTo(held,1,0),"native_left_move")) return 1;
  if (!Check(held[0].id==1 && original.marker==879,"native_left_identity")) return 1;
  if (!Check(!rc_ui::MoveTabTo(held,1,1),"native_same_id")) return 1;
  if (!Check(!rc_ui::MoveTabTo(held,1,99),"native_missing_dest")) return 1;
  held[1].closing=true;
  if (!Check(!rc_ui::MoveTabTo(held,1,3),"native_closing_intermediate")) return 1;
  held[1].closing=false;
  if (!Check(held[0].id==1 && original.marker==879,"native_failure_atomic")) return 1;

  rc_ui::TabDragIntent gesture;
  if (!Check(!gesture.Begin(-1,0,0),"invalid_gesture_source")) return 1;
  if (!Check(gesture.Begin(1,100,100),"gesture_press")) return 1;
  gesture.Observe(107,107);
  if (!Check(gesture.Complete(2)==-1,"threshold_guard")) return 1;
  if (!Check(gesture.Begin(1,100,100),"gesture_restart")) return 1;
  gesture.Observe(108,100);
  if (!Check(gesture.Complete(2)==1,"gesture_drop")) return 1;
  if (!Check(gesture.Complete(3)==-1,"repeated_release_guard")) return 1;
  if (!Check(gesture.Begin(1,100,100),"gesture_cancel_begin")) return 1;
  gesture.Cancel();
  if (!Check(gesture.Complete(2)==-1,"gesture_cancel")) return 1;
  if (!Check(gesture.Begin(1,0,0),"gesture_outside_begin")) return 1;
  gesture.Observe(15,0);
  if (!Check(gesture.Complete(-1)==-1,"outside_drop")) return 1;
  if (!Check(rc_ui::TabTitleContains(105,205,100,200,50,40),"hit_test")) return 1;
  if (!Check(!rc_ui::TabTitleContains(150,205,100,200,50,40),"hit_right_exclusive")) return 1;
  if (!Check(!rc_ui::TabTitleContains(101,200,100,200,0,40),"zero_width")) return 1;
  if (!Check(!rc_ui::TabTitleContains(101,200,100,200,40,-1),"negative_height")) return 1;
  if (!Check(!rc_ui::TabTitleContains(2147483647,2147483647,
                       2147483640,2147483640,1,1),"overflow_robust")) return 1;
  std::cout << "TAB_ORDER_CONTRACT_PASS checks=" << tests << "\n";
  return 0;
}
