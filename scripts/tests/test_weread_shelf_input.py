"""Compile production shelf gestures with input/layout collaborators, without hardware."""
import unittest

from test_reading_ui_regressions import ROOT, WEREAD, method, run_cpp


class WeReadShelfInputTest(unittest.TestCase):
    def test_release_only_taps_and_holds(self):
        source = (WEREAD / "WeReadActivity.cpp").read_text()
        navigation = (ROOT / "src/util/ButtonNavigator.cpp").read_text()
        program = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <initializer_list>
unsigned long ticks=0;
unsigned long millis() { return ticks; }
constexpr unsigned long kShelfPageHoldMs=700;
constexpr int kNoShelfSelection=-1;
struct Rect {};
struct Renderer {};
int capacity=9;
struct Layout { int itemsPerPage; };
Layout shelfGridLayout(Renderer&,Rect,int,int) { return {capacity}; }
int weReadShelfIndexFromPoint(Rect,Layout,int,int,int,int) { return -1; }
struct UITheme {
  int contentSidePadding=0,verticalSpacing=0;
  const UITheme& getMetrics() const { return *this; }
  static const UITheme& getInstance() { static UITheme theme; return theme; }
};
struct MappedInputManager {
  enum class Button { NavPrevious=1,NavNext=2,Confirm=4 };
  enum class SwipeDir { None,Up,Down,Left,Right };
  int pressed=0,released=0,held=0;
  unsigned long heldMs=0;
  bool wasPressed(Button b) const { return pressed & int(b); }
  bool wasReleased(Button b) const { return released & int(b); }
  bool isPressed(Button b) const { return held & int(b); }
  unsigned long getHeldTime() const { return heldMs; }
  bool wasScreenTouchDown(int&,int&) const { return false; }
  bool wasScreenTapped(int&,int&) const { return false; }
  SwipeDir wasSwipe() const { return SwipeDir::None; }
};
struct ButtonNavigator {
  static int nextIndex(int,int);
  static int previousIndex(int,int);
  static int nextPageIndex(int,int,int);
  static int previousPageIndex(int,int,int);
};
struct WeReadActivity {
  enum class ShelfNavigationGesture { Idle,PreviousPressed,NextPressed,PreviousPageHandled,NextPageHandled };
  enum class MainFocus { Tabs,Content };
  uint32_t shelfCount_=2,shelfLastPageTurnAt_=0;
  std::atomic<int> shelfSelected_{0};
  ShelfNavigationGesture shelfNavigationGesture_=ShelfNavigationGesture::Idle;
  std::atomic<MainFocus> mainFocus_{MainFocus::Content};
  MappedInputManager mappedInput;
  Renderer renderer;
  int moves=0,activations=0;
  Rect mainContentBounds() const { return {}; }
  void requestUpdate() {}
  void moveShelfSelection(int index,int) { shelfSelected_=index; ++moves; }
  void resetShelfCoverLoading() {}
  void activateSelected() { ++activations; }
  void handleShelfInput();
  void frame(int pressed,int released,int held,unsigned long time) {
    mappedInput={pressed,released,held,time}; ticks=time; handleShelfInput();
  }
};
'''
        for name in ("int ButtonNavigator::nextIndex", "int ButtonNavigator::previousIndex",
                     "int ButtonNavigator::nextPageIndex", "int ButtonNavigator::previousPageIndex"):
            program += method(navigation, name) + "\n"
        program += method(source, "constexpr int previousShelfIndexOrTab") + "\n"
        program += method(source, "void WeReadActivity::handleShelfInput()")
        program += r'''
int main() {
  for (int pageSize : {9,10}) {
    capacity=pageSize;
    // Release-only hardware reports both edges in the same snapshot.
    WeReadActivity tap;
    tap.frame(2,2,0,200);
    assert(tap.shelfSelected_==1 && tap.moves==1);
    tap.frame(0,0,0,210);
    assert(tap.moves==1 && tap.activations==0);
    tap.frame(1,1,0,220);
    assert(tap.shelfSelected_==0 && tap.moves==2);
    tap.frame(1,1,0,230);
    assert(tap.mainFocus_==WeReadActivity::MainFocus::Tabs);

    for (int count : {0,1,2,pageSize}) {
      WeReadActivity hold;
      hold.shelfCount_=count;
      hold.frame(2,0,2,0);
      hold.frame(0,0,2,700);
      hold.frame(0,0,2,1400);
      assert(hold.moves==0);
      hold.frame(0,2,0,1500);
      assert(hold.moves==1 && hold.shelfSelected_==(count>1 ? 1 : 0));
      hold.frame(0,0,0,1600);
      assert(hold.moves==1);

      WeReadActivity previous;
      previous.shelfCount_=count;
      previous.frame(1,0,1,0);
      previous.frame(0,0,1,700);
      previous.frame(0,1,0,800);
      assert(previous.mainFocus_==WeReadActivity::MainFocus::Tabs);
    }
    WeReadActivity pages;
    pages.shelfCount_=3*pageSize;
    pages.frame(2,0,2,0);
    pages.frame(0,0,2,700);
    assert(pages.shelfSelected_==pageSize && pages.moves==1);
    pages.frame(0,0,2,800);
    assert(pages.moves==1);
    pages.frame(0,0,2,1400);
    assert(pages.shelfSelected_==2*pageSize && pages.moves==2);
    pages.frame(0,2,0,1500);
    assert(pages.moves==2 && pages.activations==0);
    pages.frame(1,0,1,0);
    pages.frame(0,0,1,700);
    assert(pages.shelfSelected_==pageSize && pages.moves==3);
    pages.frame(0,1,0,800);
    assert(pages.moves==3);
  }
}
'''
        run_cpp(program)


if __name__ == "__main__":
    unittest.main()
