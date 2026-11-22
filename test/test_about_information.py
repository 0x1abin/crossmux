"""Compile actual About rows and compare INX with the pre-sync source."""
from pathlib import Path
import re
import json
import sys
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
def strip_includes(text):
    return "\n".join(line for line in text.splitlines() if not line.startswith(("#include", "#pragma")))
source=strip_includes((ROOT/"src/activities/settings/AboutActivity.cpp").read_text())
header=strip_includes((ROOT/"src/activities/settings/AboutActivity.h").read_text()).replace(" private:"," public:")
old_source=strip_includes(subprocess.check_output(["git","show","HEAD:src/activities/settings/AboutActivity.cpp"],cwd=ROOT,text=True))
old_header=strip_includes(subprocess.check_output(["git","show","HEAD:src/activities/settings/AboutActivity.h"],cwd=ROOT,text=True)).replace(" private:"," public:")
board=(ROOT/"freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h").read_text()
enums="\n".join(re.search(r"enum class "+name+r"[^}]+};",board).group() for name in ["DisplayController","TouchController"])
translations=dict(re.findall(r'^(STR_\w+): "(.*)"$',(ROOT/"lib/I18n/translations/english.yaml").read_text(),re.M))
keys=sorted(set(re.findall(r"\bSTR_\w+",source+old_source)))
pinned=subprocess.check_output(["git","show","622e5de9558399c92720e8f76deaed8a0cb1b6d3:src/activities/settings/AboutActivity.cpp"],cwd=ROOT,text=True)
primary=re.findall(r'"([^"\n]+)"',re.search(r'const char\* const menuNames\[[^]]+\] = \{(.*?)\};',pinned,re.S).group(1))
assert len(primary)==14
labels="\n".join("#define "+key+' "'+translations[key]+'"' for key in keys)
code=r"""
#include <FreeInkUI.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include "components/Rect.h"
namespace fui=freeink::ui;
const char* tr(const char* text){return text;}
#define CROSSPOINT_VERSION "test-version"
"""+labels+r"""
namespace BoardConfig {
"""+"@ENUMS@"+r"""
struct Profile{const char*name;DisplayController displayController;int displayWidth,displayHeight;struct{TouchController controller;}touch;};
Profile ACTIVE{"test-device",DisplayController::SSD1677,800,480,{TouchController::Cst836u}};
}
struct GfxRenderer{unsigned getDisplayWidth()const{return 792;}unsigned getDisplayHeight()const{return 528;}
int getScreenWidth()const{return 528;}int getScreenHeight()const{return 792;}};
struct UITheme {
 bool inx=false;static UITheme&getInstance(){static UITheme t;return t;}
 bool hasMainTabs()const{return inx;}
 struct Metrics{int topPadding=4,headerHeight=40,verticalSpacing=5;};Metrics getMetrics()const{return {};}
 Rect getScreenSafeArea(const GfxRenderer&,bool,bool)const{return Rect{0,0,528,792};}
};
struct{const char*name="ActualController";const char*controllerName()const{return name;}}display;
struct{bool present()const{return true;}} Frontlight;
struct Clock{bool isAvailable()const{return true;}} halClock,halTiltSensor;
unsigned long millis(){return 12345678;}
namespace HalSystem {
using DeviceId=std::array<uint8_t,6>;
bool macAvailable=true,temperatureAvailable=true;
struct Heap{size_t freeBytes=65536,totalBytes=262144,largestFreeBlockBytes=32768,freePsramBytes=1048576,totalPsramBytes=2097152;};
Heap heap;Heap getHeapInfo(){return heap;}
const char*getChipModel(){return "DeviceChip";}
bool getChipTemperatureCelsius(float& out){out=27.4f;return temperatureAvailable;}
bool getDeviceId(DeviceId& out){out={0,1,2,3,0xab,0xff};return macAvailable;}
bool getWifiStationMac(DeviceId&out){return getDeviceId(out);}
uint64_t getUptimeSeconds(){return 12345678/1000;}
}
struct{unsigned getFlashChipSize()const{return 16777216;}}ESP;
bool locked=false;int waits=0,updates=0;
struct RenderLock{template<class T>explicit RenderLock(T&){assert(!locked);locked=true;}~RenderLock(){locked=false;}};
struct StorageFixture {
 bool ok=true;uint64_t total=2147483648ULL,free=1073741824ULL;unsigned queries=0;
 std::function<void()>beforeQuery;
 bool getSpace(uint64_t&t,uint64_t&f){assert(!locked&&waits==1);++queries;if(beforeQuery)beforeQuery();t=total;f=free;return ok;}
}Storage;
struct UiScreen{fui::ThemeTokens tokens{};void setContentMarginFromScreen(fui::Insets){}void spacer(int16_t){}const fui::ThemeTokens&theme(){return tokens;}void list(fui::ListProps){}};
struct MappedInputManager{};
class UiListActivity {
 public:GfxRenderer&renderer;
 UiListActivity(const char*,GfxRenderer&r,MappedInputManager&):renderer(r){}
 virtual~UiListActivity()=default;virtual void onEnter(){}virtual int listCount()const{return 0;}
 virtual void buildScreen(UiScreen&){}virtual void activateIndex(int){}virtual const char*headerTitle()const{return "";}
 void requestUpdateAndWait(){assert(!locked);++waits;}void requestUpdate(){assert(!locked);++updates;}
 void syncListViewport(UiScreen&,fui::ListProps&){}
 static constexpr int ACTION_ROW=1;
};
"""+header+"\n"+source+"\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wswitch\"\nnamespace legacy {\n"+old_header+"\n"+old_source+"\n}\n#pragma GCC diagnostic pop\n"+r"""
int main(){
 assert(std::string(touchControllerName(static_cast<BoardConfig::TouchController>(255)))=="?");
#ifdef SIMULATOR
 assert(std::string(displayControllerName(BoardConfig::DisplayController::Epdiy))=="EPDiy");
#endif
 assert(formatBytes(0)=="0 MB"&&formatBytes(1ULL<<30)=="1.0 GB");
 assert(formatBytes(UINT64_MAX)=="17179869184.0 GB");
 GfxRenderer renderer;MappedInputManager input;
 const std::array<std::string,14> primary={@PRIMARY@};
 for(bool inx:{false,true})for(bool psram:{false,true})for(int sd=0;sd<5;++sd)for(bool validMac:{false,true}){
  UITheme::getInstance().inx=inx;HalSystem::heap.totalPsramBytes=psram?2097152:0;
  HalSystem::macAvailable=validMac;HalSystem::temperatureAvailable=validMac;
  Storage.ok=sd!=0;Storage.total=sd==2?0:sd==4?UINT64_MAX:2147483648ULL;
  Storage.free=sd==3?Storage.total+1:sd==4?0:1073741824ULL;
  AboutActivity activity(renderer,input);waits=updates=0;Storage.queries=0;
  Storage.beforeQuery=[&]{for(int i=0;i<activity.rowCount_;++i)
   if(std::string(activity.rowItems_[i].label).starts_with("SD "))assert(activity.rowValues_[i]=="Loading...");};
  activity.onEnter();assert(waits==1&&updates==1&&Storage.queries==1&&!locked);
  assert(activity.rowCount_==(inx?(psram?18:17):(psram?20:19)));
  for(int i=0;i<activity.rowCount_;++i)assert(activity.rowItems_[i].actionValue==i);
  int storageRow=inx?activity.rowCount_-1:4;
  const bool available=sd==1||sd==4;
  for(int i=0;i<(inx?1:3);++i)assert((activity.rowValues_[storageRow+i]==tr(STR_NOT_AVAILABLE))==!available);
  if(!inx){
   for(size_t i=0;i<primary.size();++i)assert(activity.rowItems_[i].label==primary[i]);
   assert(activity.rowValues_[7]=="ActualController"&&activity.rowValues_[8]=="792x528"&&activity.rowValues_[9]=="CST836U");
   if(sd==1){assert(activity.rowValues_[4]=="2.0 GB"&&activity.rowValues_[5]=="1.0 GB"&&activity.rowValues_[6]=="1.0 GB");}
  }else if(sd<2){
   legacy::AboutActivity old(renderer,input);waits=updates=0;Storage.beforeQuery={};old.onEnter();
   assert(old.rowCount_==activity.rowCount_);
   for(int i=0;i<old.rowCount_;++i){
    const std::string label=activity.rowItems_[i].label;assert(label==old.rowItems_[i].label);
    if(label!="Display Controller"&&label!="Resolution"&&label!="Touch")assert(activity.rowValues_[i]==old.rowValues_[i]);
   }
  }
 }
 display.name=nullptr;UITheme::getInstance().inx=false;Storage.beforeQuery={};waits=updates=0;
 AboutActivity fallback(renderer,input);fallback.onEnter();assert(fallback.rowValues_[7]=="SSD1677");
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"about.cpp";binary=Path(directory)/"about";
    ui=ROOT/"freeink-sdk/libs/ui/FreeInkUI"
    for defines in [("CONFIG_IDF_TARGET_ESP32C3",),("CONFIG_IDF_TARGET_ESP32S3",),("SIMULATOR",)]:
        selected=enums
        if "SIMULATOR" in defines:
            sim=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/"build/test/_deps/crosspoint_simulator-src"
            sim_board=(sim/"src/BoardConfig.h").read_text()
            selected="\n".join(re.search(r"enum class "+name+r"[^}]+};",sim_board).group() for name in ["DisplayController","TouchController"])
        cpp.write_text(code.replace("@ENUMS@",selected).replace("@PRIMARY@",",".join(json.dumps(name) for name in primary)))
        subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror",*("-D"+d for d in defines),"-I",str(ROOT/"src"),"-I",str(ui/"include"),str(cpp),"-o",str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
print("Actual About fixed-slot rows, original INX order/values, pinned primary order, HAL/SD states and UINT64 formatting pass C3/S3/simulator branches")
