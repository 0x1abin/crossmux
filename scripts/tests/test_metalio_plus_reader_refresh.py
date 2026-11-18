"""Execute actual reader cadence for Plus and existing target policies."""
from pathlib import Path
import subprocess, tempfile, os
ROOT=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='plus-reader-cadence-')as tmp:
 p=Path(tmp)
 (p/'CrossPointSettings.h').write_text('inline struct { int getRefreshFrequency()const{return 3;} } SETTINGS;\n')
 (p/'GfxRenderer.h').write_text(r"""#pragma once
#include <DisplayRefreshContext.h>
struct HalDisplay{enum RefreshMode{FAST_REFRESH,HALF_REFRESH,FULL_REFRESH};};
struct GfxRenderer{
 mutable HalDisplay::RefreshMode last=HalDisplay::FAST_REFRESH;
 bool continuous=false;
 bool combined=false;
 mutable int grayCalls=0;
 bool nativeCycle=false,nativeOk=true;
 mutable int nativeLegacyCalls=0,nativeModeCalls=0;
 bool supportsNativeGrayscaleRefreshCycle()const{return nativeCycle;}
 bool commitGrayscale16()const{++nativeLegacyCalls;return nativeOk;}
 bool commitGrayscale16(HalDisplay::RefreshMode m)const{last=m;++nativeModeCalls;return nativeOk;}
 bool supportsContinuousImageReading()const{return continuous;}
 bool supportsTextOnlyCombinedBase()const{return combined;}
 bool supportsReaderTransitions()const{return false;}
 bool canUseTextTransition()const{return false;}
 void waitRefreshComplete()const{}
 void displayBuffer(HalDisplay::RefreshMode m,DisplayRefreshContext)const{last=m;}
 void displayBufferAsync(HalDisplay::RefreshMode m,DisplayRefreshContext)const{last=m;}
 void displayGrayscaleBase(HalDisplay::RefreshMode m,DisplayRefreshContext c)const{
  if(c!=DisplayRefreshContext::TextOnlyAntiAliasing) __builtin_abort();
  last=m;++grayCalls;
 }
};
""")
 (p/'test.cpp').write_text(r"""#include <cassert>
#include "ReaderRefresh.h"
int main(){
 GfxRenderer r;int due=1;
#if FREEINK_DEVICE_METALIO_EINK4_PLUS || FREEINK_DEVICE_READPICO
 const auto clean=HalDisplay::FULL_REFRESH;
#else
 const auto clean=HalDisplay::HALF_REFRESH;
#endif
 for(int page=0;page<7;++page){
  ReaderUtils::displayWithRefreshCycle(r,due,page%2==0);
  assert(r.last==(page%3==0?clean:HalDisplay::FAST_REFRESH));
 }
 due=1;assert(ReaderUtils::consumeRefreshMode(due,true)==HalDisplay::FAST_REFRESH&&due==1);
 assert(ReaderUtils::consumeRefreshMode(due)==clean&&due==3);
 // Actual shared image helper, with continuous capability false on Plus.
 due=1;
 for(int page=0;page<7;++page){
  ReaderUtils::displayImageWithRefreshCycle(r,due,true);
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
  assert(r.last==(page%3==0?HalDisplay::FULL_REFRESH:HalDisplay::FAST_REFRESH));
#else
  assert(r.last==HalDisplay::HALF_REFRESH&&due==1);
#endif
 }
 r.continuous=true;due=1;
 for(int page=0;page<7;++page){
  ReaderUtils::displayImageWithRefreshCycle(r,due,true);
  assert(r.last==(page%3==0?clean:HalDisplay::FAST_REFRESH));
 }
 r.combined=true;due=1;
 for(int page=0;page<7;++page){
  ReaderUtils::displayBaseWithRefreshCycle(r,due,page%3==0);
#if FREEINK_DEVICE_READPICO
  assert(r.last==(page%3==0?clean:HalDisplay::HALF_REFRESH));
#else
  assert(r.last==(page%3==0?clean:HalDisplay::FAST_REFRESH));
#endif
 }
 assert(r.grayCalls==7 && due==3);
 r.nativeCycle=true;due=1;
 for(int page=0;page<7;++page){
  assert(ReaderUtils::commitNativeWithRefreshCycle(r,due,false));
  assert(r.last==(page%3==0?clean:HalDisplay::FAST_REFRESH));
 }
 assert(r.nativeModeCalls==7 && r.nativeLegacyCalls==0);
 due=2;r.nativeOk=false;
 assert(!ReaderUtils::commitNativeWithRefreshCycle(r,due,true)&&due==1&&r.last==clean);
 r.nativeOk=true;
 assert(ReaderUtils::commitNativeWithRefreshCycle(r,due,false)&&due==3&&r.last==clean);
 r.nativeCycle=false;due=5;
 assert(ReaderUtils::commitNativeWithRefreshCycle(r,due,false)&&due==1&&r.nativeLegacyCalls==1);

}
""")
 for flag in ['FREEINK_DEVICE_METALIO_EINK4_PLUS','FREEINK_DEVICE_READPICO','FREEINK_DEVICE_X3']:
  subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
   '-D'+flag+'=1','-I'+str(p),'-I'+str(ROOT/'src/activities/reader'),'-I'+str(ROOT/'lib/hal'),
   str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
  print(flag+' cadence passed')
