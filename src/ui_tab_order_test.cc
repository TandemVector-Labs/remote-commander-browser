#include "ui_tab_order.h"
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
  rc_ui::TabDragIntent drag;
  if (!Check(!drag.Begin(-1,0,0),"reject_invalid_source")) return 1;
  if (!Check(drag.Begin(2,100,100),"gesture_begin")) return 1;
  if (!Check(!drag.Move(103,103),"below_threshold")) return 1;
  if (!Check(drag.Release(3)==-1,"no_accidental_click_reorder")) return 1;
  if (!Check(drag.Begin(2,100,100) && drag.Move(108,100),"threshold")) return 1;
  if (!Check(drag.Release(3)==2,"gesture_valid_release")) return 1;
  if (!Check(drag.Release(3)==-1,"no_duplicate_release")) return 1;
  if (!Check(drag.Begin(2,100,100) && drag.Move(100,108),"vertical_movement")) return 1;
  if (!Check(drag.Release(2)==-1,"same_tab_rejected")) return 1;
  if (!Check(drag.Begin(2,100,100),"restart_gesture")) return 1;
  drag.Cancel();
  if (!Check(!drag.Move(500,500) && drag.Release(3)==-1,"cancel")) return 1;
  const int* const identity = tabs[1].browser_resource.get();
  if (!Check(rc_ui::MoveTabTo(tabs,2,3),"multi_target_move")) return 1;
  if (!Check(tabs[2].id==2 && tabs[2].browser_resource.get()==identity,"multi_move_retains_browser")) return 1;
  if (!Check(rc_ui::MoveTabTo(tabs,2,1),"reverse_move")) return 1;
  if (!Check(tabs[0].id==2 && tabs[0].browser_resource.get()==identity,"reverse_identity")) return 1;
  if (!Check(!rc_ui::MoveTabTo(tabs,2,2),"same_target")) return 1;
  if (!Check(!rc_ui::MoveTabTo(tabs,2,99),"missing_target")) return 1;
  tabs[1].closing=true;
  if (!Check(!rc_ui::MoveTabTo(tabs,2,3),"closing_intermediate")) return 1;
  tabs[1].closing=false;
  std::cout << "TAB_ORDER_CONTRACT_PASS checks=" << tests << "\n";
  return 0;
}
