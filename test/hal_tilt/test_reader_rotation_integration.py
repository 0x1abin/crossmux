"""Exercise production reader/EPUB/manager admission with input and storage seams."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/tests'))
from test_reading_ui_regressions import method

reader = (ROOT / 'src/activities/reader/ReaderActivity.cpp').read_text()
epub = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
xtc = (ROOT / 'src/activities/reader/XtcReaderActivity.h').read_text()
mapped = (ROOT / 'src/MappedInputManager.cpp').read_text()
hal_gpio = (ROOT / 'lib/hal/HalGPIO.cpp').read_text()
xtc_gate = method(xtc, 'bool automaticRotationInputAvailable()') if 'bool automaticRotationInputAvailable()' in xtc else ''
home_gate = method(mapped, 'bool MappedInputManager::isHomeKeyInteracting()') if 'bool MappedInputManager::isHomeKeyInteracting()' in mapped else 'bool MappedInputManager::isHomeKeyInteracting() const { return false; }'
home_down = method(hal_gpio, 'bool HalGPIO::isHomeKeyDown()') if 'bool HalGPIO::isHomeKeyDown()' in hal_gpio else 'bool HalGPIO::isHomeKeyDown() const { return false; }'
methods = '\n'.join([home_down, home_gate,
    method(reader, 'bool ReaderActivity::automaticRotationInputAvailable()'),
    method(reader, 'void ReaderActivity::handleAutoRotation()'),
    method(reader, 'bool ReaderActivity::applyAutomaticOrientation('),
    method(epub, 'void EpubReaderActivity::applyOrientation('),
    method(epub, 'bool EpubReaderActivity::automaticRotationInputAvailable()'),
    method(epub, 'bool EpubReaderActivity::applyAutomaticOrientation('),
    method(manager, 'bool ActivityManager::isForegroundReader()'),
    method(manager, 'bool ActivityManager::automaticRotationInputAvailable()'),
])
fixture = r"""
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include "ReaderAutoRotation.h"
struct Settings {uint8_t autoRotate=1,orientation=0;int saves=0;void saveToFile(){++saves;}} SETTINGS;
struct RenderLock {
 static inline bool busy=false;
 static bool peek(){return busy;}
 template<class T> explicit RenderLock(T&){assert(!busy);busy=true;}
 ~RenderLock(){busy=false;}
};
enum class HomeButtonAction {Ignore, Home, ReaderMenu};
struct HalGPIO {
 struct {bool down=false;bool isHomeKeyDown()const{return down;}} inputMgr;
 bool press=false,tap=false,longPress=false;
 bool isHomeKeyDown()const;
 bool wasHomeKeyPressed()const{return press;}bool wasHomeKeyTapped()const{return tap;}
 bool wasHomeKeyLongPressed()const{return longPress;}
};
struct MappedInputManager {
 enum class Button:uint8_t {First};static constexpr uint8_t kButtonCount=6;
 bool pressed=false,released=false,touchRelease=false,touchHeld=false,touchPress=false,remote=false;
 HalGPIO gpio;HomeButtonAction homeAction=HomeButtonAction::Ignore;
 bool hasRemoteClick()const{return remote;}bool isHomeKeyInteracting()const;
 std::array<bool,kButtonCount> held{};
 bool wasAnyPressed()const{return pressed;}bool wasAnyReleased()const{return released;}
 bool wasScreenTouchReleased()const{return touchRelease;}bool isPressed(Button b)const{return held[static_cast<uint8_t>(b)];}
 bool isScreenTouchHeld(int&,int&)const{return touchHeld;}bool wasScreenTouchPressed(int&,int&)const{return touchPress;}
};
struct Sensor {
 bool pending=false;uint8_t desired=0;int cancels=0;
 bool takeOrientation(uint8_t& out){if(!pending)return false;pending=false;out=desired;return true;}
 void cancelOrientationCandidate(){pending=false;++cancels;}
} halTiltSensor;
class ReaderActivity {
 public:
 virtual ~ReaderActivity()=default;
 MappedInputManager mappedInput;std::atomic<bool> pageRendered{true};bool overlay=false;int refreshes=0;
 virtual bool inputOverPage()const{return overlay;}virtual bool isReaderActivity()const{return true;}
 virtual bool automaticRotationInputAvailable()const;void handleAutoRotation();virtual bool applyAutomaticOrientation(uint8_t);
 void rotateOrientation(int delta){SETTINGS.orientation=static_cast<uint8_t>((SETTINGS.orientation+delta+4)%4);}
 void requestUpdate(){++refreshes;}
};
namespace ReaderUtils {void applyOrientation(int&,uint8_t) {assert(RenderLock::busy);}}
struct Popup {bool visible=false;bool isActive()const{return visible;}};
struct Preview {bool visible=false;bool active()const{return visible;}};
struct Section {int currentPage=7,pageCount=20,contentOffset=4321;};
class EpubReaderActivity:public ReaderActivity {
 public:
 bool epub=true;Popup loadFailurePopup,overlayPopup;Preview fontPreview;
 enum class FontPromptState {Idle,Busy};FontPromptState fontPromptState=FontPromptState::Idle;
 std::unique_ptr<Section> section=std::make_unique<Section>();
 int currentSpineIndex=4,cachedSpineIndex=-1,cachedChapterTotalPageCount=-1,nextPageNumber=-1,rememberedOffset=-1;
 int renderer=0;uint8_t appliedOrientation=0;
 void rememberCurrentContentOffset(){assert(RenderLock::busy && section);rememberedOffset=section->contentOffset;}
 void applyOrientation(uint8_t);bool automaticRotationInputAvailable()const override;bool applyAutomaticOrientation(uint8_t)override;
};
class XtcReaderActivity:public ReaderActivity {public:
"""+xtc_gate+r"""
};
class Child:public ReaderActivity {public:bool isReaderActivity()const override{return false;}};
class ActivityManager {
 public:
 enum class PendingAction {None,Push};PendingAction pendingAction=PendingAction::None;
 std::unique_ptr<ReaderActivity> currentActivity;std::atomic<bool> requestedUpdate{false};
 bool isForegroundReader()const;bool automaticRotationInputAvailable()const;
};
"""
tests = r"""
int main(){
 EpubReaderActivity r;
 assert(r.automaticRotationInputAvailable());
 r.overlayPopup.visible=true;
 assert(!r.automaticRotationInputAvailable());
 assert(!r.applyAutomaticOrientation(1));
 assert(SETTINGS.orientation==0 && r.section && SETTINGS.saves==0);
 r.overlayPopup.visible=false;
 r.mappedInput.remote=true;assert(!r.automaticRotationInputAvailable());r.mappedInput.remote=false;
 r.mappedInput.gpio.inputMgr.down=true;assert(!r.automaticRotationInputAvailable());r.mappedInput.gpio.inputMgr.down=false;
 for(bool HalGPIO::* field:{&HalGPIO::press,&HalGPIO::tap,&HalGPIO::longPress}){
  r.mappedInput.gpio.*field=true;assert(!r.automaticRotationInputAvailable());r.mappedInput.gpio.*field=false;
 }
 r.mappedInput.homeAction=HomeButtonAction::ReaderMenu;assert(!r.automaticRotationInputAvailable());
 r.mappedInput.homeAction=HomeButtonAction::Ignore;
 for(bool MappedInputManager::* field:{&MappedInputManager::pressed,&MappedInputManager::released,
   &MappedInputManager::touchRelease,&MappedInputManager::touchHeld,&MappedInputManager::touchPress}){
  r.mappedInput.*field=true;assert(!r.automaticRotationInputAvailable());r.mappedInput.*field=false;
 }
 for(auto& button:r.mappedInput.held){button=true;assert(!r.automaticRotationInputAvailable());button=false;}
 r.pageRendered=false;assert(!r.automaticRotationInputAvailable());r.pageRendered=true;
 r.overlay=true;assert(!r.automaticRotationInputAvailable());r.overlay=false;
 RenderLock::busy=true;assert(!r.automaticRotationInputAvailable());RenderLock::busy=false;
 r.loadFailurePopup.visible=true;assert(!r.automaticRotationInputAvailable());r.loadFailurePopup.visible=false;
 r.fontPromptState=EpubReaderActivity::FontPromptState::Busy;assert(!r.automaticRotationInputAvailable());
 r.fontPromptState=EpubReaderActivity::FontPromptState::Idle;
 r.fontPreview.visible=true;assert(!r.automaticRotationInputAvailable());r.fontPreview.visible=false;
 SETTINGS.autoRotate=0;assert(!r.automaticRotationInputAvailable());SETTINGS.autoRotate=1;
 halTiltSensor.pending=true;halTiltSensor.desired=1;r.overlayPopup.visible=true;
 r.handleAutoRotation();assert(!halTiltSensor.pending && halTiltSensor.cancels==1 && r.refreshes==0);
 r.overlayPopup.visible=false;
 halTiltSensor.pending=true;halTiltSensor.desired=4;r.handleAutoRotation();assert(r.refreshes==0);
 halTiltSensor.pending=true;halTiltSensor.desired=0;r.handleAutoRotation();assert(r.refreshes==0);
 halTiltSensor.pending=true;halTiltSensor.desired=1;r.handleAutoRotation();
 assert(SETTINGS.orientation==1 && SETTINGS.saves==1 && !r.section && r.refreshes==1);
 assert(r.rememberedOffset==4321 && r.cachedSpineIndex==4 && r.cachedChapterTotalPageCount==20 && r.nextPageNumber==7);
 assert(!RenderLock::busy);
 ActivityManager m;assert(!m.automaticRotationInputAvailable());
 m.currentActivity=std::make_unique<EpubReaderActivity>();assert(m.automaticRotationInputAvailable());
 m.requestedUpdate=true;assert(!m.automaticRotationInputAvailable());m.requestedUpdate=false;
 m.pendingAction=ActivityManager::PendingAction::Push;assert(!m.automaticRotationInputAvailable());
 m.pendingAction=ActivityManager::PendingAction::None;
 static_cast<EpubReaderActivity*>(m.currentActivity.get())->overlayPopup.visible=true;assert(!m.automaticRotationInputAvailable());
 m.currentActivity=std::make_unique<XtcReaderActivity>();assert(!m.automaticRotationInputAvailable());
 m.currentActivity=std::make_unique<Child>();assert(!m.automaticRotationInputAvailable());
}
"""
with tempfile.TemporaryDirectory(prefix='reader-rotation-real-') as temporary:
    work=Path(temporary);source=work/'check.cpp';binary=work/'check'
    source.write_text(fixture+'\n'+methods+'\n'+tests)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-fno-exceptions',
                    '-I'+str(ROOT/'src/activities/reader'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Production reader/EPUB/manager: input/popup/render barriers, cancelled events and existing reflow transaction passed')
