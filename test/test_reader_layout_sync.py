"""Compare production geometry fragments to the two fixed sources; not whole-page acceptance."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
BASE="efee672b612c8e2319f27b4f2a2a861d2e627229"
UPSTREAM="622e5de9558399c92720e8f76deaed8a0cb1b6d3"

def source(path,ref=None):
    if ref:return subprocess.check_output(["git","show",ref+":"+path],cwd=ROOT,text=True)
    return (ROOT/path).read_text()

def margin(text):
    start=text.index("  int bottomReserve = statusBarHeight;") if "  int bottomReserve = statusBarHeight;" in text else text.index("  if (automaticPageTurnActive &&",text.index("const uint8_t statusBarHeight"))
    return text[start:text.index("  const uint16_t viewportWidth",start)]

def function(text,name):
    start=text.index(name);end=text.index("{",start)+1;depth=1
    while depth:
        depth+=(text[end]=="{")-(text[end]=="}");end+=1
    return text[start:end]

reader="src/activities/reader/EpubReaderActivity.cpp"
theme="src/components/themes/BaseTheme.cpp"
code=r"""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <Epub/ReaderRenderSpec.h>
struct CrossPointSettings{enum STATUS_BAR_PROGRESS_BAR{BOOK_PROGRESS,CHAPTER_PROGRESS};};
struct Rect{int x=0,y=0,width=0,height=0;bool operator==(const Rect&)const=default;};
Rect painted;
struct Renderer{
 enum Orientation{Portrait,LandscapeClockwise,PortraitInverted,LandscapeCounterClockwise};
 Orientation orientation=Portrait;
 int toNativeUiLength(int px)const{return px;}
 int getScreenWidth()const{return 640;}
 int getScreenHeight()const{return 960;}
 Orientation getOrientation()const{return orientation;}
 void fillRect(int x,int y,int width,int height,bool){painted={x,y,width,height};}
} renderer;
using GfxRenderer=Renderer;
struct UITheme {
 bool mainTabs=false;
 struct Metrics{int statusBarVerticalMargin=3;} metrics;
 static UITheme& getInstance(){static UITheme theme;return theme;}
 bool hasMainTabs()const{return mainTabs;}
 const Metrics& getMetrics()const{return metrics;}
 int getProgressBarHeight()const{return 2;}
 static int getStatusBarTextTopPadding(Renderer&){return 6;}
};
struct UiHighDpiProfile{static inline bool enabled=false;static constexpr int readerContentStatusGap=1;};
struct {bool textLane=false;bool textLaneVisible()const{return textLane;}} status;
struct {uint8_t screenMargin=5;auto statusBarSpec()const{return status;}} SETTINGS;
struct BoardConfig{static inline bool a4=false;static bool isEegoA4(){return a4;}};
struct StatusBar{int progressBarMode=CrossPointSettings::BOOK_PROGRESS;int progressBarHeightPx=2;bool showsProgressBar()const{return true;}};
"""
for name,ref in [("candidateMargin",None),("baselineMargin",BASE),("upstreamMargin",UPSTREAM)]:
    code+="int "+name+"(int statusBarHeight,bool automaticPageTurnActive){int orientedMarginBottom=16;\n"+margin(source(reader,ref))+"return orientedMarginBottom;}\n"
for name,ref in [("candidateBar",None),("baselineBar",BASE),("upstreamBar",UPSTREAM)]:
    text=source(theme,ref)
    start=text.index("  // Draw Progress Bar",text.index("void BaseTheme::drawStatusBar("))
    block=text[start:text.index("  // Draw Battery",start)]
    code+="Rect "+name+"(bool fillMargin,int corner,int height,float bookProgress){\n"
    code+="const int screenHeight=renderer.getScreenHeight(),orientedMarginLeft=10,orientedMarginRight=10,orientedMarginBottom=16,paddingBottom=0,currentPage=5,pageCount=10;StatusBar sb;sb.progressBarHeightPx=height;\n"
    # Some historical branches do not use both common geometry variables.
    code+="(void)screenHeight;(void)corner;\n"+block+"return painted;}\n"
old_signature=function(source("lib/Epub/Epub/ReaderRenderSpec.h",BASE),"inline uint32_t readerRenderSpecSignature(").replace("readerRenderSpecSignature(","oldLayoutSignature(",1)
code+=old_signature+"\n"
section=source("lib/Epub/Epub/Section.cpp")
start=section.index("#ifdef ENABLE_CHINESE_VERSION")
code+=section[start:section.index("// Written into the version",start)]
code+=re.search(r"constexpr uint8_t SECTION_FILE_PARTIAL_VERSION[^;]+;",section).group()+"\n"
toolbar=source("src/activities/reader/ReaderToolbarUi.cpp")
start=toolbar.index("    const int page = std::min(totalPages,")
end=toolbar.index("    fui::TextStyle pageStyle",start)
code+="int toolbarPage(int count,int pageRows,int top){int totalPages=(count+pageRows-1)/pageRows;struct{int top;}nav_{top};char buf[16];\n"+toolbar[start:end]+"return page;}\n"
code+=r"""
int main(){
 assert(SECTION_FILE_VERSION==EXPECTED_COMPLETE);
 assert(SECTION_FILE_PARTIAL_VERSION==0xfe - (EXPECTED_COMPLETE-28));
 ReaderRenderSpec spec;
 assert(readerRenderSpecSignature(spec)!=oldLayoutSignature(spec));
 for(bool inx:{false,true})for(bool dpi:{false,true})for(bool lane:{false,true})
 for(uint8_t margin:{uint8_t(5),uint8_t(25)})for(int bar:{0,2,20})for(bool autoTurn:{false,true}){
  UITheme::getInstance().mainTabs=inx;UiHighDpiProfile::enabled=dpi;status.textLane=lane;SETTINGS.screenMargin=margin;
  auto actual=candidateMargin(bar,autoTurn);
  assert(actual==(inx?baselineMargin(bar,autoTurn):upstreamMargin(bar,autoTurn)));
 }
 for(bool inx:{false,true})for(bool a4:{false,true})for(bool fill:{false,true})
 for(int rotation=0;rotation<4;++rotation)for(int radius:{0,16})for(int height:{1,4,16})for(float progress:{0.0f,50.0f,100.0f}){
  UITheme::getInstance().mainTabs=inx;BoardConfig::a4=a4;renderer.orientation=static_cast<Renderer::Orientation>(rotation);
  auto actual=candidateBar(fill,radius,height,progress);
  assert(actual==(inx?baselineBar(fill,radius,height,progress):upstreamBar(fill,radius,height,progress)));
 }
 for(int rows=1;rows<=12;++rows)for(int count=1;count<=100;++count){
  const int maximum=std::max(0,count-rows);
  for(int top=0;top<=maximum;++top){
   int page=toolbarPage(count,rows,top);
   assert(page>=1&&page<=(count+rows-1)/rows);
   if(top==maximum)assert(page==(count+rows-1)/rows);
  }
 }
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"layout.cpp";cpp.write_text(code)
    for chinese in [False,True]:
        binary=Path(directory)/("chinese" if chinese else "latin")
        flags=["-DENABLE_CHINESE_VERSION"] if chinese else []
        subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror","-I",str(ROOT/"lib/Epub"),"-DEXPECTED_COMPLETE="+str(91 if chinese else 90),*flags,str(cpp),"-o",str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
print("Production INX baseline and non-INX pinned-upstream footer/margins, toolbar pages, Latin/CJK cache versions and layout-signature invalidation pass")
