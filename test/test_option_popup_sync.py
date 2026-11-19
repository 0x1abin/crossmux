"""Exercise production popup ownership and viewport methods with reviewed SDK navigation."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SDK=ROOT/"freeink-sdk/libs/ui/FreeInkUI"

def method(source,name):
    start=source.index(name);end=source.index("{",start)+1;depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[start:end]

source=(ROOT/"src/components/OptionPopup.h").read_text()
code=r"""
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
#include <components/lists/list.h>
namespace fui=freeink::ui;
namespace haptic_feedback{void touchAction(){}}
struct UITheme{bool inx=false;bool hasMainTabs()const{return inx;}static UITheme& getInstance(){static UITheme t;return t;}};
struct MappedInputManager{
 enum class Button{Confirm,Back,NavPrevious,NavNext,Count};
 enum class SwipeDir{None,Up,Down};
 fui::InputSnapshot snapshot{};SwipeDir swipe=SwipeDir::None;
 std::array<bool,4> pressed{},released{},held{};
 bool wasPressed(Button b)const{return pressed[static_cast<int>(b)];}
 bool wasReleased(Button b)const{return released[static_cast<int>(b)];}
 bool isPressed(Button b)const{return held[static_cast<int>(b)];}
 SwipeDir wasSwipe()const{return swipe;}
};
fui::InputSnapshot touchSnapshotFrom(const MappedInputManager& input){return input.snapshot;}
struct Popup{
 const bool upstreamStyle=false;
 bool active=false,confirmBarrierPending=false,ignoreInitialConfirmRelease=false,ignoreInitialTouchContact=false;
 bool uiReady=true;
 std::string message;
 std::vector<std::string> ownedStrings;
 std::atomic<int> selectedIndex{0};
 mutable std::atomic<int> firstOption{-1},visibleOptionRows{1},pendingScroll{0};
 mutable std::atomic<bool> followSelection{true};
 std::function<void(int)> onSelectCallback;
 static constexpr int ACTION_OPTION=1,ACTION_CHROME=2;
 struct Interactions{
  fui::ActionEvent event{};int clears=0;fui::Interaction hit{};
  fui::ActionEvent routePublished(const fui::InputSnapshot& snap){
   if(snap.touchReleased&&snap.touchX<0){++clears;return {};}
   return event;
  }
  int16_t activeIndex()const{return 0;}
  const fui::Interaction* publishedData()const{return &hit;}
 } interactions;
"""+"\n".join(method(source,name) for name in ["  bool handleInput(","  bool handleButtons(","  void finishShow(","  int windowStart("])+r"""
};
int main(){
 unsigned updates=0,callbacks=0;int chosen=-1;
 const auto update=[&]{++updates;};
 for(bool inx:{false,true}){
  UITheme::getInstance().inx=inx;
  Popup popup;popup.ownedStrings.resize(35,"option");
  popup.finishShow(27,[&](int value){++callbacks;chosen=value;});
  MappedInputManager idle;popup.handleInput(idle,update);
  int first=popup.windowStart(5);
  assert(first==(inx?25:23));
  popup.visibleOptionRows=5;
  MappedInputManager swipe;swipe.swipe=MappedInputManager::SwipeDir::Up;
  swipe.snapshot.touchReleased=true;swipe.snapshot.touchX=-1;swipe.snapshot.touchY=-1;
  popup.handleInput(swipe,update);
  assert(popup.active&&popup.selectedIndex==27&&callbacks==0&&popup.interactions.clears==1);
  assert(popup.windowStart(5)==std::min(first+5,30));
  for(int i=0;i<20;++i)popup.handleInput(swipe,update);
  assert(popup.windowStart(5)==30&&popup.selectedIndex==27);
  popup.handleButtons(false,true,false,false,update);
  int followed=popup.windowStart(5);
  assert(popup.selectedIndex==28&&followed<=28&&28<followed+5);
  popup.selectedIndex=34;
  popup.handleButtons(false,true,false,false,update);
  assert(popup.selectedIndex==0&&popup.windowStart(5)==0);
  popup.finishShow(32,[&](int value){++callbacks;chosen=value;});
  assert(popup.visibleOptionRows==1);
  MappedInputManager held;held.held[0]=true;
  popup.handleInput(held,update);assert(popup.active&&callbacks==0);
  MappedInputManager released;released.released[0]=true;
  popup.handleInput(released,update);assert(popup.active&&callbacks==0);
  MappedInputManager fresh;fresh.pressed[0]=fresh.released[0]=true;
  popup.handleInput(fresh,update);assert(!popup.active&&callbacks==1&&chosen==32);
  callbacks=0;
  popup.finishShow(0,[&](int value){++callbacks;chosen=value;});
  popup.interactions.event.action=Popup::ACTION_OPTION;popup.interactions.event.value=33;
  MappedInputManager touch;touch.snapshot.touchReleased=true;touch.snapshot.touchX=10;touch.snapshot.touchY=10;
  popup.handleInput(touch,update);assert(popup.active&&callbacks==0);
  popup.uiReady=true;popup.handleInput(touch,update);
  assert(!popup.active&&callbacks==1&&chosen==33);
  callbacks=0;
 }
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"popup.cpp";binary=Path(directory)/"popup"
    cpp.write_text(code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-I",str(SDK/"include"),str(cpp),str(SDK/"src/FreeInkUI.cpp"),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Production popup >16 options, viewport-only swipes, navigation follow, inherited held/release and touch single-dispatch pass")

# Full production class: render and publish the real SDK hit table, with fixed
# font measurements. This is not a framebuffer or whole-page comparison.
import re
trace=(ROOT/"test/inx_navigation/InxStyleParity.cpp").read_text().split("#ifdef UPSTREAM_THEME_PARITY")[0]
trace=trace.replace("using namespace freeink::ui;","namespace trace_fixture { using namespace freeink::ui;")+"}\nusing trace_fixture::TraceTarget;\n"
fields=sorted(set(re.findall(r"metrics\.(\w+)",source)))
metrics_source=(ROOT/"src/components/themes/lyra/LyraTheme.h").read_text()
metrics="struct Metrics {"+"".join("int "+key+"="+re.search(r"\."+key+r"\s*=\s*([^,}]+)",metrics_source)[1]+";" for key in fields)+"};"
complete="\n".join(line for line in source.splitlines() if not line.startswith(("#include", "#pragma"))).replace(" private:"," public:")
code=trace+r"""
#include <array>
#include <atomic>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
#include "InxItemLayout.h"
namespace fui=freeink::ui;
namespace haptic_feedback{void touchAction(){}}
struct GfxRenderer {
 int width=480,height=800;
 void displayBuffer()const{}
};
struct MappedInputManager{
 enum class Button{Confirm,Back,NavPrevious,NavNext,Count};
 enum class SwipeDir{None,Up,Down};
 fui::InputSnapshot snapshot{};SwipeDir swipe=SwipeDir::None;
 bool wasPressed(Button)const{return false;}
 bool wasReleased(Button)const{return false;}
 bool isPressed(Button)const{return false;}
 SwipeDir wasSwipe()const{return swipe;}
 struct Labels{const char*btn1;const char*btn2;const char*btn3;const char*btn4;};
 Labels mapLabels(const char*a,const char*b,const char*c,const char*d)const{return{a,b,c,d};}
};
fui::InputSnapshot touchSnapshotFrom(const MappedInputManager& input){return input.snapshot;}
namespace freeink::ui {
struct GfxRendererTarget:TraceTarget {
 static constexpr FontId FONT_SMALL=0,FONT_BODY=1,FONT_TITLE=2;
 DeviceContext device;
 DeviceContext deviceContext()const{return device;}
};
}
fui::GfxRendererTarget makeUiTarget(const GfxRenderer& renderer,bool){
 fui::GfxRendererTarget target;target.lineH=24;
 target.device.width=renderer.width;target.device.height=renderer.height;
 target.device.safeArea={11,7,13,9};return target;
}
const fui::ThemeTokens& refreshSharedUiThemeTokens(const fui::GfxRendererTarget& target,bool){
 static fui::ThemeTokens tokens;tokens=fui::themeTokensForLineHeight(target.lineHeight(1));return tokens;
}
@METRICS@
struct UITheme{
 bool inx=false;static UITheme&getInstance(){static UITheme t;return t;}
 bool hasMainTabs()const{return inx;}
 Rect getScreenSafeArea(const GfxRenderer&r,bool,bool)const{return Rect{9,35,r.width-16,r.height-70};}
 Metrics getMetrics()const{return {};}
 void drawButtonHints(const GfxRenderer&,const char*,const char*,const char*,const char*)const{}
 void drawOptionPopup(const GfxRenderer&r,const char*,const std::vector<std::string>& options,int selected,int first)const{
  auto layout=InxOptionGeometry::layout(getScreenSafeArea(r,true,false),options.size(),selected);
  for(int i=0;i<layout.rows;++i){assert(first+i>=0&&first+i<int(options.size()));assert(layout.optionRect(i).height>0);}
 }
};
#define GUI UITheme::getInstance()
Metrics uiThemeMetrics(bool){return {};}
enum class StrId{Any};
struct I18n{const char*get(StrId){return "option";}} I18N;
constexpr int STR_BACK=0,STR_SELECT=1,STR_DIR_UP=2,STR_DIR_DOWN=3;
const char* tr(int){return "hint";}
@COMPLETE@
int main(){
 for(bool inx:{false,true})for(bool landscape:{false,true})for(int count:{1,5,16,35}){
  GUI.inx=inx;GfxRenderer renderer;
  if(landscape){renderer.width=800;renderer.height=480;}
  OptionPopup popup;std::vector<std::string> options(count,"option");
  popup.show(StrId::Any,options,count-1,[](int){});
  MappedInputManager input;popup.handleInput(input,[]{});popup.render(renderer);
  const auto check=[&]{
   int rows=0,last=-1;const auto*hits=popup.interactions.publishedData();
   for(size_t i=0;i<popup.interactions.publishedCount();++i){
    const auto& hit=hits[i];if(hit.action!=popup.ACTION_OPTION)continue;
    assert(hit.value==popup.firstOption+rows&&hit.value<count&&hit.value>last);last=hit.value;
    assert(hit.rect.width>0&&hit.rect.height>0&&hit.rect.x>=0&&hit.rect.y>=0);
    assert(hit.rect.x+hit.rect.width<=renderer.width&&hit.rect.y+hit.rect.height<=renderer.height);
    if(rows)assert(hits[i-1].rect.y+hits[i-1].rect.height+6<=hit.rect.y);
    ++rows;
   }
   assert(rows==popup.visibleOptionRows&&rows>0&&rows<=16);
  };
  check();int selected=popup.selectedIndex;
  input.swipe=MappedInputManager::SwipeDir::Down;
  popup.handleInput(input,[]{});popup.render(renderer);check();assert(popup.selectedIndex==selected);
  popup.handleButtons(true,false,false,false,[]{});popup.render(renderer);check();
  assert(popup.selectedIndex>=popup.firstOption&&popup.selectedIndex<popup.firstOption+popup.visibleOptionRows);
  const auto* hits=popup.interactions.publishedData();
  const auto& hit=hits[popup.interactions.publishedCount()-1];
  input.swipe=MappedInputManager::SwipeDir::None;
  input.snapshot.touchPressed=true;input.snapshot.touchX=hit.rect.x+hit.rect.width/2;input.snapshot.touchY=hit.rect.y+hit.rect.height/2;
  popup.handleInput(input,[]{});input.snapshot.touchPressed=false;input.snapshot.touchReleased=true;
  popup.handleInput(input,[]{});assert(!popup.isActive());
 }
}
""".replace("@METRICS@",metrics).replace("@COMPLETE@",complete)
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"render.cpp";binary=Path(directory)/"render"
    cpp.write_text(code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-I",str(ROOT/"src"),"-I",str(SDK/"include"),str(cpp),str(SDK/"src/FreeInkUI.cpp"),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True,stdout=subprocess.DEVNULL)
print("Full production popup renders bounded contiguous hit tables in both orientations; swipe, follow and actual SDK touch dispatch pass")
