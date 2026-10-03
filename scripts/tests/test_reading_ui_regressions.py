"""Run production home geometry, headers, resource errors and sync refresh with small seams."""
from pathlib import Path
import importlib.util
import json
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WEREAD = ROOT / 'src/activities/apps/weread/webapi'


def method(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def run_cpp(program, include_dirs=(), defines=()):
    with tempfile.TemporaryDirectory(prefix='reading-ui-') as directory:
        cpp = Path(directory) / 'check.cpp'
        exe = Path(directory) / 'check'
        cpp.write_text(f'#include "{ROOT}/src/components/UiHighDpiProfile.h"\n' + program)
        subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        *('-I'+str(path) for path in include_dirs), *('-D'+define for define in defines), str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class ReadingUiRegressionTest(unittest.TestCase):
    def test_splash_uses_font_heights_and_keeps_version_inside_safe_area(self):
        source = (ROOT / 'src/components/themes/BaseTheme.cpp').read_text()
        program = r'''
#include <cassert>
#include <cstring>
#include <initializer_list>
#include <vector>
struct Rect { int x,y,width,height; };
constexpr int UI_12_FONT_ID=1, UI_10_FONT_ID=2, SMALL_FONT_ID=3;
namespace EpdFontFamily { enum Style { REGULAR, BOLD }; }
constexpr int STR_CROSSPOINT=0;
const char* tr(int) {return "CrossMux";}
const unsigned char Logo120[1]{};
struct GfxRenderer {
 Rect safe; int width=684,height=1216,titleHeight=46,statusHeight=40,smallHeight=34;
 mutable std::vector<Rect> blocks;
 int getScreenWidth() const {return width;}
 int getScreenHeight() const {return height;}
 int getLineHeight(int font) const {return font==1?titleHeight:font==2?statusHeight:smallHeight;}
 void clearScreen() const {blocks.clear();}
 void drawImage(const unsigned char*,int x,int y,int w,int h) const {blocks.push_back({x,y,w,h});}
 void drawCenteredText(int font,int y,const char* text,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR) const {
   int width=std::strlen(text)*14;
   blocks.push_back({(getScreenWidth()-width)/2,y,width,getLineHeight(font)});
 }
};
struct UITheme {
 static UITheme& getInstance() {static UITheme instance;return instance;}
 Rect getScreenSafeArea(const GfxRenderer& r) {return r.safe;}
 static void drawCenteredText(const GfxRenderer& r,Rect safe,int font,int y,const char* text,bool=true,
                              EpdFontFamily::Style=EpdFontFamily::REGULAR) {
   int width=std::strlen(text)*14;
   r.blocks.push_back({safe.x+(safe.width-width)/2,y,width,r.getLineHeight(font)});
 }
};
struct BaseTheme {static void drawSplash(const GfxRenderer&,const char*,const char*);};
''' + method(source, 'void BaseTheme::drawSplash(') + r'''
int main() {
 for(Rect size : {Rect{0,0,684,1216},Rect{0,0,600,1000}})
 for(int orientation=0;orientation<4;++orientation)
 for(int extra : {0,12}) for(const char* status : {"Booting...","Sleeping..."})
 for(const char* version : std::initializer_list<const char*>{nullptr,"1.6.5-readpico-rc+abcdef123456"}) {
   GfxRenderer r;
   r.width=orientation%2?size.height:size.width;
   r.height=orientation%2?size.width:size.height;
   int top=orientation==2?8:5,right=orientation==1?8:5,bottom=orientation==0?8:5,left=orientation==3?8:5;
   Rect safe{left,top,r.width-left-right,r.height-top-bottom};
   r.safe=safe;
   r.titleHeight+=extra; r.statusHeight+=extra; r.smallHeight+=extra;
   BaseTheme{}.drawSplash(r,status,version);
   assert(r.blocks.size()==(version?4:3));
   if(UiHighDpiProfile::enabled) {
     for(auto rect : r.blocks) {
       assert(rect.x>=safe.x && rect.x+rect.width<=safe.x+safe.width);
       assert(rect.y>=safe.y && rect.y+rect.height<=safe.y+safe.height);
     }
     assert(r.blocks[1].y-r.blocks[0].y-r.blocks[0].height>=24);
     assert(r.blocks[2].y-r.blocks[1].y-r.blocks[1].height>=12);
     if(version) {
       assert(r.blocks[3].y-r.blocks[2].y-r.blocks[2].height>=12);
       assert(safe.y+safe.height-r.blocks[3].y-r.blocks[3].height==32);
     }
   } else {
     assert(r.blocks[0].y==(r.getScreenHeight()-120)/2);
     assert(r.blocks[1].y==r.getScreenHeight()/2+70);
     assert(r.blocks[2].y==r.getScreenHeight()/2+95);
     if(version) assert(r.blocks[3].y==r.getScreenHeight()-30);
   }
 }
}
'''
        for defines in ((), ('CROSSMUX_UI_PROFILE_HIGH_DPI',)):
            run_cpp(program, defines=defines)

    def test_high_dpi_controls_share_geometry_and_leave_gaps(self):
        run_cpp(r'''
#include <cassert>
#include <initializer_list>
#include "activities/MainTab.h"
#include "InxItemLayout.h"
int main() {
  for (const Rect safe : {Rect{5,5,674,1203}, Rect{5,8,1203,674},
                         Rect{5,8,674,1203}, Rect{8,5,1203,674},
                         Rect{5,5,590,987}, Rect{5,8,987,590},
                         Rect{5,8,590,987}, Rect{8,5,987,590}}) {
    for (bool bottom : {false,true}) {
      auto layout=MainTabs::layout(safe,0,96,bottom,bottom?48:0);
      assert(layout.tabBar.y>=safe.y && layout.tabBar.y+96<=safe.y+safe.height);
      assert(layout.content.width==safe.width && layout.content.height>0);
      if (bottom) {
        assert(layout.content.y-layout.statusBar.y-layout.statusBar.height>=12);
        assert(layout.tabBar.y-layout.content.y-layout.content.height>=12);
      } else assert(layout.content.y>=layout.tabBar.y+layout.tabBar.height);
      for (int tab=0;tab<5;++tab) {
        auto bounds=MainTabs::tabBounds(tab,safe.width);
        assert(bounds.right-bounds.left>=96);
        assert(MainTabs::fromX((bounds.left+bounds.right)/2,safe.width)==MainTabs::values[tab]);
        assert(MainTabs::fromX(bounds.right,safe.width)==MainTab::None);
        if (tab<4) assert(MainTabs::tabBounds(tab+1,safe.width).left-bounds.right>=6);
      }
    }
    for (int slot=0;slot<12;++slot) {
      auto cell=InxGridGeometry::cellBounds(slot,safe.width,safe.height);
      assert(InxGridGeometry::indexFromPoint(cell.x+cell.width/2,cell.y+cell.height/2,
                                           safe.width,safe.height,0,12)==slot);
      assert(InxGridGeometry::indexFromPoint(cell.x+cell.width,cell.y+cell.height/2,
                                           safe.width,safe.height,0,12)==-1);
      if (slot%3<2) {
        auto next=InxGridGeometry::cellBounds(slot+1,safe.width,safe.height);
        assert(next.x-cell.x-cell.width>=12);
      }
    }
  }
}
''', include_dirs=(ROOT / 'src',), defines=('CROSSMUX_UI_PROFILE_HIGH_DPI',))

    def test_high_dpi_status_text_and_battery_fit_top_tab_footer(self):
        source = (ROOT / 'src/components/themes/inx/InxTheme.cpp').read_text()
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <initializer_list>
struct Rect { int x,y,width,height; };
constexpr int SMALL_FONT_ID=1, STATUS_NUMERIC_FONT_ID=2;
struct CrossPointSettings {
  enum { INX_TAB_TOP, INX_TAB_BOTTOM };
  enum class HIDE_BATTERY_PERCENTAGE { SHOW,HIDE_ALWAYS };
};
struct { int inxTabPosition=0, clockFormat=0; CrossPointSettings::HIDE_BATTERY_PERCENTAGE hideBatteryPercentage{}; } SETTINGS;
struct UITheme {
  struct Metrics { int batteryWidth=32,batteryHeight=20; };
  static UITheme& getInstance() { static UITheme theme; return theme; }
  const Metrics& getMetrics() { static Metrics metrics; return metrics; }
};
namespace TimeUtils { void formatCurrentTime(char*,unsigned long,bool) {} }
struct GfxRenderer {
  int width=684,height=1216; mutable Rect clip{};
  struct ClipScope { ClipScope(const GfxRenderer& r,int x,int y,int w,int h) { r.clip={x,y,w,h}; } };
  int getScreenWidth() const { return width; } int getScreenHeight() const { return height; }
  int getLineHeight(int) const { return 34; }
  void drawText(int,int,int,const char*) const {}
};
struct InxTheme {
  void drawMainTabStatusBar(const GfxRenderer&,Rect) const;
  void drawBatteryRight(const GfxRenderer& r,Rect rect,bool,int font) const {
    assert(font==SMALL_FONT_ID && rect.y>=r.clip.y);
    assert(rect.y+34<=r.clip.y+r.clip.height);
    assert(rect.y+8+rect.height<=r.clip.y+r.clip.height);
    assert(rect.x>=r.clip.x && rect.x+rect.width<=r.clip.x+r.clip.width);
  }
};
''' + method(source, 'void InxTheme::drawMainTabStatusBar(') + r'''
int main() {
  for (auto size : {Rect{0,0,684,1216},Rect{0,0,1216,684},
                    Rect{0,0,600,1000},Rect{0,0,1000,600}}) {
    GfxRenderer renderer; renderer.width=size.width; renderer.height=size.height;
    SETTINGS.inxTabPosition=CrossPointSettings::INX_TAB_TOP;
    InxTheme{}.drawMainTabStatusBar(renderer,{5,size.height-48,size.width-10,40});
    SETTINGS.inxTabPosition=CrossPointSettings::INX_TAB_BOTTOM;
    InxTheme{}.drawMainTabStatusBar(renderer,{5,5,size.width-10,48});
  }
}
''', defines=('CROSSMUX_UI_PROFILE_HIGH_DPI',))

    def test_high_dpi_builtin_font_cache_identity(self):
        settings = (ROOT / 'src/CrossPointSettings.cpp').read_text()
        sizes = (ROOT / 'src/ReaderFontSizes.cpp').read_text()
        program = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <initializer_list>
#include "fontIds.h"
constexpr uint8_t BUILTIN_READER_POINT_SIZES[]={12};
struct CrossPointSettings {
  enum { NOTOSANS=1 };
  char sdFontFamilyName[8]{}; void* sdFontResolverCtx=nullptr;
  int (*sdFontIdResolver)(void*,const char*,uint8_t)=nullptr;
  uint8_t fontPointSize=12; int fontFamily=NOTOSANS;
  int getReaderFontId() const;
};
''' + method(sizes, 'uint8_t snapToNearestPointSize(') + method(settings, 'int CrossPointSettings::getReaderFontId(') + r'''
int main() {
  CrossPointSettings settings;
  for (int family : {0,1}) for (uint8_t size : {12,14,18}) {
    settings.fontFamily=family; settings.fontPointSize=size;
    int id=settings.getReaderFontId();
    if (UiHighDpiProfile::enabled) {
      assert(id==UiHighDpiProfile::reader12FontId);
      assert(id==0x4738000C);
      assert(id!=0x4737000C && id!=0x4737000E);
      assert(id!=NOTOSANS_12_FONT_ID && id!=NOTOSERIF_12_FONT_ID);
      assert(id!=NOTOSANS_14_FONT_ID && id!=NOTOSERIF_14_FONT_ID);
    } else assert(id==(family==1?NOTOSANS_12_FONT_ID:NOTOSERIF_12_FONT_ID));
    assert(settings.fontPointSize==size);
  }
  settings.sdFontFamilyName[0]='A';
  settings.sdFontIdResolver=[](void*,const char*,uint8_t) { return 123456; };
  assert(settings.getReaderFontId()==123456);
}
'''
        for defines in ((), ('CROSSMUX_UI_PROFILE_HIGH_DPI',)):
            run_cpp(program, include_dirs=(ROOT / 'src',), defines=defines)

    def test_main_tab_content_starts_below_shared_header(self):
        files = (ROOT / 'src/activities/home/FileBrowserActivity.cpp').read_text()
        settings = (ROOT / 'src/activities/settings/SettingsActivity.cpp').read_text()
        apps = (ROOT / 'src/activities/apps/AppsMenuActivity.cpp').read_text()
        stats = (ROOT / 'src/activities/apps/reading-stats/ReadingStatsActivity.cpp').read_text()
        # Run the production layout preambles; unrelated data/row rendering stays
        # covered by the existing style and FreeInkUI list checks.
        file_layout = method(files, 'void FileBrowserActivity::buildScreen(').split('  // Full path band', 1)[0] + '}'
        settings_preamble = method(settings, 'void SettingsActivity::buildScreen(').split('  if (usesAccordion())', 1)[0]
        settings_layout = 'void SettingsActivity::buildScreen(UiScreen& screen) {\n' + settings_preamble[
            settings_preamble.index('  const auto& metrics'):].replace(
            '  const bool boldChineseCategories = usesAccordion() && I18N.getLanguage() == Language::ZH_CN;\n', '').replace('  const auto& metrics = UITheme::getInstance().getMetrics();\n', '') + '}'
        stats_layout = method(stats, 'void ReadingStatsActivity::renderInx(').split('  const auto& books', 1)[0] + ' recorded=content; }'
        program = r'''
#include <cassert>
#include <vector>
#include "components/SubpageLayout.h"
#include "components/themes/inx/InxTheme.h"
#include "InxItemLayout.h"
#define tr(key) #key
class GfxRenderer {
 public:
  int width=684,height=1216;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  void clearScreen() {}
};
struct UITheme {
  bool tabs=true;
  Rect safe{}, empty{};
  ThemeMetrics metrics=InxMetrics::values;
  static UITheme& getInstance() { static UITheme theme; return theme; }
  const ThemeMetrics& getMetrics() const { return metrics; }
  Rect getScreenSafeArea(const GfxRenderer&,bool,bool) { return safe; }
  static void drawCenteredWrappedText(const GfxRenderer&,Rect rect,int,const char*,int) { getInstance().empty=rect; }
};
namespace fui {
struct Insets { int top,right,bottom,left; };
struct ListProps { const int* items=nullptr; unsigned count=0; int action=0,inputMask=0; };
constexpr int InputTouch=1;
}
struct UiScreen {
  int top=-1;
  bool absolute=false;
  void setContentMarginFromScreen(fui::Insets margin) { top=margin.top; absolute=true; }
  void setContentMargin(fui::Insets margin) { top=margin.top; absolute=false; }
  void spacer(int gap) { top+=gap; }
  void list(fui::ListProps) {}
};
struct Page {
  GfxRenderer renderer;
  bool usesMainTabBar() const { return UITheme::getInstance().tabs; }
  Rect pageContentRect() const {
    auto& theme=UITheme::getInstance();
    const int top=theme.metrics.topPadding+theme.metrics.headerHeight;
    const Rect safe=theme.safe;
    return usesMainTabBar() ? Rect{safe.x,safe.y+top,safe.width,safe.height-top}
                           : Rect{0,top,renderer.width,renderer.height-top-theme.metrics.buttonHintsHeight};
  }
};
struct FileBrowserActivity : Page { void buildScreen(UiScreen&); };
struct SettingsActivity : Page { void buildScreen(UiScreen&); };
struct ReadingStatsActivity : Page {
  bool renderedCoverMissing=false;
  Rect recorded{};
  void drawPageHeader(Rect,const char*) {}
  void renderInx();
};
struct AppsMenuActivity : Page {
  struct { int selected=0; } nav;
  int count=12;
  bool icons=true;
  std::vector<int> rowItems;
  Rect recorded{};
  static constexpr int ACTION_ROW=1;
  int getVisibleAppCount() const { return count; }
  bool usesIconLayout() const { return icons; }
  bool showMainTabContentSelection() const { return false; }
  void drawIconGrid(Rect rect,int,bool) { recorded=rect; }
  void syncListViewport(UiScreen&,fui::ListProps&) {}
  Rect appContentRect() const;
  int iconIndexFromPoint(int,int) const;
  void buildScreen(UiScreen&);
};
''' + method(apps, 'Rect AppsMenuActivity::appContentRect(') + file_layout + settings_layout + stats_layout + method(apps, 'int AppsMenuActivity::iconIndexFromPoint(') + method(apps, 'void AppsMenuActivity::buildScreen(') + r'''
int main() {
  auto& theme=UITheme::getInstance();
  for (const Rect safe : {Rect{5,5,674,1203},Rect{8,5,1203,674},Rect{5,8,674,1203},Rect{5,5,1203,674},Rect{0,0,480,800}}) {
    theme.safe=safe;
    const bool portrait=safe.width<safe.height;
    const int width=safe.x+safe.width+(safe.x==0 ? 0 : safe.x==8 ? 5 : portrait ? 5 : 8);
    const int height=safe.y+safe.height+(safe.y==0 ? 0 : safe.y==8 ? 5 : portrait ? 8 : 5);
    for (bool tabs : {true,false}) {
      theme.tabs=tabs;
      const int bottom=(tabs ? safe.y : 0)+theme.metrics.topPadding+theme.metrics.headerHeight;
      FileBrowserActivity files; files.renderer.width=width; files.renderer.height=height;
      UiScreen fileScreen; files.buildScreen(fileScreen);
      assert(fileScreen.absolute && fileScreen.top==bottom+theme.metrics.verticalSpacing);
      SettingsActivity settings; settings.renderer=files.renderer;
      UiScreen settingsScreen; settings.buildScreen(settingsScreen);
      assert(settingsScreen.absolute && settingsScreen.top==bottom);
      ReadingStatsActivity stats; stats.renderer=files.renderer; stats.renderInx();
      assert(stats.recorded.y==bottom+6 && stats.recorded.y+stats.recorded.height==(tabs ? safe.y+safe.height : height-theme.metrics.buttonHintsHeight)-6);
      AppsMenuActivity apps; apps.renderer=files.renderer;
      UiScreen screen; apps.buildScreen(screen);
      assert(apps.recorded.y==bottom+theme.metrics.verticalSpacing);
      assert(apps.recorded.y+apps.recorded.height==(tabs ? safe.y+safe.height : height-theme.metrics.buttonHintsHeight)-theme.metrics.verticalSpacing);
      const Rect grid=apps.recorded;
      for (int slot=0;slot<InxGridGeometry::itemsPerPage;++slot) {
        const Rect cell=InxGridGeometry::cellBounds(slot,grid.width,grid.height);
        assert(apps.iconIndexFromPoint(grid.x+cell.x,grid.y+cell.y)==slot);
        assert(apps.iconIndexFromPoint(grid.x+cell.x-1,grid.y+cell.y)==-1);
      }
      assert(apps.iconIndexFromPoint(grid.x,grid.y-1)==-1);
      assert(apps.iconIndexFromPoint(grid.x,grid.y+grid.height)==-1);
      apps.icons=false; apps.buildScreen(screen);
      assert(screen.top==bottom+theme.metrics.verticalSpacing && screen.absolute);
      apps.count=0; apps.buildScreen(screen);
      assert(theme.empty.y==grid.y && theme.empty.height==grid.height);
    }
  }
}
'''
        run_cpp(program, include_dirs=(ROOT / 'src', ROOT / 'lib/hal'))

    def test_all_main_tabs_share_drawing_and_input_geometry(self):
        activity = (ROOT / 'src/activities/Activity.cpp').read_text()
        manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
        program = r'''
#include <cassert>
#include <utility>
#include "components/SubpageLayout.h"
#include "components/themes/inx/InxTheme.h"
class GfxRenderer {
 public:
  int getScreenWidth() const { return 1300; }
  int getScreenHeight() const { return 1300; }
  void getOrientedViewableTRBL(int* t,int* r,int* b,int* l) const { *t=*r=*b=*l=0; }
};
struct UITheme {
  bool tabs=true;
  Rect safe{}, drawn{};
  ThemeMetrics metrics=InxMetrics::values;
  MainTab selected=MainTab::None;
  int headers=0;
  static UITheme& getInstance() { static UITheme theme; return theme; }
  bool hasMainTabs() const { return tabs; }
  UITheme& getTheme() { return *this; }
  const ThemeMetrics& getMetrics() const { return metrics; }
  Rect getScreenSafeArea(const GfxRenderer&,bool,bool=false) { return safe; }
  void drawMainTabStatusBar(const GfxRenderer&,Rect) {}
  void drawMainTabBar(const GfxRenderer&,Rect rect,MainTab tab) { drawn=rect; selected=tab; }
  void drawHeader(const GfxRenderer&,Rect rect,const char*,const char*) { drawn=rect; ++headers; }
};
#define GUI UITheme::getInstance().getTheme()
struct Activity {
  GfxRenderer renderer;
  MainTab tab=MainTab::Recent;
  bool bottom=false;
  struct { bool hasTouch() const { return true; } } mappedInput;
  bool usesMainTabBar() const;
  bool mainTabsAtBottom() const { return bottom; }
  bool hasMainTabStatusBar() const { return usesMainTabBar() && bottom; }
  MainTabLayout mainTabLayout() const;
  MainTab mainTab() const { return tab; }
  bool mainTabBackReturnsToTabs() const { return false; }
  void selectMainTabContentEdge(MainTabContentEdge) {}
  void drawPageHeader(const Rect&,const char*,const char* =nullptr) const;
};
struct MappedInputManager {
  enum class Button { None,Left,Right,Up,Down,Confirm,Back };
  bool tapped=true,down=false;
  int x=0,y=0;
  Button released=Button::None;
  bool wasScreenTapped(int& tx,int& ty) { tx=x;ty=y;return tapped; }
  bool wasScreenTouchDown(int& tx,int& ty) { tx=x;ty=y;return down; }
  bool wasReleased(Button b) { return b==released; }
  bool wasPressed(Button) { return false; }
  bool isPressed(Button) { return false; }
};
struct { bool standbyShortcutEnabled=false; } SETTINGS;
struct ActivityManager {
  Activity* currentActivity=nullptr;
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  MainTabFocus mainTabFocus=MainTabFocus::Content;
  bool mainTabEntryReleasePending=false;
  MainTab destination=MainTab::None;
  int updates=0;
  void goToMainTab(MainTab tab) { destination=tab; }
  void requestUpdate() { ++updates; }
  void goToStandby() { assert(false); }
  bool handleMainTabInput();
};
''' + method(activity, 'bool Activity::usesMainTabBar(') + method(activity, 'MainTabLayout Activity::mainTabLayout(') + method(activity, 'void Activity::drawPageHeader(') + method(manager, 'bool ActivityManager::handleMainTabInput(') + r'''
int main() {
  auto& theme=UITheme::getInstance();
  for (const Rect safe : {Rect{5,5,674,1203},Rect{8,5,1203,674},Rect{5,8,674,1203},Rect{5,5,1203,674},Rect{0,0,480,800}}) {
    theme.safe=safe;
    for (bool bottom : {false,true}) for (MainTab current : MainTabs::values) {
      Activity page; page.tab=current; page.bottom=bottom;
      const Rect expected=page.mainTabLayout().tabBar;
      page.drawPageHeader(Rect{0,0,999,66},"title");
      assert(theme.drawn.x==expected.x && theme.drawn.y==expected.y);
      assert(theme.drawn.width==expected.width && theme.drawn.height==expected.height && theme.selected==current);
      for (int i=0;i<5;++i) {
        const int left=expected.x+expected.width*i/5, right=expected.x+expected.width*(i+1)/5;
        for (int x=left;x<right;++x) {
          ActivityManager m; m.currentActivity=&page;
          m.mappedInput.x=x; m.mappedInput.y=expected.y;
          assert(m.handleMainTabInput());
          const MainTab target=MainTabs::fromX(x-expected.x,expected.width);
          assert(target==MainTab::None ? m.updates==0 && m.destination==MainTab::None
                 : current==target ? m.updates==1 && m.destination==MainTab::None : m.destination==target);
        }
      }
      for (auto point : {std::pair{expected.x-1,expected.y},std::pair{expected.x+expected.width,expected.y},
                         std::pair{expected.x,expected.y-1},std::pair{expected.x,expected.y+expected.height}}) {
        ActivityManager m; m.currentActivity=&page; m.mainTabFocus=MainTabFocus::Tabs;
        m.mappedInput.x=point.first; m.mappedInput.y=point.second;
        assert(!m.handleMainTabInput() && m.destination==MainTab::None && m.mainTabFocus==MainTabFocus::Content);
        m.mainTabFocus=MainTabFocus::Tabs; m.mappedInput.tapped=false; m.mappedInput.down=true;
        assert(!m.handleMainTabInput() && m.mainTabFocus==MainTabFocus::Content);
      }
      for (auto button : {MappedInputManager::Button::Left,MappedInputManager::Button::Right}) {
        ActivityManager m; m.currentActivity=&page; m.mainTabFocus=MainTabFocus::Tabs;
        m.mappedInput.tapped=false; m.mappedInput.released=button;
        assert(m.handleMainTabInput());
        assert(m.destination==MainTabs::adjacent(current,button==MappedInputManager::Button::Left ? -1 : 1));
      }
    }
  }
  theme.tabs=false;
  Activity page; const Rect legacy{0,9,684,66}; page.drawPageHeader(legacy,"legacy","version");
  assert(theme.headers==1 && theme.drawn.x==0 && theme.drawn.y==9 && theme.drawn.width==684);
  ActivityManager m; m.currentActivity=&page; assert(!m.handleMainTabInput());
  theme.tabs=true; page.tab=MainTab::None; page.drawPageHeader(legacy,"picker");
  assert(theme.headers==2 && !m.handleMainTabInput());
}
'''
        run_cpp(program, include_dirs=(ROOT / 'src', ROOT / 'lib/hal'))

    def test_home_standby_owns_a_complete_back_gesture_across_transitions(self):
        source = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
        header = (ROOT / 'src/activities/ActivityManager.h').read_text()
        state_start = header.index('  enum class StandbyBackState')
        state_end = header.index(';', header.index('  StandbyBackState standbyBackState', state_start)) + 1
        program = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "activities/MainTab.h"
#include "components/HeaderBackTapTarget.h"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
enum { eIncrement };
void xTaskNotify(int,int,int) {}
struct { void clearTouchTapEvent() {} } gpio;
struct RenderLock {
  static inline bool busy=false;
  static bool peek() { return busy; }
  void unlock() {}
};
struct ActivityResult {};
struct MappedInputManager {
  enum class Button { None,Back,Confirm,Left,Right,Up,Down };
  bool pressed=false,released=false,held=false,suppressed=false;
  bool tap=false,down=false,home=false,light=false;
  int x=0,y=0;
  Button otherPress=Button::None,otherRelease=Button::None;
  bool wasPressed(Button b) const { return b==Button::Back ? pressed : b==otherPress; }
  bool wasReleased(Button b) const { return b==Button::Back ? released : b==otherRelease; }
  bool isPressed(Button b) const { return b==Button::Back && held; }
  bool consumeSuppressedRelease() { return std::exchange(suppressed,false); }
  bool wasScreenTapped(int& tx,int& ty) const { tx=x;ty=y;return tap; }
  bool wasScreenTouchDown(int& tx,int& ty) const { tx=x;ty=y;return down; }
  bool wasHomeGesture() const { return home; }
  bool wasLightPanelGesture() const { return light; }
  bool hasTouch() const { return true; }
  void resetHomeButtonInput() {}
};
struct Activity {
  std::string name="Home";
  bool home=true,tabs=false,exclusive=false;
  MainTab tab=MainTab::Recent;
  ActivityResult result;
  std::function<void(ActivityResult)> resultHandler;
  std::function<void()> action;
  int loops=0;
  virtual ~Activity()=default;
  void onEnter() {}
  void onExit() {}
  void loop() { ++loops; if(action) action(); }
  bool requiresExclusiveStorageLoop() const { return exclusive; }
  bool isHomeActivity() const { return home; }
  bool handleHomeGesture() const { return false; }
  bool usesMainTabBar() const { return tabs; }
  MainTab mainTab() const { return tab; }
  MainTabLayout mainTabLayout() const { return {Rect{0,700,480,56},Rect{0,0,480,28},Rect{0,34,480,650}}; }
  bool mainTabBackReturnsToTabs() const { return true; }
  void selectMainTabContentEdge(MainTabContentEdge) {}
};
std::unique_ptr<Activity> page(const char* theme) {
  auto p=std::make_unique<Activity>();
  p->tabs=std::string(theme)=="INX"; p->home=!p->tabs;
  p->name=p->tabs?"InxRecent":"Home";
  return p;
}
struct FrontlightPanelActivity : Activity {
  FrontlightPanelActivity(int,MappedInputManager&) { home=false;name="FrontlightPanel"; }
};
template<class T,class... Args> auto makeUniqueNoThrow(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct { bool standbyShortcutEnabled=true; } SETTINGS;
struct ActivityManager {
''' + header[state_start:state_end] + r'''
  enum class PendingAction { None,Push,Pop,Replace };
  std::atomic<PendingAction> pendingAction{PendingAction::None};
  std::atomic<bool> requestedUpdate{false};
  std::unique_ptr<Activity> currentActivity,pendingActivity;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  MappedInputManager mappedInput;
  MainTabFocus mainTabFocus=MainTabFocus::Tabs;
  bool mainTabEntryReleasePending=false;
  int renderer=0,renderTaskHandle=0,standbyCalls=0;
  const char* theme;
  explicit ActivityManager(const char* value):theme(value) { replaceActivity(page(theme)); }
  void requestUpdate() { requestedUpdate=true; }
  void cancelIdleRender() {}
  void goHome() { mainTabFocus=MainTabFocus::Tabs;replaceActivity(page(theme)); }
  void goToMainTab(MainTab tab) {
    auto p=page("INX");p->tab=tab;replaceActivity(std::move(p));
  }
  void goToStandby() {
    ++standbyCalls;
    auto p=std::make_unique<Activity>();p->home=false;p->name="Standby";
    replaceActivity(std::move(p));
  }
  void resetHomeStandbyInput();
  bool handleHomeStandbyInput();
  bool handleMainTabInput();
  void replaceActivity(std::unique_ptr<Activity>&&);
  void pushActivity(std::unique_ptr<Activity>&&);
  void popActivity();
  void exitActivity(const RenderLock&);
  void loop();
  void tick(MappedInputManager input={}) {
    mappedInput=input;loop();
    if(pendingAction.load()!=PendingAction::None && !RenderLock::busy) {
      mappedInput={.held=input.held};loop();
    }
  }
};
''' + '\n'.join(method(source, name) for name in (
            'void ActivityManager::resetHomeStandbyInput(',
            'bool ActivityManager::handleHomeStandbyInput(',
            'bool ActivityManager::handleMainTabInput(',
            'void ActivityManager::replaceActivity(',
            'void ActivityManager::pushActivity(',
            'void ActivityManager::popActivity(',
            'void ActivityManager::exitActivity(',
            'void ActivityManager::loop(')) + r'''
void freshBack(ActivityManager& m) {
  m.tick({.pressed=true,.held=true});assert(m.standbyCalls==0);
  m.tick({.held=true});assert(m.standbyCalls==0);
  m.tick({.released=true});assert(m.standbyCalls==1);
  m.tick();assert(m.standbyCalls==1 && m.currentActivity->name=="Standby");
}
int main() {
  using Button=MappedInputManager::Button;
  for(const char* theme:{"INX","Classic","Carousel","Cover Grid"}) {
    ActivityManager stray(theme);
    stray.tick({.released=true});assert(stray.standbyCalls==0);
    freshBack(stray);

    // Both the queued request and actual activation cancel the old pair.
    ActivityManager replace(theme);
    replace.tick({.pressed=true,.held=true});
    auto* outgoing=replace.currentActivity.get();
    replace.replaceActivity(page(theme));
    RenderLock::busy=true;
    replace.tick({.held=true});assert(replace.currentActivity.get()==outgoing);
    RenderLock::busy=false;
    replace.tick({.pressed=true,.held=true}); // old owner's press edge at activation
    replace.tick({.released=true});assert(replace.standbyCalls==0);
    freshBack(replace);

    // Push a panel while armed, then return to the very same parent on press.
    ActivityManager pop(theme);
    auto* parent=pop.currentActivity.get();
    pop.tick({.pressed=true,.held=true});
    pop.pushActivity(std::make_unique<FrontlightPanelActivity>(0,pop.mappedInput));
    pop.tick({.held=true});assert(pop.stackActivities.back().get()==parent);
    pop.popActivity();pop.tick({.pressed=true,.held=true});
    assert(pop.currentActivity.get()==parent);
    pop.tick({.released=true});assert(pop.standbyCalls==0);
    freshBack(pop);

    // Crash report exits on press with an empty activity stack.
    ActivityManager crash(theme);
    crash.currentActivity->home=false;crash.currentActivity->tabs=false;
    crash.currentActivity->name="Crash";
    crash.currentActivity->action=[&crash] {
      if(crash.mappedInput.wasPressed(Button::Back)) crash.popActivity();
    };
    crash.tick({.pressed=true,.held=true});
    crash.tick({.released=true});assert(crash.standbyCalls==0);
    freshBack(crash);

    ActivityManager suppressed(theme);
    suppressed.tick({.pressed=true,.held=true});
    suppressed.tick({.released=true,.suppressed=true});
    suppressed.tick({.released=true});assert(suppressed.standbyCalls==0);
    freshBack(suppressed);

    ActivityManager disabled(theme);
    disabled.tick({.pressed=true,.held=true});
    SETTINGS.standbyShortcutEnabled=false;
    disabled.tick({.held=true});
    SETTINGS.standbyShortcutEnabled=true;
    disabled.tick({.released=true});assert(disabled.standbyCalls==0);
    SETTINGS.standbyShortcutEnabled=false;
    disabled.tick({.pressed=true,.released=true});assert(disabled.standbyCalls==0);
    SETTINGS.standbyShortcutEnabled=true;freshBack(disabled);

    // A completed touch Back remains independent of an inherited button hold.
    ActivityManager gesture(theme);
    gesture.mappedInput={.held=true};gesture.resetHomeStandbyInput();
    gesture.tick({.pressed=true,.released=true,.held=true});assert(gesture.standbyCalls==1);

    // A held-key barrier cannot prevent touch opening a book or control center.
    for(bool releaseWithTap:{false,true}) {
      ActivityManager book(theme);
      book.mappedInput={.held=true};book.resetHomeStandbyInput();
      book.currentActivity->action=[&book] {
        if(book.mappedInput.tap) {
          auto p=std::make_unique<Activity>();p->home=false;p->name="Reader";
          book.replaceActivity(std::move(p));
        }
      };
      book.tick({.released=releaseWithTap,.held=!releaseWithTap,.tap=true,.x=100,.y=200});
      assert(book.currentActivity->name=="Reader" && book.standbyCalls==0);
    }
    ActivityManager panel(theme);
    panel.mappedInput={.held=true};panel.resetHomeStandbyInput();
    panel.tick({.held=true,.tap=true,.x=100,.y=10});
    assert(panel.currentActivity->name=="FrontlightPanel" && panel.standbyCalls==0);
    panel.popActivity();panel.tick({.held=true});
    panel.tick({.released=true});assert(panel.standbyCalls==0);
    // INX status-bar tap moves focus into content, so another Back returns to tabs.
    if(panel.mainTabFocus==MainTabFocus::Content) panel.tick({.released=true});
    freshBack(panel);
  }

  ActivityManager focus("INX");
  focus.tick({.pressed=true,.held=true});
  focus.tick({.held=true,.otherRelease=Button::Confirm});
  assert(focus.mainTabFocus==MainTabFocus::Content);
  focus.tick({.released=true});
  assert(focus.mainTabFocus==MainTabFocus::Tabs && focus.standbyCalls==0);
  focus.tick({.released=true});assert(focus.standbyCalls==0);
  freshBack(focus);

  for(MainTab tab:MainTabs::values) {
    if(tab==MainTab::Recent) continue;
    ActivityManager tabs("INX");tabs.currentActivity->tab=tab;
    tabs.tick({.released=true});
    assert(tabs.currentActivity->tab==MainTab::Recent && tabs.standbyCalls==0);
    tabs.tick({.released=true});assert(tabs.standbyCalls==0);
    freshBack(tabs);
  }
  ActivityManager touchTab("INX");
  touchTab.mappedInput={.held=true};touchTab.resetHomeStandbyInput();
  touchTab.tick({.held=true,.tap=true,.x=144,.y=720});
  assert(touchTab.currentActivity->tab==MainTab::Library && touchTab.standbyCalls==0);
}
'''
        for defines in ((), ('CROSSMUX_UI_PROFILE_HIGH_DPI', 'FREEINK_DEVICE_READPICO=1')):
            run_cpp(program, include_dirs=(ROOT / 'src',), defines=defines)

    def test_inx_recent_render_and_flow_use_the_safe_content_clip(self):
        source = (ROOT / 'src/activities/home/InxRecentActivity.cpp').read_text()
        program = (r'''
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "InxRecentLayout.h"
#include "components/SubpageLayout.h"
#include "InxItemLayout.h"
#include "components/themes/inx/InxTheme.h"
#define tr(key) #key
constexpr int kGap=8, kPagePadding=18, kProgressHeight=6;
Rect screenSafe;
class GfxRenderer {
 public:
  mutable Rect clip{};
  mutable bool clipped=false;
  bool flow=false;
  int getScreenWidth() const { return screenSafe.x+screenSafe.width+5; }
  int getScreenHeight() const { return screenSafe.y+screenSafe.height+8; }
  void getOrientedViewableTRBL(int* t,int* r,int* b,int* l) const { *t=screenSafe.y; *r=5; *b=8; *l=screenSafe.x; }
  mutable int textCalls=0, metricCalls=0;
  struct ClipScope {
    const GfxRenderer& r;
    ClipScope(const GfxRenderer& r,int x,int y,int w,int h):r(r) {
      assert(!r.clipped); r.clip=Rect{x,y,w,h}; r.clipped=true;
    }
    ~ClipScope() { r.clipped=false; }
  };
  void clearScreen() const { assert(!clipped); }
  void displayBuffer() const { assert(!clipped); }
  int getLineHeight(int) const { return 18; }
  void fillRect(int,int,int w,int h,bool) const { assert(clipped && w>0 && h>0); }
  void drawLine(int x,int y,int right,int,bool) const {
    assert(clipped && x==clip.x && right==clip.x+clip.width-1 && y>=clip.y && y<clip.y+clip.height);
  }
  void drawText(int,int x,int y,const char*) const {
    assert(clipped && x>=clip.x && x<clip.x+clip.width && y>=clip.y && y<clip.y+clip.height);
    ++textCalls;
  }
};
struct RecentBook { std::string title; };
struct ReadingBookStats { unsigned totalReadingMs=0, lastSessionMs=0, sessions=0, chapterProgressPercent=0; };
namespace ReadingStatsAnalytics { std::string formatDurationHm(unsigned) { return "0m"; } }
unsigned char progressOf(const ReadingBookStats*) { return 50; }
void drawSparseInk(const GfxRenderer& r,Rect) { assert(r.clipped); }
void drawThickFrame(const GfxRenderer& r,Rect) { assert(r.clipped); }
void drawProgressBadge(const GfxRenderer& r,Rect,unsigned char) { assert(r.clipped); }
void drawDottedSeparator(const GfxRenderer& r,int,int,int) { assert(r.clipped); }
void drawBookText(const GfxRenderer& r,const RecentBook&,int x,int,int width,bool) {
  assert(r.clipped && x>=r.clip.x+kPagePadding && x+width<=r.clip.x+r.clip.width-kPagePadding);
  if (r.flow) assert(x==r.clip.x+kPagePadding && width==r.clip.width-2*kPagePadding);
}
void drawMiniProgress(const GfxRenderer& r,Rect rect,unsigned char) {
  assert(r.clipped && rect.x>=r.clip.x && rect.x+rect.width<=r.clip.x+r.clip.width && rect.width>0);
  assert(rect.y>=r.clip.y && rect.y+rect.height<=r.clip.y+r.clip.height);
  if (r.flow) assert(rect.x==r.clip.x+kPagePadding);
}
void drawMetric(const GfxRenderer& r,int x,int y,const char*,const char*,int width) {
  assert(r.clipped && (x==r.clip.x+kPagePadding || x==r.clip.x+kPagePadding+width+kGap));
  assert(y>=r.clip.y && y<r.clip.y+r.clip.height); ++r.metricCalls;
}
struct CrossPointSettings { enum HIDE_BATTERY_PERCENTAGE { HIDE_ALWAYS, SHOW }; };
struct { bool standbyShortcutEnabled=false; CrossPointSettings::HIDE_BATTERY_PERCENTAGE hideBatteryPercentage=CrossPointSettings::SHOW; } SETTINGS;
struct UITheme {
  Rect safe{};
  ThemeMetrics metrics=InxMetrics::values;
  int emptyCalls=0, batteryCalls=0;
  static UITheme& getInstance() { static UITheme instance; return instance; }
  UITheme& getTheme() { return *this; }
  const ThemeMetrics& getMetrics() const { return metrics; }
  Rect getScreenSafeArea(const GfxRenderer&,bool front,bool side) {
    assert(!front && !side); return safe;
  }
  static void drawCenteredWrappedText(const GfxRenderer& r,Rect bounds,int,const char*,int) {
    assert(r.clipped && bounds.x==r.clip.x && bounds.y==r.clip.y); ++getInstance().emptyCalls;
  }
  void drawMainTabStatusBar(const GfxRenderer&,Rect) { assert(false); }
  void drawButtonHints(const GfxRenderer& r,const char*,const char*,const char*,const char*) { assert(!r.clipped); }
  void drawBatteryRight(const GfxRenderer& r,Rect rect,bool) {
    assert(!r.clipped && rect.x+rect.width==safe.x+safe.width-12 && rect.y==safe.y+safe.height-(FREEINK_DEVICE_READPICO ? 24 : 30));
    assert(rect.y>=r.clip.y+r.clip.height);
    assert(rect.y+6+rect.height<=safe.y+safe.height); ++batteryCalls;
  }
};
#define GUI UITheme::getInstance().getTheme()
struct RenderLock {};
struct InxRecentActivity {
  GfxRenderer renderer;
  std::vector<RecentBook>* books=nullptr;
  int selected=1, coverCalls=0;
  InxRecentLayout chosen=InxRecentLayout::Flow;
  struct { bool hasTouch() const { return false; } } mappedInput;
  bool usesMainTabBar() const { return true; }
  bool mainTabsAtBottom() const { return false; }
  bool hasMainTabStatusBar() const { return false; }
  Rect pageContentRect() const {
    auto& theme=UITheme::getInstance();
    const int top=theme.metrics.topPadding+theme.metrics.headerHeight;
    return Rect{theme.safe.x,theme.safe.y+top,theme.safe.width,theme.safe.height-top};
  }
  Rect contentRect() const;
  const ReadingBookStats* statsAt(int) const { return nullptr; }
  bool showMainTabContentSelection() const { return true; }
  InxRecentLayout layout() const { return chosen; }
  void setThumbnailHeight(int height) { assert(height>0); }
  void drawBookCover(int,Rect) { assert(renderer.clipped); ++coverCalls; }
  void drawPageHeader(Rect rect,const char*) {
    assert(!renderer.clipped && rect.x==0 && rect.y==UITheme::getInstance().metrics.topPadding && rect.width==renderer.getScreenWidth());
  }
  struct Labels { const char *btn1="", *btn2="", *btn3="", *btn4=""; };
  Labels mainTabButtonLabels(const char*,const char*,bool,bool) { return {}; }
  bool prepareNextMissingCover() { return false; }
  void drawGrid(const Rect&);
  void drawList(const Rect&);
  void drawIcons(const Rect&);
  void drawCover(const Rect&);
  void drawFlow(const Rect&);
  void render(RenderLock&&);
};
''' + method(source, 'Rect InxRecentActivity::contentRect(') + method(source, 'Rect fitCoverRect(') + '\n'.join(
            method(source, 'void InxRecentActivity::draw'+layout+'(')
            for layout in ('Flow', 'Grid', 'List', 'Icons', 'Cover')) +
                method(source, 'void InxRecentActivity::render(') + r'''
int main() {
  auto& theme=UITheme::getInstance(); theme.metrics.buttonHintsHeight=0;
  for (const Rect safe : {Rect{5,5,674,1203},Rect{8,5,1203,674},Rect{5,8,674,1203},Rect{5,5,1203,674},Rect{0,0,480,800}}) {
    theme.safe=screenSafe=safe;
    InxRecentActivity page;
    std::vector<RecentBook> books{{"中文长书名测试"},{"另一本书"},{"More books"}};
    page.books=&books;
    for (auto layout : {InxRecentLayout::Flow,InxRecentLayout::Grid,InxRecentLayout::List,
                        InxRecentLayout::Icons,InxRecentLayout::Cover}) {
      page.chosen=layout; page.renderer.flow=layout==InxRecentLayout::Flow;
      page.render(RenderLock{}); assert(!page.renderer.clipped);
    }
    assert(page.coverCalls==13 && page.renderer.metricCalls==4 && page.renderer.textCalls==1);
    books.clear(); page.render(RenderLock{});
    page.books=nullptr; page.render(RenderLock{});
  }
  assert(theme.emptyCalls==10 && theme.batteryCalls==35);
}
''')
        for readpico in (0, 1):
            with self.subTest(readpico=readpico):
                run_cpp(f'#define FREEINK_DEVICE_READPICO {readpico}\n' + program,
                        include_dirs=(ROOT / 'src', ROOT / 'lib/hal'))

    def test_readpico_safe_area_in_all_orientations(self):
        board = (ROOT / 'freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h').read_text()
        profile = method(board, 'constexpr BoardProfile READ_PICO =')
        match = re.search(r'\{(\d+), (\d+), (\d+), (\d+)\},\s*// portrait TRBL', profile)
        self.assertIsNotNone(match)
        insets = tuple(map(int, match.groups()))
        self.assertEqual(insets, (5, 5, 8, 5))
        renderer = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        theme = (ROOT / 'src/components/UITheme.cpp').read_text()
        run_cpp(r'''
#include <cassert>
#include <initializer_list>
#define FREEINK_DEVICE_READPICO 1
namespace BoardConfig {
struct Insets { int top, right, bottom, left; };
struct Profile { Insets viewableInsets; };
constexpr Profile ACTIVE{{INSETS}};
}
struct GfxRenderer {
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  Orientation orientation=Portrait;
  static constexpr int VIEWABLE_MARGIN_TOP=9, VIEWABLE_MARGIN_RIGHT=3,
                       VIEWABLE_MARGIN_BOTTOM=3, VIEWABLE_MARGIN_LEFT=3;
  Orientation getOrientation() const { return orientation; }
  bool portrait() const { return orientation==Portrait || orientation==PortraitInverted; }
  int getScreenWidth() const { return portrait() ? 684 : 1216; }
  int getScreenHeight() const { return portrait() ? 1216 : 684; }
  void getOrientedViewableTRBL(int*,int*,int*,int*) const;
};
struct Rect { int x,y,width,height; };
struct ThemeMetrics { int buttonHintsHeight=0; };
struct UITheme {
  ThemeMetrics getMetrics() const { return {}; }
  Rect getScreenSafeArea(const GfxRenderer&,bool,bool);
};
'''.replace('INSETS', ','.join(map(str, insets))) +
                method(renderer, 'void GfxRenderer::getOrientedViewableTRBL(') +
                method(theme, 'Rect UITheme::getScreenSafeArea(').replace(
                    'bool hasSideButtonHints', '[[maybe_unused]] bool hasSideButtonHints') + r'''
int main() {
  GfxRenderer renderer;
  UITheme theme;
  for (auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                           GfxRenderer::PortraitInverted, GfxRenderer::LandscapeCounterClockwise}) {
    renderer.orientation=orientation;
    int top=0, right=0, bottom=0, left=0;
    renderer.getOrientedViewableTRBL(&top,&right,&bottom,&left);
    const BoardConfig::Insets expected[]={{5,5,8,5},{5,5,5,8},{8,5,5,5},{5,8,5,5}};
    const auto inset=expected[orientation];
    assert(top==inset.top && right==inset.right && bottom==inset.bottom && left==inset.left);
    const Rect safe=theme.getScreenSafeArea(renderer,false,false);
    assert(safe.x==inset.left && safe.y==inset.top);
    assert(safe.width==renderer.getScreenWidth()-inset.left-inset.right && safe.height==renderer.getScreenHeight()-inset.top-inset.bottom);
    const Rect hiddenHints=theme.getScreenSafeArea(renderer,true,false);
    assert(hiddenHints.x==safe.x && hiddenHints.y==safe.y);
    assert(hiddenHints.width==safe.width && hiddenHints.height==safe.height);
  }
}
''')

    def test_header_subtitle_is_inside_clip(self):
        source = (ROOT / 'src/components/themes/inx/InxTheme.cpp').read_text()
        code = method(source, 'void InxTheme::drawHeader(')
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <initializer_list>
struct Rect { int x, y, width, height; };
constexpr int SMALL_FONT_ID = 1, STATUS_NUMERIC_FONT_ID = 1, NOTOSERIF_12_FONT_ID = 2, UI_12_FONT_ID = 3, kIconGap = 8, kRowPadding = 20;
namespace EpdFontFamily { enum Style { REGULAR, BOLD }; }
struct CrossPointSettings { enum class HIDE_BATTERY_PERCENTAGE { HIDE_ALWAYS }; };
struct { CrossPointSettings::HIDE_BATTERY_PERCENTAGE hideBatteryPercentage{}; } SETTINGS;
namespace InxMetrics { struct { int batteryWidth=20, batteryHeight=10, batteryBarHeight=24, contentSidePadding=20; } values; }
struct GfxRenderer {
  mutable Rect clip{};
  mutable bool clipped=false;
  mutable int subtitles=0;
  struct ClipScope {
    const GfxRenderer& r;
    ClipScope(const GfxRenderer& r, int x, int y, int w, int h):r(r) { r.clip={x,y,w,h}; r.clipped=true; }
    ~ClipScope() { r.clipped=false; }
  };
  int getLineHeight(int font) const { return font == SMALL_FONT_ID ? 18 : 36; }
  int getTextWidth(int, const char* text) const { return static_cast<int>(strlen(text))*6; }
  void fillRect(int,int,int,int,bool) const {}
  void drawLine(int,int,int,int,bool) const {}
  void drawText(int font, int x, int y, const char* text, bool=true, EpdFontFamily::Style=EpdFontFamily::REGULAR) const {
    if (font != SMALL_FONT_ID) return;
    ++subtitles;
    assert(clipped && x >= clip.x && y >= clip.y);
    assert(x + getTextWidth(font,text) <= clip.x + clip.width);
    assert(y + getLineHeight(font) <= clip.y + clip.height);
  }
};
struct InxTheme {
  void drawBatteryRight(const GfxRenderer&, Rect, bool, int = 1) const {}
  void drawHeader(const GfxRenderer&, Rect, const char*, const char*, bool = true) const;
};
''' + code + r'''
int main() {
  GfxRenderer r;
  for (int width : {480, 800}) for (int y : {0, 12}) {
    for (const char* subtitle : {"更多详情", "More Details", "2026-09-16"})
      InxTheme{}.drawHeader(r, {0,y,width,66}, "Stats", subtitle);
  }
  assert(r.subtitles == 12);
  InxTheme{}.drawHeader(r, {0,0,480,66}, "Stats", nullptr);
  assert(r.subtitles == 12);
}
''')

    def test_sync_refresh_survives_wifi_child(self):
        source = (WEREAD / 'WeReadProgressSyncActivity.cpp').read_text()
        enter = method(source, 'void WeReadProgressSyncActivity::onEnter(')
        callback = method(source, 'void WeReadProgressSyncActivity::onWifiSelectionComplete(')
        render = method(source, 'void WeReadProgressSyncActivity::render(')
        display = render[render.rindex('  renderer.displayBuffer('):render.rindex('}')]
        run_cpp(r'''
#include <atomic>
#include <cassert>
#include <initializer_list>
namespace HalDisplay { enum RefreshMode { FULL_REFRESH, FAST_REFRESH }; }
struct Renderer {
  HalDisplay::RefreshMode last = HalDisplay::FAST_REFRESH;
  void displayBuffer(HalDisplay::RefreshMode mode=HalDisplay::FAST_REFRESH) { last=mode; }
};
struct Activity { void onEnter() {} };
namespace ReaderUtils { void applyOrientation(Renderer&, int) {} }
struct { int orientation=0; } SETTINGS;
bool loggedIn=true;
namespace WeReadStore {
struct Session { bool valid() { return true; } void clear() {} };
bool loadSession(Session&) { return loggedIn; }
}
namespace NetworkStartup { void prepare(Renderer&) {} }
constexpr int WL_CONNECTED=1;
struct { int connected=1; int status() { return connected; } } WiFi;
struct WeReadProgressSyncActivity : Activity {
  enum class State { WifiSelection, Starting, LoginRequired };
  State state_ = State::WifiSelection;
  std::atomic<bool> fullRefreshPending_{true};
  Renderer renderer;
  bool wifiActivated_=false, returned=false, child=false;
  void requestUpdate() {}
  void launchWifiSelection() { child=true; }
  void returnToReader() { returned=true; }
  void onEnter();
  void onWifiSelectionComplete(bool);
  void renderRefresh() { DISPLAY }
};
'''.replace('DISPLAY', display) + enter + callback + r'''
int main() {
  for (bool login : {false,true}) for (int connected : {0,1}) {
    loggedIn=login; WiFi.connected=connected;
    WeReadProgressSyncActivity page;
    page.onEnter();
    if (page.child) {
      page.renderer.displayBuffer(); // A child paint cannot consume the parent's flag.
      WiFi.connected=1;
      page.onWifiSelectionComplete(true);
    }
    page.renderRefresh(); assert(page.renderer.last==HalDisplay::FULL_REFRESH);
    page.renderRefresh(); assert(page.renderer.last==HalDisplay::FAST_REFRESH);
    WiFi.connected=1;
    page.onWifiSelectionComplete(true);
    page.renderRefresh(); assert(page.renderer.last==HalDisplay::FULL_REFRESH);
    page.renderRefresh(); assert(page.renderer.last==HalDisplay::FAST_REFRESH);
    page.onWifiSelectionComplete(false); assert(page.returned);
  }
}
''')

    def test_resource_error_messages_and_layout(self):
        source = (WEREAD / 'WeReadActivity.cpp').read_text()
        message = method(source, 'const char* WeReadActivity::errorMessage(')
        render = method(source, 'void WeReadActivity::render(')
        error = render.split('    case State::Error: {', 1)[1].split('    case State::LogoutError:', 1)[0]
        keys = ('STR_WEREAD_STORAGE_ERROR', 'STR_WEREAD_CHECK_STORAGE_SPACE', 'STR_WEREAD_CHECK_SD_CARD',
                'STR_WEREAD_MEMORY_ERROR', 'STR_WEREAD_RESTART_HINT', 'STR_WEREAD_HTTP_ERROR',
                'STR_WEREAD_NO_WIFI', 'STR_WEREAD_CACHE_NOT_AVAILABLE', 'STR_WEREAD_CACHE_WHOLE_BOOK_ONLY')
        for language in ('chinese', 'english'):
            spec = importlib.util.spec_from_file_location('gen_i18n', ROOT / 'scripts/gen_i18n.py')
            generator = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(generator)
            translations = generator.parse_yaml_file(str(ROOT / f'lib/I18n/translations/{language}.yaml'))
            strings = '\n'.join(f'const char* {key} = {json.dumps(translations[key], ensure_ascii=False)};' for key in keys)
            run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <initializer_list>
