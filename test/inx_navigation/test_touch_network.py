#!/usr/bin/env python3
"""Exercise production popup contacts, WiFi actions and footer hit geometry."""
from pathlib import Path
import subprocess
import tempfile
from test_theme_menus import method, run

ROOT = Path(__file__).resolve().parents[2]
popup = (ROOT / 'src/components/OptionPopup.h').read_text()
a = popup.index('  bool handleInput(')
a_body = popup.index('{', a)
end = a_body + 1
depth = 1
while depth:
    depth += (popup[end] == '{') - (popup[end] == '}')
    end += 1
handler = popup[a:end]
code = r'''
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "FreeInkUICore.h"
#include "InxItemLayout.h"
namespace fui=freeink::ui;
struct UITheme {static UITheme& getInstance(){static UITheme t;return t;} bool hasMainTabs()const{return true;}};
struct MappedInputManager {
 enum class Button {NavPrevious,NavNext,Confirm,Back};
 enum class SwipeDir {None,Up,Down};
 fui::InputSnapshot snap{};SwipeDir swipe=SwipeDir::None;
 int pressed=-1,released=-1,held=-1;
 bool wasPressed(Button b)const{return pressed==int(b);}
 bool wasReleased(Button b)const{return released==int(b);}
 bool isPressed(Button b)const{return held==int(b);}
 SwipeDir wasSwipe()const{return swipe;}
};
fui::InputSnapshot touchSnapshotFrom(const MappedInputManager& i){return i.snap;}
struct OptionPopup {
 bool upstreamStyle=false,active=true,ignoreInitialTouchContact=false,ignoreInitialConfirmRelease=false,uiReady=true;
 int selectedIndex=0;std::atomic<int> visibleOptionRows{1};
 static constexpr int MAX_OPTIONS=24;
 static constexpr fui::ActionId ACTION_OPTION=1,ACTION_CHROME=2;
 std::vector<std::string> ownedStrings=std::vector<std::string>(12,"option");
 fui::InteractionBuffer<24> interactions;
 std::function<void(int)> onSelectCallback;
 @HANDLER@
 void publish(Rect safe){
  const auto layout=InxOptionGeometry::layout(safe,12,selectedIndex);
  visibleOptionRows=layout.rows;
  interactions.beginPublishCycle();interactions.clear();
  for(int slot=0;slot<layout.rows;++slot){
   const auto r=layout.optionRect(slot);
   interactions.addInteraction(fui::Interaction{fui::Rect{int16_t(r.x),int16_t(r.y),int16_t(r.width),int16_t(r.height)},ACTION_OPTION,int16_t(layout.first+slot),fui::InputTouch});
  }
  interactions.publish();
 }
};
int main(){
 int scenes=0;
 for(Rect safe:{Rect{0,0,480,750},Rect{5,8,684-10,1216-13},Rect{8,5,1216-13,684-10},Rect{0,0,800,340}}){
  OptionPopup p;int calls=0,chosen=-1,updates=0;
  p.onSelectCallback=[&](int i){++calls;chosen=i;};
  const auto update=[&]{++updates;};MappedInputManager input;
  p.ignoreInitialTouchContact=true;input.snap.touchPressed=true;
  p.publish(safe);p.handleInput(input,update);
  input.snap={};input.snap.touchReleased=true;p.handleInput(input,update);
  assert(p.active&&calls==0&&!p.ignoreInitialTouchContact);
  input={};p.handleInput(input,update);
  // A swipe clears the pressed interaction and moves by the actual drawn row count.
  auto layout=InxOptionGeometry::layout(safe,12,p.selectedIndex);
  auto row=layout.optionRect(1);input.snap.touchPressed=true;input.snap.touchX=row.x+5;input.snap.touchY=row.y+5;
  p.handleInput(input,update);assert(p.selectedIndex==0);
  input.snap={};input.snap.touchReleased=true;input.snap.touchX=input.snap.touchY=-1;input.swipe=MappedInputManager::SwipeDir::Up;
  p.handleInput(input,update);assert(p.selectedIndex==layout.rows&&p.active&&calls==0);
  assert(p.interactions.activeIndex()<0);p.publish(safe);
  input={};input.snap.touchReleased=true;input.snap.touchX=input.snap.touchY=-1;p.handleInput(input,update);
  assert(p.active&&calls==0);
  for(int i=0;i<20;++i){input={};input.swipe=MappedInputManager::SwipeDir::Up;p.handleInput(input,update);p.publish(safe);}
  assert(p.selectedIndex==11&&p.active&&calls==0);
  for(int i=0;i<20;++i){input={};input.swipe=MappedInputManager::SwipeDir::Down;p.handleInput(input,update);p.publish(safe);}
  assert(p.selectedIndex==0&&p.active&&calls==0);
  layout=InxOptionGeometry::layout(safe,12,0);row=layout.optionRect(1);
  input={};input.snap.touchPressed=true;input.snap.touchX=row.x+row.width/2;input.snap.touchY=row.y+row.height/2;
  p.handleInput(input,update);assert(p.selectedIndex==0);
  input.snap.touchPressed=false;input.snap.touchReleased=true;p.handleInput(input,update);
  assert(!p.active&&calls==1&&chosen==layout.first+1);assert(!p.handleInput(input,update));
  // A confirm held across entry must not activate on its inherited release.
  OptionPopup q;q.ignoreInitialConfirmRelease=true;q.onSelectCallback=[&](int){++calls;};
  input={};input.released=int(MappedInputManager::Button::Confirm);q.handleInput(input,update);
  assert(q.active&&calls==1);q.handleInput(input,update);assert(!q.active&&calls==2);
  ++scenes;
 }
}
'''.replace('@HANDLER@', handler)
with tempfile.TemporaryDirectory(prefix='touch-network-') as directory:
    directory=Path(directory)
    for high in (False,True):
        source=directory/'popup.cpp';source.write_text(code)
        binary=directory/'popup'
        command=['c++','-std=c++20','-I'+str(ROOT/'src'),'-I'+str(ROOT/'freeink-sdk/libs/ui/FreeInkUI/include'),str(source),'-o',str(binary)]
        if high:command.insert(2,'-DCROSSMUX_UI_PROFILE_HIGH_DPI=1')
        subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)

