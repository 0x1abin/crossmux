"""Run production confirmation, Home Back and catalog row logic against small host seams."""
from pathlib import Path
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class UiSurfaceSyncTest(unittest.TestCase):
    def test_confirmation_old_placement_and_new_custom_labels(self):
        header = (ROOT / "src/activities/util/ConfirmationActivity.h").read_text()
        declaration = header[header.index("class ConfirmationActivity"):]
        source = (ROOT / "src/activities/util/ConfirmationActivity.cpp").read_text()
        implementation = source[source.index("ConfirmationActivity::ConfirmationActivity"):source.index("void ConfirmationActivity::render(")]
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
constexpr int UI_12_FONT_ID=12, UI_10_FONT_ID=10, STR_CANCEL=0, STR_CONFIRM=1;
const char* tr(int key) {return key == STR_CANCEL ? "Cancel" : "Confirm";}
namespace EpdFontFamily {enum Style {REGULAR, BOLD};}
struct GfxRenderer {
 int getScreenWidth() const {return 480;}
 std::string truncatedText(int,const char* text,int,EpdFontFamily::Style) {return text;}
 std::vector<std::string> wrappedText(int,const char* text,int,int,EpdFontFamily::Style) {return {text};}
};
struct MappedInputManager {}; struct RenderLock {}; struct ActivityResult {bool isCancelled=false;};
struct Activity {
 GfxRenderer& renderer; MappedInputManager& mappedInput;
 Activity(const char*,GfxRenderer& r,MappedInputManager& i):renderer(r),mappedInput(i) {}
 virtual ~Activity()=default; virtual void onEnter() {} virtual void loop() {} virtual void render(RenderLock&&) {}
 void requestUpdate(bool) {} void setResult(ActivityResult&&) {} static void finish() {}
};
struct Theme {
 bool mainTabs=false; bool hasMainTabs() const {return mainTabs;}
 static Theme& getInstance() {static Theme value;return value;}
};
using UITheme=Theme;
struct OptionPopup {
 inline static std::vector<std::string> labels;
 inline static std::string title, headline;
 template<class Handler> void show(const char* t,const char* const* options,int count,int,Handler) {
   title=t;headline.clear();labels.assign(options,options+count);
 }
 template<class Handler> void show(const char* t,const char* h,const char* const* options,int count,int i,Handler handler) {
   show(t,options,count,i,handler);headline=h;
 }
};
''' + declaration + implementation + r'''
void ConfirmationActivity::loop() {}
void ConfirmationActivity::render(RenderLock&&) {}
int main() {
 GfxRenderer renderer; MappedInputManager input;
 for(bool inx : {false,true}) {
   Theme::getInstance().mainTabs=inx;
   ConfirmationActivity old(renderer,input,"Delete?","book"); old.onEnter();
   assert((OptionPopup::labels==std::vector<std::string>{"Cancel","Confirm"}));
   assert(OptionPopup::title=="Delete?");
   ConfirmationActivity labels(renderer,input,"Notice","body","Go back","Continue"); labels.onEnter();
   assert((OptionPopup::labels==std::vector<std::string>{"Go back","Continue"}));
   ConfirmationActivity fallback(renderer,input,"Notice","body","",""); fallback.onEnter();
   assert((OptionPopup::labels==std::vector<std::string>{"Cancel","Confirm"}));
   ConfirmationActivity placed(renderer,input,"Delete?","book",ConfirmationActivity::BodyPlacement::PopupTitle);
   placed.onEnter();assert(OptionPopup::title==(inx ? "book" : "Delete?"));
 }
}
''')

    def test_home_back_keeps_standby_owner_and_consumes_one_release(self):
        source = (ROOT / "src/activities/home/HomeActivity.cpp").read_text()
        loop = method(source, "void HomeActivity::loop()")
        start = loop.index("  // The standby shortcut")
        end = loop.index("  if (coverGridUi)", start)
        branch = loop[start:end]
        render = method(source, "void HomeActivity::render(")
        hint = render[render.index("  const char* backLabel"):render.index("  const Rect content")]
        run_cpp(r'''