''' + strings + r'''
#define tr(key) key
namespace WeReadClient { enum class Error { SdCard, OutOfMemory, Network, Unavailable, WholeBookOnly, Protocol }; }
namespace EpdFontFamily { enum Style { BOLD, REGULAR }; }
constexpr int UI_10_FONT_ID=10, WL_CONNECTED=1;
struct { int connected=1; int status() const { return connected; } } WiFi;
struct Rect { int x,y,width,height; };
struct Metrics { int contentSidePadding=20; };
struct Renderer {
  struct ClipScope { ClipScope(const Renderer&,int,int,int,int) {} };
  int getLineHeight(int) const { return 26; }
  int getTextWidth(const char* text) const {
    int width=0;
    for (; *text; ++text) {
      unsigned char c=*text;
      if (c < 128) width+=12;
      else if ((c & 0xc0) != 0x80) width+=26;
    }
    return width;
  }
};
using GfxRenderer=Renderer;
std::vector<std::string> shown;
struct { void drawPopup(const Renderer&, const char* s) { shown.emplace_back(s); } } GUI;
namespace SubpageLayout {
int sectionGap(const Metrics&) { return 12; }
int centeredTop(Rect r,int h) { return r.y + std::max(0,(r.height-h)/2); }
Rect insetHorizontal(Rect r,int n) { return {r.x+n,r.y,r.width-2*n,r.height}; }
}
namespace UITheme {
void drawCenteredText(const Renderer& r,Rect rect,int,int y,const char* text,bool,EpdFontFamily::Style) {
  assert(r.getTextWidth(text)<=rect.width);
  assert(y>=rect.y && y+26<=rect.y+rect.height);
  shown.emplace_back(text);
}
}
struct WeReadActivity {
  WeReadClient::Error error_;
  Renderer renderer;
  Metrics metrics;
  Rect content;
  enum class State { Error };
  const char* errorMessage() const;
  void renderError() { switch (State::Error) { case State::Error: { ERROR } }
};
'''.replace('ERROR', error) + message + r'''
int main() {
  using E=WeReadClient::Error;
  for (Rect bounds : {Rect{0,66,480,694},Rect{0,66,800,374}}) {
    WeReadActivity page{E::SdCard,{}, {},bounds};
    shown.clear(); page.renderError();
    assert(shown==std::vector<std::string>({STR_WEREAD_STORAGE_ERROR,STR_WEREAD_CHECK_STORAGE_SPACE,STR_WEREAD_CHECK_SD_CARD}));
    page.error_=E::OutOfMemory; shown.clear(); page.renderError();
    assert(shown==std::vector<std::string>({STR_WEREAD_MEMORY_ERROR,STR_WEREAD_RESTART_HINT}));
    page.error_=E::Network; shown.clear(); page.renderError();
    assert(shown==std::vector<std::string>({STR_WEREAD_HTTP_ERROR}));
    WiFi.connected=0; shown.clear(); page.renderError();
    assert(shown==std::vector<std::string>({STR_WEREAD_NO_WIFI})); WiFi.connected=1;
  }
}
''')


    def test_cover_download_creates_directory_before_request(self):
        source = (ROOT / 'lib/WeReadWebApi/src/WeReadClient.cpp').read_text()
        download = method(source, 'Error Operation::fetchCoverSource(')
        sink = source[source.index('struct FileSink {'):source.index('bool finishFile(')]
        run_cpp(r"""
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
namespace fs = std::filesystem;
std::string lastLog;
void logError(const char*, const char* format, ...) {
  char buf[512]; va_list args; va_start(args,format);
  vsnprintf(buf,sizeof(buf),format,args); va_end(args); lastLog=buf;
}
#define LOG_ERR(...) logError(__VA_ARGS__)
bool shortWrite=false;
struct HalFile {
  std::ofstream stream;
  bool isOpen() const { return stream.is_open(); }
  void close() { stream.close(); }
  size_t write(const void* data,size_t size) {
    if (shortWrite) return 0;
    stream.write(static_cast<const char*>(data),size);
    return stream.good() ? size : 0;
  }
};
struct {
  int directoryChecks=0;
  bool exists(const char* p) { return fs::exists(p); }
  bool remove(const char* p) { return fs::remove(p); }
  bool ensureDirectoryExists(const char* p) {
    ++directoryChecks;
    std::error_code ec;
    fs::create_directories(p,ec);
    return !ec && fs::is_directory(p);
  }
  bool openFileForWrite(const char*,const std::string& path,HalFile& file) {
    file.stream.open(path,std::ios::binary|std::ios::trunc);
    return file.isOpen();
  }
} Storage;
namespace WeReadProtocol { enum class ImageType { None,Jpeg,Png,Detect }; }
namespace WeReadStore {
  bool rootReady=true;
  bool ensureRoot() { return rootReady; }
  struct ImageRecord { char href[64]{},url[512]{}; };
  enum class ImageWorkState { Pending,Skipped,Complete };
}
namespace WeReadHttpClient {
  bool extractHttpsHost(const char* url,char*,size_t) { return strncmp(url,"https://",8)==0; }
}
const char* coverSourceName(WeReadProtocol::ImageType type) {
  return type==WeReadProtocol::ImageType::Png ? "cover.png" : "cover.jpg";
}
""" + sink + r"""
enum class Error { Ok,SdCard,Protocol };
enum class CoverWorkResult { Skipped,Pending,Complete };
struct Operation {
  WeReadProtocol::ImageType coverType_=WeReadProtocol::ImageType::Jpeg;
  WeReadStore::ImageWorkState coverState_=WeReadStore::ImageWorkState::Pending;
  char url_[512]="https://cdn.weread.qq.com/cover.jpg",imageHost_[128]{};
  std::string bookDir_;
  uint8_t coverAttempts_=0,coverRedirects_=0;
  int requests=0;
  Error fetchCoverSource(CoverWorkResult&);
  Error requestImage(WeReadStore::ImageRecord& image,WeReadStore::ImageWorkState& state,
                     uint8_t&,uint8_t&,bool,WeReadProtocol::ImageType* detected) {
    ++requests;
    assert(fs::is_directory(bookDir_)); // Original code fails here for a shelf-only book.
    const std::string path=bookDir_+"/"+image.href+".part";
    FileSink sink; sink.path=&path;
    const uint8_t data[]={0xff,0xd8,0xff};
    if (!resetFile(&sink) || !writeFile(&sink,data,sizeof(data))) return Error::SdCard;
    state=WeReadStore::ImageWorkState::Complete;
    if (detected) *detected=WeReadProtocol::ImageType::Jpeg;
    return Error::Ok;
  }
};
""" + download + r"""
int main(int argc,char** argv) {
  assert(argc==1);
  const fs::path root=fs::path(argv[0]).parent_path()/"sd";
  Operation op; op.bookDir_=(root/"weread"/"new-book").string();
  CoverWorkResult result;
  assert(!fs::exists(op.bookDir_));
  assert(op.fetchCoverSource(result)==Error::Ok && result==CoverWorkResult::Complete);
  assert(fs::file_size(fs::path(op.bookDir_)/"cover.jpg.part")==3);
  assert(op.fetchCoverSource(result)==Error::Ok && op.requests==2);
  WeReadStore::rootReady=false;
  assert(op.fetchCoverSource(result)==Error::SdCard && op.requests==2);
  assert(lastLog.find(op.bookDir_)!=std::string::npos);
  WeReadStore::rootReady=true;
  op.bookDir_=(root/"not-a-directory").string(); std::ofstream(op.bookDir_) << "keep";
  assert(op.fetchCoverSource(result)==Error::SdCard && op.requests==2);
  assert(fs::file_size(op.bookDir_)==4);
  const int checks=Storage.directoryChecks;
  for (const char* url : {"", "http://invalid/cover.jpg"}) {
    strcpy(op.url_,url);
    assert(op.fetchCoverSource(result)==Error::Ok && result==CoverWorkResult::Skipped);
  }
  assert(Storage.directoryChecks==checks && op.requests==2);
  std::string missing=(root/"missing"/"cover.part").string(); FileSink failed; failed.path=&missing;
  assert(!resetFile(&failed) && lastLog.find(missing)!=std::string::npos);
  std::string good=(root/"short.part").string(); FileSink shortSink; shortSink.path=&good;
  assert(resetFile(&shortSink)); shortWrite=true;
  const uint8_t bytes[]={1,2,3};
  assert(!writeFile(&shortSink,bytes,3));
  assert(shortSink.failure==FileSink::Failure::SdCard && shortSink.size==0);
  assert(lastLog.find("written=0 expected=3")!=std::string::npos);
}
""")


    def test_txt_spacing_keeps_cache_fields_byte_aligned(self):
        import re
        # Immutable legacy schema fixture; active TXT books now use EPUB sections.
        source = subprocess.check_output(['git', '-C', str(ROOT), 'show',
            '593c8dbc8feb404d740108a3db281c4eeae3e33e:src/activities/reader/TxtReaderActivity.cpp'], text=True)
        read = method(source, 'bool readPodChecked(')
        write = method(source, 'bool writePodChecked(')
        expression = re.search(
            r'!writePodChecked\(f, (.*SETTINGS\.extraParagraphSpacing.*?)\)\s*\|\|\s*!writePodChecked\(f, complete\)',
            source).group(1)
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
struct HalFile {
  std::vector<uint8_t> bytes;
  size_t position=0;
  size_t write(const uint8_t* data,size_t size) {bytes.insert(bytes.end(),data,data+size);return size;}
  int read(uint8_t* data,size_t size) {
    if(position+size>bytes.size()) return 0;
    std::memcpy(data,bytes.data()+position,size);position+=size;return static_cast<int>(size);
  }
};
struct {uint8_t extraParagraphSpacing=0;} SETTINGS;
''' + 'template<typename T>\n' + read + '\ntemplate<typename T>\n' + write + r'''
int main() {
  for(uint8_t level=0;level<=5;++level) {
    SETTINGS.extraParagraphSpacing=level;
    HalFile f;
    assert(writePodChecked(f, ''' + expression + r'''));
    const uint8_t complete=1, encoding=1;
    const uint32_t pageCount=7;
    assert(writePodChecked(f,complete));assert(writePodChecked(f,encoding));assert(writePodChecked(f,pageCount));
    assert(f.bytes.size()==7);
    HalFile reopened=f;
    uint8_t spacing=255,loadedComplete=0,loadedEncoding=0;uint32_t loadedPages=0;
    assert(readPodChecked(reopened,spacing));assert(readPodChecked(reopened,loadedComplete));
    assert(readPodChecked(reopened,loadedEncoding));assert(readPodChecked(reopened,loadedPages));
    assert(spacing==(level!=0) && loadedComplete==1 && loadedEncoding==1 && loadedPages==7);
    assert(reopened.position==reopened.bytes.size());
  }
}
''')


    def test_inx_corner_status_and_touch_home_footer(self):
        theme = (ROOT / 'src/components/themes/inx/InxTheme.cpp').read_text()
        home = (ROOT / 'src/activities/home/InxRecentActivity.cpp').read_text()
        status = method(theme, 'void InxTheme::drawMainTabStatusBar(')
        render = method(home, 'void InxRecentActivity::render(')
        footer = render[render.index('  if (usesMainTabBar()'):render.index('  if (prepareNextMissingCover()')]
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <initializer_list>
struct Rect { int x, y, width, height; };
constexpr int STATUS_NUMERIC_FONT_ID = 1, SMALL_FONT_ID = 1;
struct CrossPointSettings {
  enum { INX_TAB_TOP, INX_TAB_BOTTOM };
  enum class HIDE_BATTERY_PERCENTAGE { SHOW, HIDE_ALWAYS };
};
struct {
  int inxTabPosition = CrossPointSettings::INX_TAB_BOTTOM, clockFormat = 0;
  CrossPointSettings::HIDE_BATTERY_PERCENTAGE hideBatteryPercentage{};
} SETTINGS;
struct Metrics { int batteryWidth=16, batteryHeight=12, topPadding=0; };
struct UITheme {
  static UITheme& getInstance() { static UITheme ui; return ui; }
  const Metrics& getMetrics() const { static Metrics m; return m; }
};
namespace TimeUtils {
bool valid=true, formatted12=false;
int calls=0;
bool formatCurrentTime(char* out, size_t size, bool hour12) {
  assert(size == 9); ++calls; formatted12=hour12;
  if (!valid) return false;
  std::strcpy(out, hour12 ? "12:59 PM" : "23:59"); return true;
}
}
struct GfxRenderer {
  int width=480, height=800, top=9, right=3, bottom=3, left=3;
  mutable int clocks=0, clockX=0, clockY=0;
  mutable char clock[9]{};
  mutable Rect clip{};
  mutable bool clipped=false;
  struct ClipScope {
    const GfxRenderer& r;
    ClipScope(const GfxRenderer& r, int x, int y, int w, int h):r(r) {
      r.clip={x,y,w,h}; r.clipped=true;
    }
    ~ClipScope() { r.clipped=false; }
  };
  int getLineHeight(int) const { return 18; }
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  void getOrientedViewableTRBL(int* t,int* r,int* b,int* l) const { *t=top; *r=right; *b=bottom; *l=left; }
  void drawText(int font, int x, int y, const char* text) const {
    assert(font == STATUS_NUMERIC_FONT_ID && clipped);
    ++clocks; clockX=x; clockY=y; std::strcpy(clock,text);
  }
};
struct InxTheme {
  mutable Rect battery{};
  mutable bool percentage=false;
  void drawBatteryRight(const GfxRenderer& r, Rect rect, bool show, int = 1) const {
    assert(r.clipped); battery=rect; percentage=show;
  }
  void drawMainTabStatusBar(const GfxRenderer&, Rect) const;
};
''' + status + r'''
namespace InxRecentGeometry {
constexpr int footerReservedHeight=40;
Rect batteryRect(Rect safe) { return {safe.x+safe.width-27,safe.y+safe.height-30,15,12}; }
}
struct Input { bool touch=true; bool hasTouch() const { return touch; } };
struct Gui {
  int statusCalls=0, legacyCalls=0;
  Rect footer{};
  InxTheme theme;
  void drawMainTabStatusBar(const GfxRenderer& r, Rect rect) {
    ++statusCalls; footer=rect; theme.drawMainTabStatusBar(r,rect);
  }
  void drawBatteryRight(const GfxRenderer&, Rect, bool) { ++legacyCalls; }
} GUI;
struct InxRecentActivity {
  GfxRenderer renderer;
  Input mappedInput;
  bool inx=true;
  bool usesMainTabBar() const { return inx; }
  bool mainTabsAtBottom() const { return SETTINGS.inxTabPosition == CrossPointSettings::INX_TAB_BOTTOM; }
  bool hasMainTabStatusBar() const { return inx && mappedInput.touch && mainTabsAtBottom(); }
  void drawFooter() {
    const int width=renderer.getScreenWidth();
    const auto& metrics=UITheme::getInstance().getMetrics();
    const Rect safeArea{renderer.left,renderer.top,width-renderer.left-renderer.right,renderer.height-renderer.top-renderer.bottom};
''' + footer + r'''
  }
};
int main() {
  for (bool landscape : {false,true}) for (bool largeInsets : {false,true})
    for (bool hour12 : {false,true}) for (bool valid : {false,true}) for (bool hide : {false,true}) {
      GfxRenderer r;
      if (landscape) { r.width=800; r.height=480; }
      if (largeInsets) { r.top=20; r.left=18; r.right=21; r.bottom=18; }
      SETTINGS.clockFormat=hour12;
      SETTINGS.hideBatteryPercentage=hide ? CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS
                                         : CrossPointSettings::HIDE_BATTERY_PERCENTAGE::SHOW;
      TimeUtils::valid=valid;
      for (bool bottom : {false,true}) {
        SETTINGS.inxTabPosition=bottom ? CrossPointSettings::INX_TAB_BOTTOM : CrossPointSettings::INX_TAB_TOP;
        const Rect rect=bottom ? Rect{r.left,r.top,r.width-r.left-r.right,28}
                               : Rect{r.left,r.height-40,r.width-r.left-r.right,40-r.bottom};
        InxTheme theme;
        const int clocks=r.clocks, formats=TimeUtils::calls;
        theme.drawMainTabStatusBar(r,rect);
        assert(!r.clipped && theme.percentage == !hide);
        assert(r.clocks-clocks == bottom && TimeUtils::calls-formats == bottom);
        assert(r.width-theme.battery.x-theme.battery.width == std::max(12,r.right+1));
        assert(theme.battery.x+theme.battery.width < rect.x+rect.width);
        const int iconY=theme.battery.y+6;
        if (bottom) assert(iconY == std::max(12,r.top));
        else {
          assert(r.height-iconY-theme.battery.height == std::max(12,r.bottom+1));
          assert(theme.battery.y+6+theme.battery.height < rect.y+rect.height);
        }
        assert(iconY >= rect.y && iconY+theme.battery.height <= rect.y+rect.height);
        if (bottom) {
          assert(r.clockX == std::max(12,r.left) && theme.battery.y == r.clockY);
          assert(r.clockX+8*8+6 < theme.battery.x-4*8);
          assert(TimeUtils::formatted12 == hour12);
          assert(std::strcmp(r.clock, !valid ? "--:--" : hour12 ? "12:59 PM" : "23:59") == 0);
        }
      }
      const int clocks=r.clocks;
      InxTheme{}.drawMainTabStatusBar(r,{0,0,0,28});
      InxTheme{}.drawMainTabStatusBar(r,{0,0,480,0});
      assert(r.clocks == clocks);
    }
  for (bool inx : {false,true}) for (bool touch : {false,true}) for (bool bottom : {false,true}) {
    GUI.statusCalls=GUI.legacyCalls=0;
    SETTINGS.inxTabPosition=bottom ? CrossPointSettings::INX_TAB_BOTTOM : CrossPointSettings::INX_TAB_TOP;
    InxRecentActivity home; home.inx=inx; home.mappedInput.touch=touch;
    const int formats=TimeUtils::calls;
    home.drawFooter();
    assert(home.renderer.clocks == 0 && TimeUtils::calls == formats);
    const bool footer=inx && touch && !bottom;
    assert(GUI.statusCalls == footer);
    assert(GUI.legacyCalls == (!footer && !home.hasMainTabStatusBar()));
    if (footer) {
      assert(GUI.footer.y == 760 && GUI.footer.height == 37);
      assert(GUI.theme.battery.y == 770 && GUI.theme.battery.x == 452);
    }
  }
}
''')


if __name__ == '__main__':
    unittest.main()
