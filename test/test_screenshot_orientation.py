"""Decode actual screenshot BMPs and exercise failure/Avatar restore paths."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
def method(source,name):
    start=source.index(name);end=source.index("{",start)+1;depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[start:end]
screenshot=(ROOT/"src/util/ScreenshotUtil.cpp").read_text()
bitmap=(ROOT/"lib/GfxRenderer/Bitmap.h").read_text()
header=bitmap[bitmap.index("#pragma pack(push, 1)"):bitmap.index("enum class BmpReaderError")]
helper=method((ROOT/"lib/GfxRenderer/BitmapHelpers.cpp").read_text(),"void createBmpHeader(")
rotate=method((ROOT/"lib/GfxRenderer/GfxRenderer.cpp").read_text(),"static inline void rotateCoordinates(")
code=r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
struct HalDisplay{enum RefreshMode{FAST_REFRESH,HALF_REFRESH};};
unsigned long millis(){return 99;}void delay(int){}
struct GfxRenderer{
 enum Orientation{Portrait,LandscapeClockwise,PortraitInverted,LandscapeCounterClockwise};
 int panelWidth=800,panelHeight=480,stride=100;Orientation orientation=Portrait;
 std::vector<uint8_t> bytes,backup;bool missing=false,storeOk=true;int stores=0,restores=0;
 const uint8_t*getFrameBuffer()const{return missing?nullptr:bytes.data();}
 int getDisplayWidth()const{return panelWidth;}int getDisplayHeight()const{return panelHeight;}
 int getDisplayWidthBytes()const{return stride;}
 Orientation getOrientation()const{return orientation;}
 int getScreenWidth()const{return orientation==Portrait||orientation==PortraitInverted?panelHeight:panelWidth;}
 int getScreenHeight()const{return orientation==Portrait||orientation==PortraitInverted?panelWidth:panelHeight;}
 bool storeBwBuffer(){++stores;if(!storeOk)return false;backup=bytes;return true;}
 void restoreBwBuffer(){++restores;bytes=backup;backup.clear();}
 void clearScreen(){std::fill(bytes.begin(),bytes.end(),255);}
 void drawRect(int,int,int,int,int,bool){bytes[0]^=0xff;}
 void displayBuffer(HalDisplay::RefreshMode=HalDisplay::FAST_REFRESH){}
};
struct HalFile;
struct StorageFixture {
 std::map<std::string,std::vector<uint8_t>> files;
 bool openFail=false,closeFail=false,mkdirFail=false;size_t writeBudget=SIZE_MAX;int opened=0,closed=0;
 bool exists(const char*)const{return false;}bool mkdir(const char*)const{return !mkdirFail;}
 bool openFileForWrite(const char*,const char*,HalFile&);
 bool remove(const char*p){assert(opened==closed);files.erase(p);return true;}
}Storage;
struct HalFile{
 std::string path;bool open=false;
 size_t write(const uint8_t*b,size_t n){size_t count=std::min(n,Storage.writeBudget);Storage.writeBudget-=count;auto&v=Storage.files[path];v.insert(v.end(),b,b+count);return count;}
 bool close(){if(open){++Storage.closed;open=false;}return !Storage.closeFail;}
 ~HalFile(){if(open)close();}
};
bool StorageFixture::openFileForWrite(const char*,const char*p,HalFile&f){if(openFail)return false;++opened;files[p].clear();f.path=p;f.open=true;return true;}
"""+header+r"""
enum class BmpRowOrder{BottomUp,TopDown};
"""+helper+"\n"+rotate+r"""
class ScreenshotUtil{public:static bool saveFramebufferAsBmp(const char*,const GfxRenderer&);};
"""+method(screenshot,"bool ScreenshotUtil::saveFramebufferAsBmp(")+r"""
namespace avatar{
 struct ScreenRect{int x,y,width,height;};
 void drawAvatar(GfxRenderer&r,int,ScreenRect){std::fill(r.bytes.begin(),r.bytes.end(),0);}
}
struct UglyAvatarActivity{
 GfxRenderer renderer;int value=1;int*data_=&value;void onSave();
};
"""+method((ROOT/"src/activities/apps/avatar/UglyAvatarActivity.cpp").read_text(),"void UglyAvatarActivity::onSave()")+r"""
int pattern(int x,int y){return (x*7+y*11+(x>>3)*5+(y>>4)*3)%17<5;}
void prepare(GfxRenderer&r,int pw,int ph,GfxRenderer::Orientation o,int padding=0){
 r.panelWidth=pw;r.panelHeight=ph;r.stride=(pw+7)/8+padding;r.orientation=o;r.bytes.assign(r.stride*ph,0xFF);
 for(int y=0;y<ph;++y)for(int x=0;x<pw;++x)if(!pattern(x,y))r.bytes[y*r.stride+x/8]&=~(1u<<(7-(x&7)));
}
int main(){
 for(auto [pw,ph]:{std::pair{800,480},std::pair{792,528},std::pair{768,552},std::pair{1216,684},std::pair{113,67},std::pair{2048,31}})
  for(auto o:{GfxRenderer::Portrait,GfxRenderer::PortraitInverted,GfxRenderer::LandscapeClockwise,GfxRenderer::LandscapeCounterClockwise}){
   GfxRenderer r;prepare(r,pw,ph,o,3);auto before=r.bytes;Storage={};
   assert(ScreenshotUtil::saveFramebufferAsBmp("/screenshots/test.bmp",r));
   assert(r.bytes==before&&r.stores==0&&r.restores==0&&Storage.opened==Storage.closed);
   const auto&output=Storage.files.at("/screenshots/test.bmp");BmpHeader h;memcpy(&h,output.data(),sizeof(h));
   const int w=r.getScreenWidth(),height=r.getScreenHeight(),rowBytes=(w+31)/32*4;
   assert(h.fileHeader.bfType==0x4d42&&h.infoHeader.biBitCount==1&&h.infoHeader.biWidth==w&&h.infoHeader.biHeight==height);
   assert(h.fileHeader.bfSize==output.size()&&output.size()==sizeof(h)+rowBytes*height);
   for(int y=0;y<height;++y)for(int x=0;x<rowBytes*8;++x){
    const int actual=(output[sizeof(h)+(height-1-y)*rowBytes+x/8]>>(7-(x&7)))&1;
    if(x>=w){assert(actual==0);continue;}int nx,ny;rotateCoordinates(o,x,y,&nx,&ny,pw,ph);assert(actual==pattern(nx,ny));
   }
  }
 for(int fault=0;fault<5;++fault){
  GfxRenderer r;prepare(r,800,480,GfxRenderer::Portrait);const auto before=r.bytes;Storage={};
  Storage.openFail=fault==0;Storage.writeBudget=fault==1?10:fault==2?sizeof(BmpHeader)+7:SIZE_MAX;
  Storage.closeFail=fault==3;Storage.mkdirFail=fault==4;
  assert(!ScreenshotUtil::saveFramebufferAsBmp("/screenshots/test.bmp",r));
  assert(Storage.files.empty()&&Storage.opened==Storage.closed&&r.bytes==before);
 }
 GfxRenderer invalid;prepare(invalid,2049,10,GfxRenderer::LandscapeCounterClockwise);Storage={};
 assert(!ScreenshotUtil::saveFramebufferAsBmp("/test.bmp",invalid)&&Storage.opened==0);
 invalid.missing=true;assert(!ScreenshotUtil::saveFramebufferAsBmp("/test.bmp",invalid));
 for(int fault=0;fault<5;++fault){
  UglyAvatarActivity a;prepare(a.renderer,800,480,GfxRenderer::Portrait);const auto before=a.renderer.bytes;Storage={};
  a.renderer.storeOk=fault!=1;Storage.openFail=fault==2;Storage.writeBudget=fault==3?10:SIZE_MAX;Storage.closeFail=fault==4;
  a.onSave();assert(a.renderer.bytes==before&&a.renderer.stores==1&&a.renderer.restores==(fault==1?0:1));
  if(fault)assert(Storage.files.empty());else assert(Storage.files.contains("/avatars/avatar-99.bmp"));
 }
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp=Path(directory)/"screenshot.cpp";binary=Path(directory)/"screenshot";cpp.write_text(code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror",str(cpp),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Actual BMP header/row decoding across four rotations, six panel sizes, padded stride, write/close errors and Avatar original-buffer restoration pass")