#include <cassert>
#include <string>
#include <vector>
struct {bool standbyShortcutEnabled=false;} SETTINGS;
constexpr int STR_RESUME=0,STR_STANDBY_TITLE=1;
const char* tr(int id) {return id==STR_RESUME ? "Resume" : "Sleep";}
struct MappedInputManager {
 enum class Button {Back}; bool released=false;
 bool wasReleased(Button) const {return released;}
};
struct Home {
 bool hasContinueReading=true; struct Book {std::string path;}; std::vector<Book> recentBooks{{"last"}};
 MappedInputManager mappedInput; int opened=0,otherActions=0;
 void onRecentBookSelected(const std::string& path) {assert(path=="last");++opened;}
 void step();
};
''' +
                "void Home::step() {\n" + branch + "++otherActions;\n}\n" + "const char* hint(bool hasContinueReading) {\n" + hint + "return backLabel;\n}\n" + r'''
int main() {
 Home home;
 assert(std::string(hint(true))=="Resume" && std::string(hint(false)).empty());
 SETTINGS.standbyShortcutEnabled=true;
 assert(std::string(hint(true))=="Sleep" && std::string(hint(false))=="Sleep");
 SETTINGS.standbyShortcutEnabled=false;
 home.step();home.step();assert(home.opened==0);
 home.mappedInput.released=true;home.step();assert(home.opened==1 && home.otherActions==2);
 home.mappedInput.released=false;home.step();assert(home.opened==1);
 SETTINGS.standbyShortcutEnabled=true;home.mappedInput.released=true;home.step();assert(home.opened==1);
 SETTINGS.standbyShortcutEnabled=false;home.recentBooks.clear();home.step();assert(home.opened==1);
 home.hasContinueReading=false;home.step();assert(home.opened==1);
}
''')

    def test_dialog_upstream_center_and_inx_safe_center(self):
        source = (ROOT / "src/components/OptionPopup.h").read_text()
        geometry = source[source.index("    const fui::Rect screen = device.screen();"):source.index("    // Chrome guard first")]
        run_cpp(r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
namespace fui {
struct Rect {int16_t x,y,width,height;}; struct Size {int16_t width,height;};
int16_t clampI16(int v,int lo=-32768,int hi=32767) {return std::clamp(v,lo,hi);}
Rect centeredRect(Rect r,Size s) {return {int16_t(r.x+(r.width-s.width)/2),int16_t(r.y+(r.height-s.height)/2),s.width,s.height};}
struct DialogOption {const char* label;int action,value,state;};
struct Props {int gap=10,buttonHeight=30;uint8_t optionCount=0;};
int optionDialogHeight(int,const Props& p,int) {return 20+30*p.optionCount;}
constexpr int StateFocused=1,StateNormal=0;
}
struct Theme {bool inx=false;bool hasMainTabs() const {return inx;}static Theme& getInstance(){static Theme t;return t;}};
using UITheme=Theme;
struct Device {fui::Rect all,safe;fui::Rect screen() const{return all;}fui::Rect safeRect()const{return safe;}};
fui::Rect layout(Device device,bool upstreamStyle,bool inx) {
 Theme::getInstance().inx=inx;
 struct {int optionPopupDialogSideMargin=10;} metrics;
 fui::Props props;int target=0,totalOptions=2,selectedIndex=0,visibleOptionRows=0;
 constexpr int MAX_OPTIONS=16,ACTION_OPTION=1;
 std::vector<std::string> ownedStrings{"Cancel","Confirm"};fui::DialogOption options[MAX_OPTIONS];
 auto windowStart=[](int){return 0;};
""" + geometry + r"""
 assert(visibleOptionRows>=1 && options[0].label!=nullptr);
 return dialogRect;
}
int main() {
 Device d{{0,0,480,800},{0,9,480,788}};
 auto ordinary=layout(d,false,false);assert(ordinary.y==360 && ordinary.height==80);
 auto inx=layout(d,false,true);assert(inx.y==363 && inx.height==80);
 auto explicitUpstream=layout(d,true,true);assert(explicitUpstream.y==360);
 Device inset{{0,0,480,800},{190,360,50,40}};
 auto constrained=layout(inset,false,false);
 assert(constrained.x==190 && constrained.y==360 && constrained.width==50 && constrained.height==40);
 Device tiny{{0,0,12,12},{5,7,0,0}};
 auto empty=layout(tiny,false,false);assert(empty.x==5 && empty.y==7 && empty.width==0 && empty.height==0);
}
""")

    def test_header_rect_keeps_inx_and_removes_shared_double_bezel(self):
        source = (ROOT / "src/components/UITheme.cpp").read_text()
        bodies = "\n".join(method(source, name) for name in (
            "Rect UITheme::getContentArea(", "Rect UITheme::getScreenSafeArea(", "Rect UITheme::getHeaderRect("))
        program = r"""
#include <algorithm>
#include <cassert>
#pragma GCC diagnostic ignored "-Wunused-parameter"
struct Rect {int x,y,width,height;};
struct ThemeMetrics {int topPadding=10,headerHeight=80,buttonHintsHeight=24;};
struct CrossPointSettings {enum class UI_THEME {CLASSIC,INX};};
struct GfxRenderer {
 enum class Orientation {Portrait,LandscapeClockwise,PortraitInverted,LandscapeCounterClockwise};
 Orientation orientation=Orientation::Portrait;
 Orientation getOrientation() const{return orientation;}
 int getScreenWidth()const{return 480;} int getScreenHeight()const{return 800;}
 void getOrientedViewableTRBL(int* t,int* r,int* b,int* l)const{*t=11;*r=7;*b=13;*l=5;}
};
struct UITheme {
 CrossPointSettings::UI_THEME currentType=CrossPointSettings::UI_THEME::CLASSIC;
 ThemeMetrics metrics;
 static UITheme& getInstance(){static UITheme t;return t;}
 const ThemeMetrics& getMetrics()const{return metrics;}
 bool hasMainTabs()const{return currentType==CrossPointSettings::UI_THEME::INX;}
 static Rect getContentArea(const GfxRenderer&);
 Rect getScreenSafeArea(const GfxRenderer&,bool=false,bool=false);
 Rect getHeaderRect(const GfxRenderer&,bool=false,bool=false,bool=false);
};
const ThemeMetrics& uiThemeMetrics(bool reading) {
 static const ThemeMetrics sharedReading{17,90,24};
 return reading&&UITheme::getInstance().hasMainTabs()?sharedReading:UITheme::getInstance().getMetrics();
}
""" + bodies + r"""
int main(){
 GfxRenderer r;auto& theme=UITheme::getInstance();
 for(bool inx:{false,true})for(bool reading:{false,true})for(bool hints:{false,true}) {
  theme.currentType=inx?CrossPointSettings::UI_THEME::INX:CrossPointSettings::UI_THEME::CLASSIC;
  const int top=inx&&reading?17:10, height=inx&&reading?90:80;
  for(auto orientation:{GfxRenderer::Orientation::Portrait,GfxRenderer::Orientation::LandscapeClockwise,
                       GfxRenderer::Orientation::PortraitInverted,GfxRenderer::Orientation::LandscapeCounterClockwise}){
   r.orientation=orientation;auto safe=theme.getScreenSafeArea(r,hints,false);
   auto header=theme.getHeaderRect(r,hints,false,reading);
   if(inx){assert(header.x==safe.x&&header.y==safe.y+top&&header.width==safe.width);}
   else {
    assert(header.x==(hints&&orientation==GfxRenderer::Orientation::LandscapeClockwise?24:0));
    assert(header.y==top+(hints&&orientation==GfxRenderer::Orientation::PortraitInverted?24:0));
    assert(header.width==480-(hints&&(orientation==GfxRenderer::Orientation::LandscapeClockwise||
                                    orientation==GfxRenderer::Orientation::LandscapeCounterClockwise)?24:0));
   }
   assert(header.height==height&&header.x>=0&&header.y>=0&&header.x+header.width<=480);
  }
 }
}
"""
        for defines in ((), ("CROSSMUX_UI_PROFILE_HIGH_DPI", "FREEINK_DEVICE_READPICO=1")):
            run_cpp(program, defines=defines)

    def test_battery_and_status_offsets_preserve_inx(self):
        source = (ROOT / "src/components/themes/BaseTheme.cpp").read_text()
        battery = method(source, "void BaseTheme::drawBatteryLeft(")
        status = method(source, "void BaseTheme::drawStatusBar(")
        geometry = status[status.index("  // Draw Progress Text"):status.index("  if (sb.showBookProgressPercent")]
        library = (ROOT / "src/activities/library/LibraryListActivity.cpp").read_text()
        corner = method(library, "int bottomCornerClearance()")
        program = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
struct Rect {int x,y,width,height;};
constexpr int SMALL_FONT_ID=8;
struct EpdFontFamily {enum Style {REGULAR};};
struct Glyph {int top=12,height=10;};
struct Face {bool present=true;Glyph glyph;const Glyph* getGlyph(char,EpdFontFamily::Style)const{return present?&glyph:nullptr;}};
struct GfxRenderer {
 std::map<int,Face> fonts{{SMALL_FONT_ID,{}}};
 const auto& getFontMap()const{return fonts;}
 int getFontAscenderSize(int)const{return 16;}
 int getScreenWidth()const{return 480;} int getScreenHeight()const{return 800;}
 void drawText(int,int,int,const char*)const{}
};
struct UITheme {
 bool inx=false;
 bool hasMainTabs()const{return inx;}
 static UITheme& getInstance(){static UITheme t;return t;}
 static int getStatusBarHeight(){return 24;}
 static int getStatusBarTextTopPadding(const GfxRenderer&){return UiHighDpiProfile::enabled?7:0;}
};
namespace BoardConfig {struct Profile {int cornerRadius=32,bottomCornerRadius=40,statusBarOffsetY=6;};inline Profile ACTIVE;}
struct {uint16_t getBatteryPercentage()const{return 56;}} powerManager;
constexpr int batteryPercentSpacing=4;
int batteryY=0;
struct BaseTheme {
 static constexpr int STATUS_NUMERIC_FONT_ID=8;
 void drawBatteryLeft(const GfxRenderer&,Rect,bool)const;
 void drawBatteryOutline(const GfxRenderer&,int,int y,int,int)const{batteryY=y;}
 void fillBatteryIcon(const GfxRenderer&,Rect rect,uint16_t)const{assert(rect.y==batteryY);}
};
""" + battery + corner + r"""
struct Geometry {int y,left,right;};
Geometry layout(const GfxRenderer& renderer){
 const int orientedMarginBottom=10,orientedMarginLeft=5,orientedMarginRight=9,paddingBottom=3;
 struct {int statusBarHorizontalMargin=12;} metrics;
""" + geometry + r"""
 (void)leftClusterWidth; (void)rightClusterWidth;
 return {textY,leftClusterX,rightClusterX};
}
int main(){
 GfxRenderer r;BaseTheme theme;
 for(bool inx:{false,true}){
  UITheme::getInstance().inx=inx;
  theme.drawBatteryLeft(r,{0,100,16,8},true);
  assert(batteryY==100+(inx?(UiHighDpiProfile::enabled?8:6):5));
  r.fonts[SMALL_FONT_ID].present=false;
  theme.drawBatteryLeft(r,{0,100,16,8},false);
  assert(batteryY==100+(inx&&UiHighDpiProfile::enabled?8:6));
  r.fonts[SMALL_FONT_ID].present=true;
  auto value=layout(r);
#if FREEINK_DEVICE_READPICO
  constexpr int oldY=800-24-10-3+7;
#else
  constexpr int oldY=800-24-10-3-4;
#endif
  assert(value.y==oldY+(inx?0:6));
  assert(value.left==12+(inx?32:40)+1&&value.right==480-12-(inx?32:40));
  assert(bottomCornerClearance()==(inx?32:40));
 }
 BoardConfig::ACTIVE.bottomCornerRadius=0;UITheme::getInstance().inx=false;
 assert(bottomCornerClearance()==32&&layout(r).left==45);
}
"""
        for defines in ((), ("CROSSMUX_UI_PROFILE_HIGH_DPI", "FREEINK_DEVICE_READPICO=1")):
            run_cpp(program, defines=defines)

    def test_filtered_catalog_rows_keep_memory_guard(self):
        source = (ROOT / "src/activities/plugins/PluginCatalogActivity.cpp").read_text()
        parse = method(source, "bool PluginCatalogActivity::parseBrowseResponse()")
        append = parse[parse.index("  size_t retainedBytes"):parse.index("  // Streamed straight")]
        constants = source[source.index("constexpr size_t MAX_CATALOG_BYTES"):source.index("bool catalogHasSpace(")]
        budget = method(source, "bool catalogHasSpace(")
        run_cpp(r'''
