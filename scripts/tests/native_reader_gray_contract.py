"""Attach production Reader/Gfx/Epdiy submission checks to the existing native renderer harness."""
from pathlib import Path
import shutil,re
from test_reading_ui_regressions import method


def prepare(work, root):
    sdk=root/'freeink-sdk/libs/display/FreeInkDisplay'
    for folder in ['reader-driver/src/driver','reader-driver/src/bus','reader-driver/include']:
        (work/folder).mkdir(parents=True)
    for name in ['EpdiyLcdDriver.cpp','EpdiyLcdDriver.h','PanelDriver.h']:
        shutil.copy2(sdk/'src/driver'/name,work/'reader-driver/src/driver'/name)
    for name in ['GrayscaleCapabilities.h','RefreshContext.h']:
        shutil.copy2(sdk/'include'/name,work/'reader-driver/include'/name)
    shutil.copy2(sdk/'test/host/pro_stubs/EpdBus.h',work/'reader-driver/src/bus/EpdBus.h')
    (work/'esp_heap_caps.h').write_text('''#pragma once
#include <Logging.h>
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
inline void* heap_caps_malloc(size_t n,int){return std::malloc(n);}
inline void heap_caps_free(void* p){std::free(p);}
''')
    (work/'EpdiyLcd.h').write_text('''#pragma once
#include <Logging.h>
#include <Arduino.h>
struct EpdiyLcdConfig {};
enum class EpdiyLcdRefresh { Full,Half,Fast,TextTurn };
namespace readerHw {inline bool ok=true;inline int bases=0,gray=0,bw=0;inline uint8_t lsb=0,msb=0;}
inline bool epdiyLcdBegin(const EpdiyLcdConfig&,uint16_t,uint16_t,bool){return true;}
inline void epdiyLcdEnd(){}inline void epdiyLcdDeepSleep(){}
inline bool epdiyLcdDraw(const uint8_t*,EpdiyLcdRefresh,bool){++readerHw::bw;return readerHw::ok;}
inline void epdiyLcdStashBase(const uint8_t*){++readerHw::bases;}
inline bool epdiyLcdDrawGray(const uint8_t* lsb,const uint8_t* msb,EpdiyLcdRefresh,bool){++readerHw::gray;readerHw::lsb=lsb[0];readerHw::msb=msb[0];return readerHw::ok;}
inline uint8_t* epdiyLcdBeginGrayscale16(){return nullptr;}
inline bool epdiyLcdCommitGrayscale16(const uint8_t*){return false;}
inline void epdiyLcdCancelGrayscale16(){}
''')
    reader=(root/'src/activities/reader/EpubReaderActivity.cpp').read_text()
    header=(root/'src/activities/reader/EpubReaderActivity.h').read_text()
    private_pass=method(header,'  template <typename RenderPlane>')
    cycle=reader[reader.index('  bool committed = false;',reader.index('bool EpubReaderActivity::renderContents')):reader.index('  const auto t0 = millis();',reader.index('bool EpubReaderActivity::renderContents'))]
    snapshot_start=reader.index('  if (activityManager.isSwitchPending()) return false;',reader.index('  const auto tBwRender',reader.index('bool EpubReaderActivity::renderContents')))
    snapshot=reader[snapshot_start:reader.index('\n  if (absoluteImageGrayscale)',snapshot_start)]
    caller_start=reader.index('    const auto start = millis();\n    if (!renderContents(')
    caller=reader[caller_start:reader.index('    markPageRendered();',caller_start)+len('    markPageRendered();')]
    caller=re.sub(r'renderContents\(std::move\(p\),\s*orientedMarginTop,\s*orientedMarginRight,\s*orientedMarginBottom,\s*orientedMarginLeft\)', 'paint()', caller)
    save_start=reader.index('  if (currentSpineIndex != lastSavedSpineIndex',caller_start)
    save=method(reader[save_start:],'  if (currentSpineIndex != lastSavedSpineIndex')
    guard_start=reader.index('  if (legacyProgressPending) return false;',reader.index('bool EpubReaderActivity::saveProgress'))
    guard=reader[guard_start:reader.index('  int progressPercent',guard_start)]
    activity=(root/'src/activities/reader/ReaderActivity.cpp').read_text()
    shared=activity[activity.index('  if (renderCommitted) {',activity.index('void ReaderActivity::render(')):activity.index('\n}',activity.index('void ReaderActivity::render('))]
    shared=shared.replace('trustedtime::trustedNow()', '1003').replace('getProgressBasisPoints()', '900')
    utils=(root/'src/activities/reader/ReaderRefresh.h').read_text()
    refresh=method(utils,'inline HalDisplay::RefreshMode consumeRefreshMode(')+'\n'+method(utils,'inline void displayWithRefreshCycle(')
    (work/'ReaderGrayProbe.h').write_text('''#pragma once
#include <Logging.h>
#include "activities/reader/ReaderSession.h"
#include "reader-driver/src/driver/EpdiyLcdDriver.h"
namespace freeink { const EpdiyLcdConfig& readerContractConfig(){static EpdiyLcdConfig c;return c;} }
inline freeink::EpdiyLcdDriver& readerDriver(){static freeink::EpdiyLcdDriver d(freeink::readerContractConfig());return d;}
inline freeink::EpdBus readerBus;
inline bool readerDriverActive=false;
inline bool trackBwAllocations=false;
inline int failBwAllocation=-1;
inline size_t liveBwBytes=0,peakBwBytes=0;
inline void* trackedBw[16]{};
inline size_t trackedSizes[16]{};
#if defined(READER_TEST_WRAP_MALLOC)
extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(size_t size) {
 if(trackBwAllocations && (size==HalDisplay::BUFFER_SIZE || size==8000)) {
  if(failBwAllocation==0){failBwAllocation=-1;return nullptr;}
  if(failBwAllocation>0)--failBwAllocation;
  void* p=__real_malloc(size);
  if(p)for(int i=0;i<16;++i)if(!trackedBw[i]){trackedBw[i]=p;trackedSizes[i]=size;liveBwBytes+=size;peakBwBytes=std::max(peakBwBytes,liveBwBytes);break;}
  return p;
 }
 return __real_malloc(size);
}
extern "C" void __wrap_free(void* p) {
 if(p)for(int i=0;i<16;++i)if(trackedBw[i]==p){liveBwBytes-=trackedSizes[i];trackedBw[i]=nullptr;break;}
 __real_free(p);
}
#endif
namespace ReaderUtils {
'''+refresh+'''
}
struct ReaderGrayProbe {
 GfxRenderer& renderer;
 struct Manager {int cancelAt=-1,calls=0;bool pending=false;bool isSwitchPending(){if(calls++==cancelAt)pending=true;return pending;}} activityManager;
 int pagesUntilFullRefresh=3;bool forcedRefreshPending=true;
 bool legacyProgressPending=false;int footnoteDepth=0;
 struct Position {int spineIndex=-1,pageNumber=-1;} savedPositions[1];
 int currentSpineIndex=2,renderedSpineIndex_=-1,committedPageNumber_=-1;
 struct Section {int currentPage=9,pageCount=12;int estimatedTotalPages(){return pageCount;}} data;
 Section* section=&data;
 int lastSavedSpineIndex=-1,lastSavedPage=-1,lastSavedPageCount=-1;
 unsigned long lastRenderCompleteMs=0;
 int rendered=0,saves=0;bool renderCommitted=false;ReaderSession readerSession;
 void markPageRendered(){++rendered;renderCommitted=true;}
 bool saveProgress(int spineIndex,int currentPage,int) {
'''+guard+''' ++saves;return true;
 }
'''+private_pass+'''
 bool paint() {
'''+cycle+'''
 const bool needsAnyGrayscale=true,tiledGrayscale=false,use16LevelText=false;
 forcedRefreshPending=false;
'''+snapshot+'''
 renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
 --pagesUntilFullRefresh;
 committed=renderWholePlaneGrayscale([&](bool lsb){memset(renderer.getFrameBuffer(),lsb?0x11:0x22,renderer.getBufferSize());});
 return committed;
 }
 void renderBook(){
'''+caller+'\n'+save+'''
 }
 void render() {renderCommitted=false;renderBook();
'''+shared+'''
 }
};
inline void testReaderGrayContract(GfxRenderer& renderer) {
 readerDriverActive=true;
 BoardConfig::ACTIVE.displayWidth=HalDisplay::DISPLAY_WIDTH;BoardConfig::ACTIVE.displayHeight=HalDisplay::DISPLAY_HEIGHT;
 readerDriver().begin(readerBus);
 for(int cancel=-1;cancel<6;++cancel)for(bool fail:{false,true}) {
  memset(renderer.getFrameBuffer(),0xA5,renderer.getBufferSize());
  ReaderGrayProbe probe{renderer};probe.activityManager.cancelAt=cancel;
  readerHw::ok=!fail;readerHw::bases=readerHw::gray=readerHw::bw=0;
  probe.readerSession.onRenderComplete(uint32_t(millis()-3000),1000,0);probe.readerSession.noteTurn(true,true);
  trackBwAllocations=true;probe.render();trackBwAllocations=false;
  assert(renderer.getRenderMode()==GfxRenderer::BW && liveBwBytes==0);
  const bool committed=cancel==-1&&!fail;
  assert(probe.saves==int(committed)&&probe.rendered==int(committed));
  assert(probe.readerSession.durationSeconds()==unsigned(committed?3:0));
  assert(probe.pagesUntilFullRefresh==(committed?2:3));
  assert(probe.forcedRefreshPending==!committed);
  if(cancel!=-1)assert(readerHw::gray==0);
  if(committed)assert(readerHw::gray==1&&readerHw::lsb==0x11&&readerHw::msb==0x22);
  for(size_t i=0;i<renderer.getBufferSize();++i)assert(renderer.getFrameBuffer()[i]==0xA5);
 }
#if defined(READER_TEST_WRAP_MALLOC)
 for(bool fail:{false,true}) {
  ReaderGrayProbe probe{renderer};readerHw::ok=!fail;readerHw::bases=readerHw::gray=readerHw::bw=0;
  failBwAllocation=0;probe.readerSession.onRenderComplete(uint32_t(millis()-3000),1000,0);probe.readerSession.noteTurn(true,true);
  trackBwAllocations=true;probe.render();trackBwAllocations=false;
  assert(readerHw::bases==0&&readerHw::gray==0&&readerHw::bw==1);
  assert(probe.saves==int(!fail)&&liveBwBytes==0&&renderer.getRenderMode()==GfxRenderer::BW);
 }
#endif
 ReaderGrayProbe pending{renderer};assert(!pending.saveProgress(2,9,12));assert(pending.saves==0);
 pending.renderedSpineIndex_=2;pending.committedPageNumber_=8;assert(!pending.saveProgress(2,9,12));
 assert(pending.saveProgress(2,8,12));
 readerDriver().deepSleep(readerBus);readerDriverActive=false;
 printf("Reader/Gfx/Epdiy prepare, commit failure, all cancellation stages, B/W OOM fallback and progress gate passed; B/W peak=%zu, retained=%zu\\n",peakBwBytes,liveBwBytes);
}
''')
    return work/'reader-driver/src/driver/EpdiyLcdDriver.cpp'