wifi=(ROOT/'src/activities/network/WifiSelectionActivity.cpp').read_text()
handlers='\n'.join(method(wifi,'WifiSelectionActivity::'+name) for name in ('onScanEvent','onCancelEvent','onReturnEvent','returnFromFailure'))
trace=(ROOT/'test/inx_navigation/InxStyleParity.cpp').read_text().split('#ifdef UPSTREAM_THEME_PARITY')[0]
code=trace+r'''
#include <cassert>
#include <algorithm>
namespace fui=freeink::ui;
using UiScreen=fui::Screen<24>;
struct UITheme {static UITheme& getInstance(){static UITheme t;return t;} struct {int listRowHeight=56;} metrics;const auto& getMetrics()const{return metrics;}};
namespace UiHighDpiProfile {inline bool enabled=false;constexpr int buttonHeight=96,controlGap=12;}
enum class StrId {STR_CANCEL, STR_BACK};const char* translate(StrId id){return id==StrId::STR_BACK?"Back":"Cancel";}
#define tr(id) translate(StrId::id)
struct {int deletes=0,disconnects=0;void scanDelete(){++deletes;}void disconnect(){++disconnects;}} WiFi;
struct WifiSelectionActivity {
 enum class WifiSelectionState {NETWORK_ERROR,SCANNING,NETWORK_LIST,AUTO_CONNECTING,CONNECTING,CONNECTION_FAILED,FORGET_PROMPT};
 WifiSelectionState state=WifiSelectionState::NETWORK_LIST;
 bool autoConnecting=false,manualNetworkListRequested=false,usedSavedPassword=false,completed=false;
 int forgetPromptSelection=-1,scans=0,updates=0;
 struct {void clearTapFlash(){}} app;
 struct {bool hasTouch()const{return true;}} mappedInput;
 void closeRouting(){}void startWifiScan(){++scans;state=WifiSelectionState::SCANNING;}
 void requestUpdate(){++updates;}void onComplete(bool ok){assert(!ok);completed=true;}
 void showNetworkListFromAutoConnect(){autoConnecting=false;state=WifiSelectionState::NETWORK_LIST;}
 static void onScanEvent(const fui::ActionEvent&,void*);static void onCancelEvent(const fui::ActionEvent&,void*);
 static void onReturnEvent(const fui::ActionEvent&,void*);void returnFromFailure();
 void addTouchControls(UiScreen&,const char*,fui::ActionId);
};
@HANDLERS@
constexpr fui::ActionId ACTION_CANCEL=4;
@FOOTER@
int main(){
 using S=WifiSelectionActivity::WifiSelectionState;
 for(S state:{S::NETWORK_ERROR,S::SCANNING,S::NETWORK_LIST,S::AUTO_CONNECTING,S::CONNECTING,S::CONNECTION_FAILED}){
  WifiSelectionActivity a;a.state=state;WiFi.deletes=WiFi.disconnects=0;
  a.onCancelEvent({},&a);assert(a.completed);
  assert(WiFi.deletes==(state==S::SCANNING));assert(WiFi.disconnects==(state==S::CONNECTING||state==S::AUTO_CONNECTING));
  WifiSelectionActivity b;b.state=state;b.onScanEvent({},&b);
  assert(b.scans==(state==S::NETWORK_ERROR||state==S::NETWORK_LIST||state==S::CONNECTION_FAILED));
 }
 for(bool saved:{false,true}){WifiSelectionActivity a;a.state=S::CONNECTION_FAILED;a.usedSavedPassword=saved;
  a.onReturnEvent({},&a);assert(a.state==(saved?S::FORGET_PROMPT:S::NETWORK_LIST));}
 WifiSelectionActivity a;a.state=S::SCANNING;a.autoConnecting=true;a.onReturnEvent({},&a);
 assert(a.state==S::SCANNING&&!a.autoConnecting&&a.manualNetworkListRequested);
 for(S state:{S::NETWORK_LIST,S::NETWORK_ERROR})for(bool high:{false,true})for(bool landscape:{false,true})for(bool pair:{false,true}){
  a.state=state;
  UiHighDpiProfile::enabled=high;TraceTarget target;fui::DeviceContext device;
  device.width=landscape?1216:684;device.height=landscape?684:1216;device.hasTouch=true;
  device.safeArea=fui::Insets{5,5,8,5};fui::InteractionBuffer<24> hits;fui::InputSnapshot input;
  fui::Frame<24> frame(target,device,input,hits);auto tokens=fui::themeTokensForLineHeight(24);tokens.rowHeight=56;UiScreen screen(frame,tokens);screen.takeBottom(48,12);
  a.addTouchControls(screen,pair?"Retry":nullptr,2);
  assert(hits.count()==(pair?2:1));const auto& cancel=hits.data()[0];assert(cancel.action==4&&cancel.rect.height==(high?96:std::max(screen.theme().rowHeight,screen.theme().minTouchSize)));
  if(pair){const auto& retry=hits.data()[1];assert(retry.action==2&&retry.rect.x-cancel.rect.right()>=6);}
  for(size_t i=0;i<hits.count();++i){const auto r=hits.data()[i].rect;assert(r.x>=0&&r.right()<=device.width&&r.y>=5&&r.bottom()<=device.height-8);}
 }
}
'''
code=code.replace('@HANDLERS@',handlers).replace('@FOOTER@',method(wifi,'WifiSelectionActivity::addTouchControls'))
with tempfile.TemporaryDirectory(prefix='wifi-controls-') as directory:
    run(code,Path(directory),sdk=True)
