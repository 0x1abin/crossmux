"""Compile the production cache and render-task loop against small host seams."""
from pathlib import Path
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]
READER = ROOT / 'src/activities/reader/EpubReaderActivity.cpp'


class ReaderPageCacheTest(unittest.TestCase):
    def test_cache_identity_cancellation_and_lifetime(self):
        source = READER.read_text()
        functions = '\n'.join(method(source, name) for name in (
            'bool EpubReaderActivity::pageCacheEligible(',
            'ReaderPageCacheKey EpubReaderActivity::pageCacheKey(',
            'void EpubReaderActivity::freePageCache(',
            'void EpubReaderActivity::renderIdle(',
            'bool EpubReaderActivity::buildPageCacheSlot(',
        ))
        begin = source.index('  const auto key = pageCacheKey(section ?')
        end = source.index('\n#else', begin)
        consume = source[begin:end]
        begin = source.index('  const uint32_t missingCodepoint =', source.index('  unsigned long cacheBaseMs'))
        missing_glyph = source[begin:source.index('\n#else', begin)]
        program = r'''
#include <array>
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>
#include "ReaderPageCache.h"
#include "CancelCheck.h"
template<class F> struct ScopedCleanup {F fn;~ScopedCleanup(){fn();}};
struct ImageBlock {static void releaseRenderCache() {}};
int imageWarms=0;bool cancelImage=false;
#define ENABLE_CHINESE_VERSION 1
template<class... Args> void log(const char*,const char*,Args...) {}
#define LOG_ERR(...) log(__VA_ARGS__)
#define LOG_DBG(...) log(__VA_ARGS__)
unsigned long millis() { return 0; }
struct Manager {
  bool cancelled=false, switching=false;
  bool isSwitchPending() const {return switching;}
  bool idleRenderCancelled(uint32_t) const {return cancelled || switching;}
} activityManager;
struct Settings {
  uint8_t textAntiAliasing=1, readingBackgroundEnabled=0, orientation=0, fakeBold=0;
  uint8_t readingGuideLineEnabled=0, readingGuideLineStyle=0;
  int8_t readingGuideLineOffset=0;
  ReaderRenderSpec spec;
} SETTINGS;
namespace memory {
int allocations=0, live=0, failAt=0;
bool headroom=true;
size_t chargedBytes=0;
struct Free { void operator()(uint8_t* p) const {delete[] p; --live;} };
using ByteBuffer=std::unique_ptr<uint8_t,Free>;
bool psramHasHeadroom(size_t total,size_t,size_t) {chargedBytes=total; return headroom;}
ByteBuffer makePsramByteBufferUninitializedNoThrow(size_t n) {
  if (++allocations == failAt) return {};
  ++live; return ByteBuffer(new uint8_t[n]);
}
}
struct FontCacheManager {
  int clears=0;
  uint32_t missing=0;
  void clearCache() {++clears;}
  void resetStats() {}
  uint32_t consumeMissingChineseCodepoint() {auto value=missing; missing=0; return value;}
};
struct GfxRenderer {
  enum RenderMode {BW, GRAYSCALE_LSB, GRAYSCALE_MSB};
  RenderMode mode=BW;
  std::array<uint8_t,16> frame;
  FontCacheManager fonts;
  bool inverted=false;
  GfxRenderer() {frame.fill(0xA5);}
  bool hasFrameBuffer() const {return true;}
  bool isInverted() const {return inverted;}
  bool supportsStripGrayscale() const {return false;}
  bool supportsTextOnlyCombinedBase() const {return true;}
  int getScreenWidth() const {return 16;}
  int getScreenHeight() const {return 8;}
  size_t getBufferSize() const {return frame.size();}
  uint8_t* getFrameBuffer() {return frame.data();}
  FontCacheManager* getFontCacheManager() {return &fonts;}
  RenderMode getRenderMode() const {return mode;}
  void setRenderMode(RenderMode m) {mode=m;}
  int getLineHeight(int,float) const {return 1;}
  int getFontAscenderSize(int) const {return 1;}
  struct FrameBufferLoan {explicit FrameBufferLoan(GfxRenderer&) {}};
  struct SyntheticBoldScope {SyntheticBoldScope(GfxRenderer&,uint8_t) {}};
};
constexpr int TAG_PageLine=1;
int renders=0, cancelAt=0;
struct Block {bool isEmpty() const {return false;} int getRubyShift(int) const {return 0;}};
struct PageLine {
  int yPos=0;
  Block block;
  uint32_t missing=0;
  int getTag() const {return TAG_PageLine;}
  const Block* getBlock() const {return &block;}
  void render(GfxRenderer& renderer,int,int,int) {
    renderer.frame[yPos]=static_cast<uint8_t>(renderer.mode+1);
    renderer.fonts.missing=missing;
    if (++renders == cancelAt) activityManager.cancelled=true;
  }
};
namespace readingGuideLine {
bool fitsVertically(uint8_t,int,int,int) {return true;}
void draw(GfxRenderer& renderer,int,int,int,uint8_t) {renderer.frame[15]=0x55;}
}
struct Page {
  bool images=false;
  std::vector<std::unique_ptr<PageLine>> elements;
  Page() {for(int i=0;i<3;++i) {auto p=std::make_unique<PageLine>(); p->yPos=i; elements.push_back(std::move(p));}}
  bool hasImages() const {return images;}
  bool hasImagesNeedingDecode() const {return images;}
  bool warmImages(GfxRenderer& r,int,int,CancelCheck c) {
    ++imageWarms;r.frame.fill(0x11);r.mode=GfxRenderer::GRAYSCALE_MSB;
    if(cancelImage) activityManager.cancelled=true;
    return !c.isCancelled();
  }
};
struct Section {
  int currentPage=4, pageCount=20, loads=0;
  bool building=false, images=false, fail=false, cancelOnLoad=false;
  bool isBuilding() const {return building;}
  std::unique_ptr<Page> loadPage(int index) {
    ++loads;
    if(cancelOnLoad) activityManager.cancelled=true;
    if(fail) return {};
    auto p=std::make_unique<Page>(); p->images=images;
    for(auto& line:p->elements) line->missing=0x4E00+index;
    return p;
  }
};
struct EpubReaderActivity {
  GfxRenderer renderer;
  std::unique_ptr<Section> section=std::make_unique<Section>();
  static constexpr int kPageCacheSlots=2;
  memory::ByteBuffer pageCacheBase_[kPageCacheSlots],pageCacheLsb_[kPageCacheSlots],
      pageCacheMsb_[kPageCacheSlots],pageCacheStash_[kPageCacheSlots];
  ReaderPageCache pageCache_[kPageCacheSlots];
  int pageCacheLiveSlot_=0;
  ReaderPageCacheKey renderedPageKey_;
  uint32_t sectionGeneration_=1,renderEpoch_=1,pageCacheMissingCodepoint_[kPageCacheSlots]={};
  int currentSpineIndex=2;
  bool pageCacheFailed_=false;
  enum class Overlay {None,Menu}; Overlay overlay=Overlay::None;
  enum class FontPromptState {Idle,Asking}; FontPromptState fontPromptState=FontPromptState::Idle;
  ReaderRenderSpec effectiveRenderSpec(uint16_t w,uint16_t h) const {
    auto spec=SETTINGS.spec; spec.viewportWidth=w; spec.viewportHeight=h; return spec;
  }
  bool pageCacheEligible() const;
  ReaderPageCacheKey pageCacheKey(int,int,int,int,int) const;
  void freePageCache();
  void renderIdle(uint32_t);
  bool buildPageCacheSlot(int,const ReaderPageCacheKey&,uint32_t);
  EpubReaderActivity() {renderedPageKey_=pageCacheKey(section->currentPage,1,1,1,1);}
  uint32_t missingGlyph(bool pageCacheHit) {
    auto* fcm=renderer.getFontCacheManager();
''' + missing_glyph + r'''
    return missingCodepoint;
  }
  bool consume(int orientedMarginTop=1,int orientedMarginRight=1,int orientedMarginBottom=1,int orientedMarginLeft=1,
               int direction=1) {
    section->currentPage+=direction;
''' + consume + r'''
    return pageCacheHit;
  }
};
''' + functions + r'''
void reset() {
  assert(memory::live==0);
  memory::allocations=0; memory::failAt=0; memory::headroom=true;
  activityManager={}; SETTINGS={}; renders=0; cancelAt=0;
}
int main() {
  reset();
  {
    EpubReaderActivity r;
    const auto original=r.renderer.frame;
    r.renderIdle(0);
    for(const auto& slot:r.pageCache_) assert(slot.state==ReaderPageCache::State::Ready);
    assert(r.pageCache_[1].key.page==r.section->currentPage-1);
    assert(r.renderer.frame==original && r.renderer.mode==GfxRenderer::BW);
    assert(r.pageCacheMissingCodepoint_[0]==0x4E05 && r.pageCacheMissingCodepoint_[1]==0x4E03);
    assert(memory::live==8 && memory::allocations==8);
    for(int slot=0;slot<r.kPageCacheSlots;++slot)
      assert(r.pageCacheBase_[slot].get()[0]==1 && r.pageCacheLsb_[slot].get()[0]==2 && r.pageCacheMsb_[slot].get()[0]==3);
    r.renderIdle(0); assert(r.section->loads==2);
    assert(r.consume() && r.pageCacheLiveSlot_==0 && r.missingGlyph(true)==0x4E05);
    r.renderIdle(0); assert(memory::allocations==8);
    assert(memory::chargedBytes==4*r.renderer.getBufferSize());
    assert(r.consume(1,1,1,1,-1) && r.pageCacheLiveSlot_==1 && r.missingGlyph(true)==0x4E04);
    r.freePageCache(); assert(memory::live==0);
    for(const auto& slot:r.pageCache_) assert(slot.state==ReaderPageCache::State::Empty);
  }
  reset();
  {EpubReaderActivity r; r.renderIdle(0); assert(r.consume(1,1,1,1,-1) && r.pageCacheLiveSlot_==1);}
  // Every key field matters, including every resolved layout setting.
  reset();
  {
    EpubReaderActivity r; r.renderIdle(0);
    const auto key=r.pageCache_[0].key;
    auto changed=key;
#define CHANGE(field,value) changed=key; changed.field=value; assert(!r.pageCache_[0].ready(changed))
    CHANGE(top,2); CHANGE(right,2); CHANGE(bottom,2); CHANGE(left,2);
    CHANGE(page,0); CHANGE(spine,0); CHANGE(sectionGeneration,2); CHANGE(renderEpoch,2);
    CHANGE(orientation,1); CHANGE(fakeBold,1); CHANGE(antiAliasing,false); CHANGE(inverted,true);
    CHANGE(background,true); CHANGE(guideLine,true); CHANGE(guideStyle,1); CHANGE(guideOffset,1);
    CHANGE(spec.fontId,2); CHANGE(spec.lineCompression,2); CHANGE(spec.extraParagraphSpacing,1);
    CHANGE(spec.firstLineIndent,2); CHANGE(spec.characterSpacing,1); CHANGE(spec.wordSpacingPercent,120);
    CHANGE(spec.paragraphAlignment,2); CHANGE(spec.viewportWidth,2); CHANGE(spec.viewportHeight,2);
    CHANGE(spec.hyphenationEnabled,true); CHANGE(spec.embeddedStyle,false); CHANGE(spec.imageRendering,2);
    CHANGE(spec.focusReadingEnabled,true); CHANGE(spec.collectTouchLinks,true);
#undef CHANGE
    // This failed before the fix: consumption overwrote all cached margins first.
    assert(!r.consume(2,2,2,2));
  }
  reset();
  {EpubReaderActivity r; r.renderIdle(0); ++r.sectionGeneration_; assert(!r.consume());}
  reset();
  {EpubReaderActivity r; r.renderIdle(0); SETTINGS.spec.fontId=99; assert(!r.consume());}
  // Images use one stash, not three unused result planes; failures allocate nothing.
  for(bool fail: {false,true}) {
    reset(); EpubReaderActivity r; r.section->images=!fail; r.section->fail=fail;
    for(int i=0;i<100;++i) r.renderIdle(0);
    assert(r.section->loads==2 && memory::allocations==(fail?0:1));
    for(const auto& slot:r.pageCache_) assert(slot.state==ReaderPageCache::State::Skipped);
    r.section->images=false; r.section->fail=false;
    assert(!r.consume()); r.renderIdle(0); assert(r.section->loads==4);
  }
  reset();
  {EpubReaderActivity r; r.section->images=true;auto original=r.renderer.frame;
   imageWarms=0;cancelImage=true;r.renderIdle(0);cancelImage=false;
   assert(imageWarms==1 && r.renderer.frame==original && r.renderer.mode==GfxRenderer::BW);
   assert(memory::live==1 && !r.pageCacheFailed_ && !r.pageCacheBase_[0]);
   r.freePageCache();assert(memory::live==0);}
  reset();
  {EpubReaderActivity r;r.section->images=true;memory::headroom=false;r.renderIdle(0);
   assert(!r.pageCacheFailed_ && memory::live==0);}
  reset();
  {EpubReaderActivity r;r.section->images=true;memory::failAt=1;r.renderIdle(0);
   assert(!r.pageCacheFailed_ && memory::live==0);}
  // Interrupt in each plane and at every element boundary; scratch never leaks.
  for(int element=1;element<=18;++element) {
    reset(); EpubReaderActivity r; const auto original=r.renderer.frame;
    r.renderer.mode=GfxRenderer::GRAYSCALE_MSB;
    cancelAt=element; r.renderIdle(0);
    assert(r.pageCache_[(element-1)/9].state!=ReaderPageCache::State::Ready);
    assert(r.renderer.frame==original && r.renderer.mode==GfxRenderer::GRAYSCALE_MSB);
    assert(renders==element);
    assert(r.renderer.fonts.clears==(element<=9 ? 2 : 4));
  }
  reset();
  {EpubReaderActivity r; activityManager.cancelled=true; r.renderIdle(0); assert(r.section->loads==0);}
  reset();
  {EpubReaderActivity r; r.section->cancelOnLoad=true; r.renderIdle(0); assert(memory::allocations==0);}
  for(int fail=1;fail<=8;++fail) {
    reset(); EpubReaderActivity r; memory::failAt=fail; r.renderIdle(0);
    assert(r.pageCacheFailed_ && memory::live==0 && !r.pageCacheEligible());
    r.renderIdle(0); assert(r.section->loads==(fail<=4 ? 1 : 2));
  }
  reset();
  {EpubReaderActivity r; memory::headroom=false; r.renderIdle(0); assert(r.pageCacheFailed_ && memory::live==0);}
  reset();
  {EpubReaderActivity r; r.section->building=true; r.renderIdle(0);
   assert(r.pageCache_[0].state==ReaderPageCache::State::Ready && r.pageCache_[1].state==ReaderPageCache::State::Ready);}
  reset();
  {EpubReaderActivity r; r.section->currentPage=0; r.renderedPageKey_=r.pageCacheKey(0,1,1,1,1);
   r.renderIdle(0); assert(r.section->loads==1 && memory::allocations==4);}
  reset();
  {EpubReaderActivity r; r.section->currentPage=19; r.renderedPageKey_=r.pageCacheKey(19,1,1,1,1);
   r.renderIdle(0); assert(r.section->loads==1 && r.pageCache_[1].key.page==18);}
  reset();
  {EpubReaderActivity r; r.overlay=EpubReaderActivity::Overlay::Menu; r.renderIdle(0); assert(r.section->loads==0);}
  reset();
}
'''
        run_cpp(program, (ROOT / 'src/activities/reader', ROOT / 'lib/Epub', ROOT / 'lib/Memory'))

    def test_readpico_input_cancellation_on_hardware_and_simulator(self):
        source = (ROOT / 'src/main.cpp').read_text()
        begin = source.index('  // Cancel before any handler can wait for the framebuffer lock.')
        end = source.index('\n#endif', source.index('    activityManager.cancelIdleRender();', begin))
        production = source[begin:end]
        program = r'''
#include <cassert>
#include <cstdint>
struct Input {
  bool press=false, release=false;
  bool wasAnyPressed() const {return press;}
  bool wasAnyReleased() const {return release;}
} mappedInputManager;
struct GPIO {
  bool physical=false, touch=false;
#if CROSSPOINT_EMULATED
  bool wasAnyPressed() const {return physical;}
#else
  uint8_t physicalPressedMask() const {return physical ? 1 : 0;}
#endif
  bool wasTouchActivity() const {return touch;}
} gpio;
struct Manager {int cancellations=0; void cancelIdleRender() {++cancellations;}} activityManager;
void poll() {
''' + production + r'''
}
int main() {
  poll(); assert(activityManager.cancellations==0);
  bool* events[]={&mappedInputManager.press,&mappedInputManager.release,&gpio.physical,&gpio.touch};
  for(auto* event:events) {
    *event=true; poll(); assert(activityManager.cancellations==1 && *event);
    *event=false; activityManager.cancellations=0;
  }
}
'''
        for emulated in (0, 1):
            with self.subTest(emulated=emulated):
                run_cpp(f'#define CROSSPOINT_EMULATED {emulated}\n' + program)

    def test_render_task_priorities_and_waiter(self):
        source = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
        loop = method(source, 'void ActivityManager::renderTaskLoop(')
        header = (ROOT / 'src/activities/ActivityManager.h').read_text()
        cancellation = '\n'.join(method(header, name) for name in (
            'void cancelIdleRender(', 'bool idleRenderCancelled('))
        program = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
