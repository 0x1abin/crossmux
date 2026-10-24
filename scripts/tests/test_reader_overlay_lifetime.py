"""Serialize real reader menu transitions with an in-flight render."""
from pathlib import Path
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class ReaderOverlayLifetimeTest(unittest.TestCase):
    def test_open_switch_close_and_render_lifetime(self):
        source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        reopen = method(source, 'void EpubReaderActivity::openReaderMenu(')
        reopen = reopen[:reopen.index('  // Child screens')] + '}\n'
        functions = reopen + '\n'.join(method(source, name) for name in (
            'void EpubReaderActivity::openOverlay(',
            'void EpubReaderActivity::closeOverlayToPage(',
        ))
        run_cpp(r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
std::mutex renderMutex, signalMutex;
std::condition_variable lockAttempted;
std::atomic<int> attempts{0};
thread_local bool ownsRender=false;
struct RenderLock {
  RenderLock(){
    assert(!ownsRender); // The firmware mutex is non-recursive.
    {std::lock_guard<std::mutex> guard(signalMutex);++attempts;}
    lockAttempted.notify_all();renderMutex.lock();ownsRender=true;
  }
  ~RenderLock(){ownsRender=false;renderMutex.unlock();}
};
void requireRender(){assert(ownsRender);}
#define LOG_ERR(...) ((void)0)
struct {char sdFontFamilyName[32]{};int fontPointSize=20,sdFontFlashPreload=0;} SETTINGS;
struct HalDisplay {enum {FAST_REFRESH};};
struct GfxRenderer {
  bool gray=false;int stores=0,restores=0,displays=0;
  bool storeBwBuffer(){requireRender();++stores;return true;}
  void restoreBwBuffer(bool resync){requireRender();assert(!resync);++restores;}
  void displayBuffer(int){requireRender();++displays;}
  bool grayscaleReplacesFrame()const{return gray;}
};
struct ReaderToolbarUi {
  struct Nav {int top=0;void reset(int i=0){requireRender();top=i;}} value;
  explicit ReaderToolbarUi(GfxRenderer&){requireRender();}
  ~ReaderToolbarUi(){requireRender();}
  void begin(){requireRender();}
  Nav& nav(){requireRender();return value;}
};
bool failAllocation=false;
template<class T,class... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... args){
  requireRender();return failAllocation?nullptr:std::make_unique<T>(std::forward<A>(args)...);
}
bool xteink=true;
bool xteinkClassPanel(){return xteink;}
struct EpubReaderActivity {
  enum class Overlay {None,Toolbar,Contents,Text,More};Overlay overlay=Overlay::None;
  GfxRenderer renderer;
  struct {void resetHomeButtonInput(){requireRender();}bool hasTouch()const{return true;}} mappedInput;
  struct {void dismiss(){requireRender();}} overlayPopup;
  struct {void begin(const char*,int,bool){requireRender();}} fontPreview;
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  int pendingManualTurn=0,focusedTool=0,panelIndex=0,pagesUntilFullRefresh=8,requests=0;
  bool panelHoldJumped=false,panelCursorShown=false,overlayPageStored=false;
  bool section=true,pageBufferStale=false,imageScalingDirty=false;
  bool usesToolbarMenu()const{return true;}
  int currentTocIndex()const{return 2;}
  void buildMoreActions(){requireRender();}
  void settleOverlayRefresh(){requireRender();}
  void renderOverlay(){requireRender();assert(toolbarUi);}
  void pushOverlayRefresh(){requireRender();}
  void discardOverlayPage(){requireRender();overlayPageStored=false;}
  void requestUpdate(){requireRender();++requests;}
  void openReaderMenu();void openOverlay(Overlay);void closeOverlayToPage();
};
''' + functions + r'''
int main(){
  for(bool panel:{false,true})for(bool stored:{false,true})for(bool dirty:{false,true}){
    EpubReaderActivity reader;xteink=panel;reader.renderer.gray=panel;
    reader.openOverlay(EpubReaderActivity::Overlay::Toolbar);
    reader.openOverlay(EpubReaderActivity::Overlay::Text);
    reader.openOverlay(EpubReaderActivity::Overlay::Contents);
    reader.openOverlay(EpubReaderActivity::Overlay::More);
    reader.openReaderMenu();
    reader.overlayPageStored=stored;reader.imageScalingDirty=dirty;
    int restores=reader.renderer.restores;
    reader.closeOverlayToPage();
    bool restored=!panel && stored && !dirty;
    assert(!reader.toolbarUi && reader.overlay==EpubReaderActivity::Overlay::None);
    assert(reader.renderer.restores==restores+int(restored));
    assert(reader.renderer.displays==int(restored));
    assert(reader.requests==1+int(!restored)); // reopening + page redraw
    assert(reader.pagesUntilFullRefresh==(panel?1:8));
    failAllocation=true;reader.openOverlay(EpubReaderActivity::Overlay::Toolbar);
    assert(!reader.toolbarUi && reader.overlay==EpubReaderActivity::Overlay::None);
    reader.openReaderMenu();assert(!reader.toolbarUi);
    failAllocation=false;
  }
  EpubReaderActivity reader;xteink=true;
  reader.openOverlay(EpubReaderActivity::Overlay::Toolbar);
  std::promise<void> painting, release;
  auto releaseFrame=release.get_future();
  std::thread render([&]{
    RenderLock lock;auto* active=reader.toolbarUi.get();
    painting.set_value();releaseFrame.wait();
    assert(reader.toolbarUi.get()==active);
    assert(reader.overlay==EpubReaderActivity::Overlay::Toolbar);
    reader.renderOverlay();
  });
  painting.get_future().wait();
  int nextAttempt=attempts.load()+1;
  std::thread close([&]{reader.closeOverlayToPage();});
  {std::unique_lock<std::mutex> guard(signalMutex);
   lockAttempted.wait(guard,[&]{return attempts.load()>=nextAttempt;});}
  // Close is waiting on the paint: it has not changed state or freed the UI.
  assert(reader.toolbarUi && reader.overlay==EpubReaderActivity::Overlay::Toolbar);
  release.set_value();render.join();close.join();
  assert(!reader.toolbarUi && reader.overlay==EpubReaderActivity::Overlay::None);
}
''')


    def test_font_picker_return_waits_for_render_after_applying_settings(self):
        source = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
        start = source.index('startActivityForResult(std::move(textSettings), [this](const ActivityResult&)')
        callback = method(source[start:], '[this](const ActivityResult&)')
        apply_settings = method(source, 'void EpubReaderActivity::applyReaderTextSettings(')
        run_cpp(r"""
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
std::mutex renderMutex, signalMutex;
std::condition_variable lockAttempted;
std::atomic<int> attempts{0};
thread_local bool ownsRender=false, pauseSettingsUnlock=false;
std::promise<void> settingsApplied, painting, releasePaint;
auto settingsReady=settingsApplied.get_future();
auto paintingReady=painting.get_future().share();
auto releaseFrame=releasePaint.get_future();
struct RenderLock {
  RenderLock(){
    assert(!ownsRender);
    {std::lock_guard<std::mutex> guard(signalMutex);++attempts;}
    lockAttempted.notify_all();renderMutex.lock();ownsRender=true;
  }
  ~RenderLock(){
    ownsRender=false;renderMutex.unlock();
    if(pauseSettingsUnlock){
      pauseSettingsUnlock=false;settingsApplied.set_value();paintingReady.wait();
    }
  }
};
struct {int saves=0;void saveToFile(){assert(!ownsRender);++saves;}} SETTINGS;
struct {int loads=0;void ensureLoaded(int){assert(ownsRender);++loads;}} sdFontSystem;
struct ReaderToolbarUi {int begins=0;void begin(){assert(ownsRender);++begins;}};
struct ActivityResult {};
struct EpubReaderActivity {
  enum class Overlay {None,Text};Overlay overlay=Overlay::None;
  int panelIndex=4,requests=0,renderer=0,currentSpineIndex=3;
  int cachedSpineIndex=-1,cachedChapterTotalPageCount=0,nextPageNumber=0,remembered=0;
  struct Section {int currentPage=7,pageCount=40;};
  std::unique_ptr<Section> section=std::make_unique<Section>();
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  void rememberCurrentContentOffset(){assert(ownsRender);++remembered;}
  void requestUpdate(){++requests;}
  void applyReaderTextSettings();
  auto pickerResult(){return
""" + callback + r""";}
};
""" + apply_settings + r"""
void checkReturn(const EpubReaderActivity& reader,bool hasToolbar){
  assert(reader.overlay==EpubReaderActivity::Overlay::Text && reader.panelIndex==0);
  assert(reader.requests==1 && !reader.section && reader.remembered==1);
  assert(reader.cachedSpineIndex==3 && reader.cachedChapterTotalPageCount==40);
  assert(reader.nextPageNumber==7);
  assert(!hasToolbar || reader.toolbarUi->begins==1);
}
int main(){
  // The actual apply method takes its own lock; wrapping it would nest a mutex.
  for(bool hasToolbar:{false,true}){
    EpubReaderActivity reader;
    if(hasToolbar)reader.toolbarUi=std::make_unique<ReaderToolbarUi>();
    reader.pickerResult()(ActivityResult{});checkReturn(reader,hasToolbar);
  }
  EpubReaderActivity reader;reader.toolbarUi=std::make_unique<ReaderToolbarUi>();
  int nextAttempt=attempts.load()+3; // apply settings, in-flight paint, restore menu
  std::thread render([&]{
    settingsReady.wait();RenderLock lock;painting.set_value();releaseFrame.wait();
    assert(reader.overlay==EpubReaderActivity::Overlay::None && reader.panelIndex==4);
    assert(reader.toolbarUi->begins==0);
  });
  std::thread returning([&]{
    pauseSettingsUnlock=true;reader.pickerResult()(ActivityResult{});
  });
  paintingReady.wait();
  {std::unique_lock<std::mutex> guard(signalMutex);
   assert(lockAttempted.wait_for(guard,std::chrono::seconds(5),[&]{return attempts.load()>=nextAttempt;}));}
  // A paint started after settings applied: restore must wait before changing UI.
  assert(reader.overlay==EpubReaderActivity::Overlay::None && reader.panelIndex==4);
  assert(reader.toolbarUi->begins==0);
  releasePaint.set_value();render.join();returning.join();checkReturn(reader,true);
  assert(SETTINGS.saves==3 && sdFontSystem.loads==3);
}
""")


if __name__ == '__main__':
    unittest.main()