print('PASS: production popup contacts/swipes and WiFi actions/footer geometry, regular and high PPI')

# Draw the real screen builder and shared legacy text renderer with recorded glyph bounds.
ui=(ROOT/'src/components/UITheme.cpp').read_text()
utf8=(ROOT/'lib/Utf8/Utf8.cpp').read_text()
text_methods='\n'.join(method(ui,'UITheme::'+name) for name in ('drawCenteredText','drawCenteredWrappedText'))
code=r'''
#include <FreeInkApp.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "@UTF8_HEADER@"
namespace fui=freeink::ui;
using UiScreen=fui::Screen<24>;
struct Rect {int x,y,width,height;};
namespace EpdFontFamily {enum Style {REGULAR,BOLD};}
constexpr int UI_10_FONT_ID=1,UI_12_FONT_ID=2,SMALL_FONT_ID=3;
namespace freeink::ui {struct GfxRendererTarget {static constexpr FontId FONT_BODY=1;};}
namespace UiHighDpiProfile {inline bool enabled=false;constexpr int buttonHeight=96,controlGap=12;}
struct Draw {Rect rect;std::string text;int font;EpdFontFamily::Style style;};
struct GfxRenderer {
 int width=480,height=800;std::vector<Draw> draws;
 int getScreenWidth()const{return width;} int getScreenHeight()const{return height;}
 int getLineHeight(int font)const{return (font==UI_12_FONT_ID?28:24)*(UiHighDpiProfile::enabled?2:1);}
 int getTextWidth(int font,const char* text,EpdFontFamily::Style=EpdFontFamily::REGULAR)const {
  return std::strlen(text)*(getLineHeight(font)/3);
 }
 void drawText(int font,int x,int y,const char* text,bool=true,EpdFontFamily::Style style=EpdFontFamily::REGULAR) {
  draws.push_back({{x,y,getTextWidth(font,text,style),getLineHeight(font)},text,font,style});
 }
 std::vector<std::string> wrappedText(int font,const char* text,int width,int lines,EpdFontFamily::Style)const {
  const size_t length=std::max(1,width/(getLineHeight(font)/3));std::string rest=text;
  std::vector<std::string> result;
  while(!rest.empty() && static_cast<int>(result.size())<lines) {
   result.push_back(rest.substr(0,length));rest.erase(0,length);
  }
  return result;
 }
 struct ClipScope {ClipScope(const GfxRenderer&,int,int,int,int){}};
};
struct ThemeMetrics {int topPadding,headerHeight,tabBarHeight,verticalSpacing,contentSidePadding;};
struct UITheme {
 enum class TextVerticalAlignment {TOP,CENTER,BOTTOM};
 ThemeMetrics metrics{10,84,40,16,20};
 static UITheme& getInstance(){static UITheme theme;return theme;}
 const ThemeMetrics& getMetrics()const{return metrics;}
 Rect getScreenSafeArea(const GfxRenderer& r,bool,bool)const{return {5,8,r.width-13,r.height-13};}
 static void drawCenteredText(GfxRenderer&,Rect,int,int,const char*,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR);
 static void drawCenteredWrappedText(GfxRenderer&,Rect,int,const char*,int,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR,
                                    TextVerticalAlignment=TextVerticalAlignment::CENTER);
};
struct Gui {int hints=0;void drawButtonHints(GfxRenderer&,const char*,const char*,const char*,const char*){++hints;}} GUI;
struct MappedInput {
 bool touch=true;bool hasTouch()const{return touch;}
 struct Labels {const char* btn1;const char* btn2;const char* btn3;const char* btn4;};
 Labels mapLabels(const char* a,const char* b,const char* c,const char* d)const{return {a,b,c,d};}
};
constexpr fui::ActionId ACTION_CANCEL=4,ACTION_RETURN=5,ACTION_SCAN=2,ACTION_ROW=1;
enum class WifiSelectionState {SCANNING,AUTO_CONNECTING,CONNECTING,NETWORK_ERROR,CONNECTION_FAILED,SAVE_PROMPT,FORGET_PROMPT,NETWORK_LIST};
constexpr int STR_CANCEL=0,STR_BACK=1,STR_SHOW_NETWORKS=2,STR_RETRY=3,STR_ERROR_GENERAL_FAILURE=4,
 STR_NO_NETWORKS=5,STR_CONNECTION_FAILED=6,STR_FINDING_SAVED_WIFI=7,STR_SCANNING=8,
 STR_CONNECTING_SAVED_WIFI=9,STR_CONNECTING=10,STR_TO_PREFIX=11;
bool chinese=false;
const char* tr(int id) {
 switch(id) {
 case STR_CANCEL:return "Cancel";case STR_BACK:return "Back";case STR_SHOW_NETWORKS:return "Show networks";
 case STR_TO_PREFIX:return chinese?"至 ":"to ";case STR_FINDING_SAVED_WIFI:return "Finding saved WiFi";
 case STR_SCANNING:return "Scanning";case STR_CONNECTING_SAVED_WIFI:return chinese?"Connecting to a saved wireless network with a translated status message":"Connecting saved WiFi";
 case STR_CONNECTING:return "Connecting";default:return "Error";
 }
}
struct Target: fui::DrawTarget {
 int16_t lineHeight(fui::FontId)const override{return 24;}
 fui::Size measureText(fui::FontId,const char*,fui::TextStyle)const override{return {24,24};}
 void fill(fui::Rect,fui::Paint,uint8_t,uint8_t)override{}
 void stroke(fui::Rect,fui::Paint,uint8_t,uint8_t,uint8_t)override{}
 void line(fui::Point,fui::Point,uint8_t,fui::Paint)override{}
 void triangle(fui::Point,fui::Point,fui::Point,fui::Paint)override{}
 void bitmap(fui::Rect,fui::BitmapRef,fui::BitmapMode,fui::Paint,fui::Rotation)override{}
 void text(fui::Rect,const char*,fui::TextStyle)override{}
};
struct WifiSelectionActivity {
 WifiSelectionState state=WifiSelectionState::CONNECTING;bool autoConnecting=false;
 GfxRenderer renderer;MappedInput mappedInput;std::string selectedSSID,connectionError;
 std::vector<int> networks;std::vector<fui::ListItem> networkRowItems;
 size_t selectedNetworkIndex=0;fui::ListNav listNav;
 int subtitleHeight()const{return UITheme::getInstance().metrics.tabBarHeight;}
 void buildPromptDialog(UiScreen&){}
 void addTouchControls(UiScreen&,const char*,fui::ActionId);
 void buildListScreen(UiScreen&);
 void renderConnecting(const Rect*,const ThemeMetrics*)const;
};
@UTF8_METHOD@
@TEXT_METHODS@
@METHODS@
int main() {
 int scenes=0;
 for(bool high:{false,true})for(int orientation=0;orientation<4;++orientation)
 for(ThemeMetrics theme:{ThemeMetrics{0,66,40,0,20},ThemeMetrics{10,84,48,16,20},ThemeMetrics{13,84,50,10,20}})
 for(int phase=0;phase<4;++phase)
 for(const char* ssid:{"HomeWiFi","12345678901234567890123456789012","中文网络中文网络中文"})
 for(bool localized:{false,true}) {
  UiHighDpiProfile::enabled=high;chinese=localized;
  if(high){theme.headerHeight=112;theme.tabBarHeight=64;theme.verticalSpacing=24;theme.contentSidePadding=32;}
  UITheme::getInstance().metrics=theme;
  const auto state=phase==0?WifiSelectionState::CONNECTING:phase==1?WifiSelectionState::AUTO_CONNECTING:WifiSelectionState::SCANNING;
  WifiSelectionActivity a;a.state=state;a.autoConnecting=phase==1||phase==3;a.selectedSSID=ssid;
  a.renderer.width=high?684:480;a.renderer.height=high?1216:800;
  if(orientation%2)std::swap(a.renderer.width,a.renderer.height);
  fui::DeviceContext device;device.width=a.renderer.width;device.height=a.renderer.height;
  device.hasTouch=true;device.safeArea={8,8,5,5};
  fui::InteractionBuffer<24> hits;fui::InputSnapshot input;Target target;
  fui::Frame<24> frame(target,device,input,hits);auto tokens=fui::themeTokensForLineHeight(24);
  UiScreen screen(frame,tokens);GUI.hints=0;
  a.buildListScreen(screen);
  const auto body=screen.contentRect();
  assert(GUI.hints==0 && hits.count()==(a.autoConnecting?2:1));
  assert(!a.renderer.draws.empty());
  const auto last=a.renderer.draws.back();
  for(const auto& draw:a.renderer.draws) {
   assert(std::abs((draw.rect.x*2+draw.rect.width)-(body.x*2+body.width))<=1);
   assert(draw.rect.y>=body.y && draw.rect.y+draw.rect.height<=body.bottom());
   assert(draw.rect.x>=body.x+theme.contentSidePadding);
   assert(draw.rect.x+draw.rect.width<=body.right()-theme.contentSidePadding);
   for(size_t i=0;i<hits.count();++i)assert(draw.rect.y+draw.rect.height<=hits.data()[i].rect.y);
  }
  if(state==WifiSelectionState::SCANNING) {
   assert(last.text==(a.autoConnecting?"Finding saved WiFi":"Scanning") && last.font==UI_10_FONT_ID);
  } else {
   assert(a.renderer.draws.size()>=2);
   assert(last.text.starts_with(tr(STR_TO_PREFIX)) && last.font==UI_10_FONT_ID);
   for(size_t i=0;i+1<a.renderer.draws.size();++i) {
    const auto& title=a.renderer.draws[i];
    assert(title.font==UI_12_FONT_ID && title.style==EpdFontFamily::BOLD);
    assert(title.rect.y+title.rect.height+theme.verticalSpacing<=last.rect.y);
   }
   if(std::strlen(ssid)+std::strlen(tr(STR_TO_PREFIX))>25)assert(last.text.ends_with("...") && last.text.size()<=25);
   if(std::string(ssid).starts_with("中文"))assert(last.text==std::string(tr(STR_TO_PREFIX))+"中文网络中文...");
  }
  if(hits.count()==2)assert(hits.data()[1].rect.x-hits.data()[0].rect.right()>=6);
  a.mappedInput.touch=false;a.renderer.draws.clear();GUI.hints=0;
  const Rect bounds{body.x,body.y,body.width,body.height};a.renderConnecting(&bounds,&theme);
  assert(GUI.hints==(a.autoConnecting?1:0));
  assert(!a.renderer.draws.empty());
  assert(a.renderer.draws.back().text==last.text);
  ++scenes;
 }
 assert(scenes==576);
}
'''
# Const renderer methods in production record draw calls through a mutable test seam.
code=code.replace('std::vector<Draw> draws;', 'mutable std::vector<Draw> draws;')
code=code.replace('EpdFontFamily::Style style=EpdFontFamily::REGULAR) {', 'EpdFontFamily::Style style=EpdFontFamily::REGULAR) const {')
code=code.replace('static void drawCenteredText(GfxRenderer&', 'static void drawCenteredText(const GfxRenderer&')
code=code.replace('static void drawCenteredWrappedText(GfxRenderer&', 'static void drawCenteredWrappedText(const GfxRenderer&')
code=code.replace('void drawButtonHints(GfxRenderer&', 'void drawButtonHints(const GfxRenderer&')
code=code.replace('@UTF8_HEADER@',str(ROOT/'lib/Utf8/Utf8.h'))
code=code.replace('@UTF8_METHOD@',method(utf8,'utf8CodepointLen')+'\n'+method(utf8,'utf8SafeTruncateBuffer'))
code=code.replace('@TEXT_METHODS@',text_methods)
code=code.replace('@METHODS@','\n'.join(method(wifi,'WifiSelectionActivity::'+name) for name in ('addTouchControls','buildListScreen','renderConnecting')))
with tempfile.TemporaryDirectory(prefix='wifi-status-') as directory:
    run(code,Path(directory),sdk=True)
print('PASS: shared WiFi status/SSID centering, UTF-8 ellipsis and touch controls across 576 scenes')