using TaskHandle_t=void*;
constexpr unsigned portMAX_DELAY=9999;
#define pdTRUE true
#define pdMS_TO_TICKS(ms) (ms)
#define taskENTER_CRITICAL(x) ((void)0)
#define taskEXIT_CRITICAL(x) ((void)0)
constexpr int eIncrement=0;
struct Stop {};
int notifications=0, cursor=0;
std::vector<int> wakes;
std::vector<unsigned> waits;
struct Settings {int screenInverted=0;} SETTINGS;
struct Display {void setInverted(bool) {}} display;
struct RenderLock {};
struct HalPowerManager {struct Lock {~Lock() {}};};
struct ActivityManager;
ActivityManager* manager;
unsigned ulTaskNotifyTake(bool,unsigned);
void xTaskNotify(TaskHandle_t,int,int) {++notifications;}
struct Activity {
  int renders=0,idles=0;
  bool cancelDuringRender=false,queueWaiterDuringIdle=false;
  unsigned delay=400;
  unsigned idleRenderDelayMs() const {return delay;}
  void render(RenderLock&&);
  void renderIdle(uint32_t);
};
struct ActivityManager {
  enum class PendingAction {None,Push};
  std::atomic<PendingAction> pendingAction{PendingAction::None};
  std::atomic<uint32_t> idleRenderGeneration{0};
  std::unique_ptr<Activity> currentActivity=std::make_unique<Activity>();
  TaskHandle_t waitingTaskHandle=0;
  std::atomic<bool> requestedUpdate{false};
  bool isSwitchPending() const {return pendingAction!=PendingAction::None;}
''' + cancellation + r'''
  void renderTaskLoop();
};
void Activity::render(RenderLock&&) {++renders; if(cancelDuringRender) ++manager->idleRenderGeneration;}
void Activity::renderIdle(uint32_t) {
  ++idles;
  if(queueWaiterDuringIdle) {manager->waitingTaskHandle=manager; ++manager->idleRenderGeneration;}
}
unsigned ulTaskNotifyTake(bool,unsigned wait) {
  waits.push_back(wait);
  if(cursor==static_cast<int>(wakes.size())) throw Stop{};
  return wakes[cursor++];
}
''' + loop + r'''
void run(ActivityManager& m,std::vector<int> events) {
  manager=&m; wakes=events; waits.clear(); cursor=0; notifications=0;
  try {m.renderTaskLoop();} catch(Stop&) {}
}
int main() {
  {ActivityManager m; run(m,{1,0});
   assert(m.currentActivity->renders==1 && m.currentActivity->idles==1);
   assert((waits==std::vector<unsigned>{portMAX_DELAY,400,portMAX_DELAY}));}
  {ActivityManager m; run(m,{1,1,0});
   assert(m.currentActivity->renders==2 && m.currentActivity->idles==1);}
  {ActivityManager m; m.currentActivity->cancelDuringRender=true; run(m,{1,0});
   assert(m.currentActivity->idles==0);}
  {ActivityManager m; m.currentActivity->queueWaiterDuringIdle=true; run(m,{1,0});
   assert(notifications==0 && m.waitingTaskHandle==&m);}
  {ActivityManager m; m.currentActivity->queueWaiterDuringIdle=true; run(m,{1,0,1});
   assert(notifications==1 && m.waitingTaskHandle==0 && m.currentActivity->renders==2);}
  {ActivityManager m; m.pendingAction=ActivityManager::PendingAction::Push; run(m,{1});
   assert(m.currentActivity->renders==0 && m.currentActivity->idles==0 && waits.back()==portMAX_DELAY);}
  {ActivityManager m; m.currentActivity->delay=0; run(m,{1}); assert(waits.back()==portMAX_DELAY);}
  {ActivityManager m; assert(!m.idleRenderCancelled(0)); m.cancelIdleRender(); assert(m.idleRenderCancelled(0));}
  {ActivityManager m; m.requestedUpdate=true; run(m,{1,0}); assert(m.currentActivity->idles==0);}
}
'''
        run_cpp(program)


    def test_image_stage_cancel_cleanup_and_retry(self):
        page = (ROOT / 'lib/Epub/Epub/Page.cpp').read_text()
        epub = (ROOT / 'lib/Epub/Epub.cpp').read_text()
        cache = (ROOT / 'lib/Epub/Epub/converters/PixelCache.h').read_text()
        program = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "CancelCheck.h"
template<class... Args> void log(const char*,const char*,Args...) {}
#define LOG_DBG(...) log(__VA_ARGS__)
#define LOG_ERR(...) log(__VA_ARGS__)
unsigned long millis(){return 0;}
bool stopped=false;
CancelCheck cancel{nullptr,[](void*){return stopped;}};
struct HalFile {
 bool opened=false;
 int writes=0,stopAfter=0;
 std::vector<uint8_t> bytes;
 size_t write(const void* p,size_t n){
  const auto* b=static_cast<const uint8_t*>(p);bytes.insert(bytes.end(),b,b+n);
  if(stopAfter && ++writes==stopAfter) stopped=true;
  return n;
 }
 void close(){opened=false;}
 void flush(){}
 bool isOpen()const{return opened;}
};
struct Store {
 bool exists=false;int removes=0;
 bool openFileForWrite(const char*,const std::string&,HalFile& f){exists=true;f.opened=true;return true;}
 bool remove(const char*){exists=false;++removes;return true;}
} Storage;
""" + method(cache, 'struct PixelCache') + ';' + r"""
struct Epub {
 bool stopInStream=false;
 bool readItemContentsToStream(const std::string&,HalFile& f,size_t n,bool,size_t maximum,CancelCheck c)const{
  assert(n==8192 && maximum==16384);f.write("partial",7);
  if(stopInStream) stopped=true;
  return !c.isCancelled();
 }
 bool extractItemToFile(const std::string&,const std::string&,CancelCheck)const;
};
""" + method(epub, 'bool Epub::extractItemToFile(') + r"""
struct GfxRenderer{};
struct Block {
 bool stopExtract=false, stopDecode=false, needed=true;
 int extracts=0,decodes=0;
 bool needsDecode(){return needed;}
 bool ensureExtracted(CancelCheck){++extracts;if(stopExtract)stopped=true;return true;}
 bool cacheDecodedImage(GfxRenderer&,int,int,CancelCheck c){++decodes;if(stopDecode)stopped=true;return !c.isCancelled();}
};
constexpr int TAG_PageImage=1;
struct Element{virtual ~Element()=default;virtual int getTag(){return 0;}};
struct PageImage:Element{Block block;int xPos=0,yPos=0;int getTag()override{return TAG_PageImage;}Block& getImageBlock(){return block;}};
struct Page{
 std::vector<std::unique_ptr<Element>> elements;
 bool warmImages(GfxRenderer&,int,int,CancelCheck);
};
""" + method(page, 'bool Page::warmImages(') + r"""
int main(){
 GfxRenderer renderer;Page page;
 auto a=std::make_unique<PageImage>();auto* first=a.get();page.elements.push_back(std::move(a));
 auto b=std::make_unique<PageImage>();auto* second=b.get();page.elements.push_back(std::move(b));
 first->block.stopExtract=true;
 assert(!page.warmImages(renderer,0,0,cancel));
 assert(first->block.decodes==0 && second->block.extracts==0);
 stopped=false;first->block.stopExtract=false;first->block.stopDecode=true;
 assert(!page.warmImages(renderer,0,0,cancel));assert(second->block.extracts==0);
 stopped=false;first->block.stopDecode=false;
 assert(page.warmImages(renderer,0,0,cancel));assert(second->block.decodes==1);
 Epub epub;epub.stopInStream=true;
 assert(!epub.extractItemToFile("src","dest",cancel));assert(!Storage.exists);
 stopped=false;epub.stopInStream=false;
 assert(epub.extractItemToFile("src","dest",cancel));assert(Storage.exists);
 {
  PixelCache cache;assert(cache.begin("test.pxc",8,32,0,0,1));
  cache.file.stopAfter=3;assert(!cache.finalize(cancel));assert(!Storage.exists && !cache.file.isOpen());
 }
 stopped=false;
 {
  PixelCache cache;assert(cache.begin("test.pxc",8,32,0,0,1));
  assert(cache.finalize(cancel));assert(Storage.exists && cache.file.bytes.size()==4+2*32);
 }
 assert(Storage.exists); // A complete cache survives destruction and can be retried normally.
}
"""
        run_cpp(program, include_dirs=(ROOT / 'lib/Memory',))


if __name__ == '__main__':
    unittest.main()
