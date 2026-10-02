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