#include <cassert>
#include <string>
#include <vector>
#include <Memory.h>
#include "activities/plugins/PluginCatalogPaging.h"
struct {size_t getFreeHeap() const {return 128*1024;} size_t getMaxAllocHeap() const {return 96*1024;}} ESP;
struct JsonListParser {
 enum {F_TITLE,F_AUTHOR,F_ID,F_URL,F_VERSION,F_BASE,F_COUNT};
 struct Row {std::string field[F_COUNT];std::vector<std::string> files;};
};
struct Item {std::string title,author,id,url,version,base;std::vector<std::string> files;};
struct Result {std::vector<Item> items;bool oom,hasMore;};
''' + constants + budget + r'''
Result parseRows(std::vector<JsonListParser::Row> rows,bool filtered,std::vector<int> compatibleRows,size_t pageOffset=0) {
 struct {int pageSize=2;bool filtered;bool filtersCompatibility() const {return filtered;}} manifest{2,filtered};
 std::vector<Item> items;items.reserve(manifest.pageSize+1);
''' + append + r'''
 for(auto& row : rows) append(row);
 return {std::move(items),outOfMemory,visibleWindow.hasMore()};
}
int main() {
 std::vector<JsonListParser::Row> rows(3);
 rows[0].field[0]="one";rows[1].field[0]="two";rows[2].field[0]="three";
 auto all=parseRows(rows,false,{});assert(!all.oom && all.items.size()==2 && all.hasMore);
 assert(all.items[0].title=="one" && all.items[1].title=="two");
 auto second=parseRows(rows,false,{},2);assert(!second.oom && second.items.size()==1 && !second.hasMore);
 assert(second.items[0].title=="three");
 auto filtered=parseRows(rows,true,{0,2});assert(!filtered.oom && filtered.items.size()==2);
 assert(filtered.items[0].title=="one" && filtered.items[1].title=="three");
 rows[0].field[0]=std::string(50*1024,'x');
 auto large=parseRows(rows,false,{});assert(large.oom && large.items.empty());
 auto excluded=parseRows(rows,true,{1});assert(!excluded.oom && excluded.items.size()==1 && excluded.items[0].title=="two");
 auto skipped=parseRows(rows,false,{},1);assert(!skipped.oom && skipped.items.size()==2);
 assert(skipped.items[0].title=="two" && skipped.items[1].title=="three");
}
''', include_dirs=[ROOT / "lib/Memory", ROOT / "src"], defines=["SIMULATOR"])


if __name__ == "__main__":
    unittest.main()
