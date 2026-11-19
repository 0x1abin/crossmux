"""Exercise production snapshot ownership and final Home painting, not optical acceptance."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def method(source, signature):
    start=source.index(signature)
    end=source.index("{",start)+1
    depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}")
        end+=1
    return source[start:end]

source=(ROOT/"src/activities/home/HomeActivity.cpp").read_text()
render=method(source,"void HomeActivity::render(")
grid=method(render,"  if (coverGridUi) {")
polarity=render[render.index("  if (coverBufferStored && coverBufferInverted"):render.index("  const CarouselUpdateScope updateScope")]
assert render.index("coverBufferInverted !=") < render.index("const bool menuOnlyUpdate")
code=r"""
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
bool allocationOk=true;
unsigned allocations=0;
template<class T>auto makeUniqueNoThrow(size_t size){++allocations;return allocationOk?std::make_unique<T>(size):nullptr;}
#define LOG_ERR(...) ((void)0)
#define tr(x) (x)
constexpr const char* STR_SELECT="select";
constexpr const char* STR_DIR_LEFT="left";
constexpr const char* STR_DIR_RIGHT="right";
struct HalDisplay{enum RefreshMode{HALF_REFRESH,FAST_REFRESH};};
struct Renderer {
 bool inverted=false,copyOk=true;
 unsigned clears=0,refreshes=0,restores=0;
 HalDisplay::RefreshMode last=HalDisplay::FAST_REFRESH;
 size_t getRegionByteSize(int,int,int width,int height){return width*height/8;}
 bool isInverted()const{return inverted;}
 bool copyRegionToBuffer(int,int,int,int,uint8_t* data,size_t size){if(size)data[0]=inverted?0xaa:0x55;return copyOk;}
 bool copyBufferToRegion(int,int,int,int,const uint8_t*,size_t){++restores;return copyOk;}
 void clearScreen(){++clears;}
 void displayBuffer(HalDisplay::RefreshMode mode){++refreshes;last=mode;}
};
struct CoverGridHomeUi {
 static constexpr int THUMB_HEIGHT=400;
 unsigned changes=0,checks=0,pathRefreshes=0;
 bool takeThumbHeightChanged(){return checks++<changes;}
 void refreshCoverPaths(){++pathRefreshes;}
 void setSelection(int){}
};
struct UITheme {
 static UITheme& getInstance(){static UITheme theme;return theme;}
 void drawCoverGridHome(CoverGridHomeUi&){}
 void drawButtonHints(Renderer&,const char*,const char*,const char*,const char*){}
};
#define GUI UITheme::getInstance()
struct HomeActivity {
 enum class CarouselUpdateScope{None,MenuOnly,Full};
 Renderer renderer;
 std::unique_ptr<uint8_t[]> coverBuffer;
 size_t coverBufferSize=0;
 int coverRectX=0,coverRectY=0,coverRectW=64,coverRectH=16;
 bool coverBufferStored=false,coverBufferUnavailable=false,coverRendered=false,coverBufferInverted=false;
 bool recentsLoaded=false,recentsLoading=false,firstRenderDone=false;
 int selectorIndex=0;unsigned loads=0,updates=0;
 CarouselUpdateScope scope=CarouselUpdateScope::None;
 std::unique_ptr<CoverGridHomeUi> coverGridUi;
 struct {
  struct Labels{const char* btn1;const char* btn2;const char* btn3;const char* btn4;};
  Labels mapLabels(const char* a,const char* b,const char* c,const char* d){return {a,b,c,d};}
 } mappedInput;
 void requestCarouselUpdate(CarouselUpdateScope next){scope=next;}
 void loadRecentCovers(int height){assert(height==CoverGridHomeUi::THUMB_HEIGHT);++loads;recentsLoaded=true;}
 void requestUpdate(){++updates;}
 bool storeCoverBuffer();bool restoreCoverBuffer();void freeCoverBuffer();
 void checkPolarity(){
"""+polarity+r"""
 }
 void paintGrid(){const char* backLabel="standby";
"""+grid+r"""
 }
};
"""+"\n".join(method(source,name) for name in ["bool HomeActivity::storeCoverBuffer()","bool HomeActivity::restoreCoverBuffer()","void HomeActivity::freeCoverBuffer()"])+r"""
int main(){
 HomeActivity home;
 allocations=0;
 assert(!home.storeCoverBuffer()&&allocations==0);
 home.recentsLoaded=true;
 assert(home.storeCoverBuffer()&&allocations==1&&home.coverBufferSize==128);
 assert(home.restoreCoverBuffer()&&home.renderer.restores==1);
 assert(home.storeCoverBuffer()&&allocations==1);
 home.coverBufferStored=home.coverRendered=true;
 home.scope=HomeActivity::CarouselUpdateScope::MenuOnly;
 home.renderer.inverted=true;
 assert(!home.restoreCoverBuffer()&&home.renderer.restores==1);
 home.checkPolarity();
 assert(!home.coverBuffer&&!home.coverBufferStored&&!home.coverRendered);
 assert(home.scope==HomeActivity::CarouselUpdateScope::Full);
 assert(home.storeCoverBuffer()&&home.coverBufferInverted);
 home.freeCoverBuffer();assert(!home.coverBuffer&&home.coverBufferSize==0);
 allocationOk=false;unsigned before=allocations;
 assert(!home.storeCoverBuffer()&&home.coverBufferUnavailable&&allocations==before+1);
 assert(!home.storeCoverBuffer()&&allocations==before+1);
 home.freeCoverBuffer();allocationOk=true;
 assert(home.storeCoverBuffer());
 home.renderer.copyOk=false;assert(!home.storeCoverBuffer());
 for(unsigned changes:{0u,1u,2u})for(bool loaded:{false,true})for(bool first:{false,true}){
  HomeActivity a;a.coverGridUi=std::make_unique<CoverGridHomeUi>();
  a.coverGridUi->changes=changes;a.recentsLoaded=loaded;a.firstRenderDone=first;
  a.paintGrid();
  assert(a.renderer.refreshes==1&&a.firstRenderDone);
  assert(a.renderer.last==(first?HalDisplay::FAST_REFRESH:HalDisplay::HALF_REFRESH));
  assert(a.loads==(!loaded||changes>0));
  assert(a.updates==(changes>1));
 }
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"home.cpp";binary=Path(directory)/"home"
    cpp.write_text(code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror",str(cpp),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Production Home region reuse/OOM/release/polarity and one final grid paint pass; loading popups and physical waveforms are separate")
