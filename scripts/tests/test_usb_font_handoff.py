"""Exercise USB font ownership with production selection and handoff methods."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp
from test_builtin_font_shrink import rows

ROOT = Path(__file__).resolve().parents[2]

class UsbFontHandoffTest(unittest.TestCase):
    def test_usb_start_success_failure_and_exit_order(self):
        activity = (ROOT / 'src/activities/network/UsbDriveActivity.cpp').read_text()
        program = r'''
#include <cassert>
#include <vector>
#define LOG_ERR(...) ((void)0)
std::vector<int> events; bool locked=false, detached=false, handoff=false;
unsigned long millis(){return 10;}
namespace bleinput {void stop(){events.push_back(1);}}
struct GfxRenderer {} renderer;
struct RenderLock {RenderLock(){assert(!locked);locked=true;} ~RenderLock(){locked=false;}};
struct SdFont {int ends=0;void beginStorageHandoff(GfxRenderer&){assert(locked&&!detached);handoff=true;events.push_back(2);}
 void endStorageHandoff(GfxRenderer&){assert(locked&&!detached);handoff=false;++ends;events.push_back(7);}} sdFontSystem;
struct StorageType {bool success=true;int ends=0;bool beginUsbDrive(){assert(handoff&&!locked);events.push_back(4);detached=success;return success;}
 void endUsbDrive(){detached=false;++ends;events.push_back(6);}} Storage;
struct Activity {virtual ~Activity()=default;void onEnter(){}void onExit(){events.push_back(8);}};
struct App {template<class...Args> void setScreen(Args...){}};
struct UsbDriveActivity : Activity {
 enum class State {IoError, WaitingForHost};State state=State::IoError;App app;bool preparing=true,startFailed=false,restartRequested=false;
 unsigned long startFailureStartedAt=0,hostWaitStartedAt=0;int updates=0;
 static void driveScreen(){}void resetUi(){}void requestUpdateAndWait(){assert(handoff&&!detached&&!locked);events.push_back(3);}
 void requestUpdate(){++updates;events.push_back(5);}void onEnter();void onExit();
};
''' + method(activity, 'void UsbDriveActivity::onEnter(') + method(activity, 'void UsbDriveActivity::onExit(') + r'''
int main(){
 for(bool ok:{false,true}) {
  events.clear();locked=detached=handoff=false;Storage.success=ok;Storage.ends=0;sdFontSystem.ends=0;
  UsbDriveActivity a;a.onEnter();assert(handoff&&detached==ok&&a.startFailed!=ok&&!a.preparing&&a.updates==1);
  assert((events==std::vector<int>{1,2,3,4,5}));
  locked=true;a.onExit();locked=false;
  assert(!handoff&&!detached&&Storage.ends==1&&sdFontSystem.ends==1);
  assert((events==std::vector<int>{1,2,3,4,5,6,7,8}));
 }
}
'''
        run_cpp(program, defines=('FREEINK_DEVICE_READPICO=1',))

    def test_release_keeps_embedded_hebrew_and_defers_reload(self):
        gfx = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        header = (ROOT / 'lib/GfxRenderer/GfxRenderer.h').read_text()
        system = (ROOT / 'src/SdCardFontSystem.cpp').read_text()
        activity = (ROOT / 'src/activities/network/UsbDriveActivity.cpp').read_text()
        on_enter = method(activity, 'void UsbDriveActivity::onEnter(')
        self.assertLess(on_enter.index('RenderLock lock;'), on_enter.index('beginStorageHandoff(renderer)'))
        self.assertLess(on_enter.index('beginStorageHandoff(renderer)'), on_enter.index('requestUpdateAndWait()'))
        self.assertLess(on_enter.index('requestUpdateAndWait()'), on_enter.index('Storage.beginUsbDrive()'))
        self.assertIn('endStorageHandoff(renderer)', method(activity, 'void UsbDriveActivity::onExit('))
        for signature in ('void SdCardFontSystem::ensureLoaded(', 'bool SdCardFontSystem::loadVariant(',
                          'bool SdCardFontSystem::adoptCompleteChineseNotoSans('):
            self.assertIn('storageHandoff_.load(std::memory_order_acquire)', method(system, signature))
        self.assertIn('storageHandoff_.load(std::memory_order_acquire)', method(system, 'int SdCardFontSystem::resolveVariant('))
        fontdir = ROOT / 'lib/EpdFont/builtinFonts'
        def coverage(*names):
            return [(a, b) for name in names for a,b,_ in rows((fontdir / (name + '.h')).read_text(), 'Intervals')]
        def table(name, ranges):
            return 'static const uint32_t ' + name + '[][2]={' + ','.join('{%du,%du}' % r for r in ranges) + '};\n'
        self.assertEqual(coverage('ubuntu_12_regular'), coverage('ubuntu_12_bold'))
        program = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "Utf8.h"
#include "Utf8.cpp"
#define LOG_ERR(...) ((void)0)
namespace BidiUtils {enum class BidiBaseDir {AUTO};}
const char* resolveVisualText(const char* text, std::string&, BidiUtils::BidiBaseDir){return text;}
struct SdFace {int calls=0;void prewarm(const char*,uint8_t,bool,bool){++calls;}};
''' + table('latin', coverage('notosans_16_regular')) + table('cjk16', coverage('notosans_cjk_16')) + table('cjk12', coverage('notosans_cjk_common_intervals')) + table('rtl', coverage('ubuntu_12_regular')) + r'''
std::set<uint32_t> from(const uint32_t (*v)[2],size_t n) {std::set<uint32_t> out;for(size_t i=0;i<n;++i)for(uint32_t x=v[i][0];x<=v[i][1];++x)out.insert(x);return out;}
struct EpdFontFamily {enum Style{REGULAR,BOLD};std::set<uint32_t> coverage;bool hasCodepoint(uint32_t c,Style)const{return coverage.contains(c);} void getTextDimensions(const char*,int* w,int* h,Style)const{*w=10;*h=10;}};
struct FontCacheManager {int releases=0;void releaseSdFontCaches(){++releases;}};
struct GfxRenderer {
 std::map<int,EpdFontFamily> fontMap;
 std::map<int,SdFace*> sdCardFonts_;
 std::map<int,int> sdCardFontScales_,preferredFontMap_;
 std::map<int,std::array<int,2>> fallbackFontMap_;
 FontCacheManager cache;
 FontCacheManager* getFontCacheManager(){return &cache;}
''' + method(header,'void removeFont(int fontId)') + r'''
 int resolveFontFamilyId(int) const;
 int resolveTextFontId(int,const char*,EpdFontFamily::Style) const;
 void ensureSdGlyphsResident(int,const char*,EpdFontFamily::Style,bool) const;
 int getTextWidth(int,const char*,EpdFontFamily::Style,BidiUtils::BidiBaseDir) const;
};
''' + method(gfx,'int GfxRenderer::resolveFontFamilyId(') + method(gfx,'int GfxRenderer::resolveTextFontId(') + method(gfx,'void GfxRenderer::ensureSdGlyphsResident(') + method(gfx,'int GfxRenderer::getTextWidth(') + r'''
struct {bool mounted=true;bool ready() const{return mounted;}} Storage;
struct SdCardFontSystem {
 std::atomic<bool> registryDirty_{false},storageHandoff_{false};
 std::vector<int> events;int reloads=0;
 void unloadTtf(GfxRenderer& r){assert(storageHandoff_.load());events.push_back(1);r.removeFont(415);}
 void unloadCpfonts(GfxRenderer& r){assert(storageHandoff_.load());events.push_back(2);r.removeFont(414);}
 void ensureLoaded(GfxRenderer&){assert(!storageHandoff_.load());assert(Storage.ready());assert(registryDirty_.load());++reloads;}
 void beginStorageHandoff(GfxRenderer&);
 void endStorageHandoff(GfxRenderer&);
};
''' + method(system,'void SdCardFontSystem::beginStorageHandoff(') + method(system,'void SdCardFontSystem::endStorageHandoff(') + r'''
int main(){
 GfxRenderer r; SdCardFontSystem s;
 for(int id:{14,15,16,17,18}) r.fontMap[id]={from(latin,std::size(latin))};r.fontMap[116]={from(cjk16,std::size(cjk16))};
 r.fontMap[112]={from(cjk12,std::size(cjk12))};r.fontMap[212]={from(rtl,std::size(rtl))};
 r.fontMap[414]={from(rtl,std::size(rtl))};r.fontMap[415]={from(rtl,std::size(rtl))};
 SdFace sd; r.sdCardFonts_[414]=&sd;for(int id:{14,15,16,17,18}) r.fallbackFontMap_[id]={116,414};r.fallbackFontMap_[116]={112,0};r.fallbackFontMap_[112]={212,0};
 for(int id:{14,15,16,17,18}) for(auto style:{EpdFontFamily::REGULAR,EpdFontFamily::BOLD}) {assert(r.resolveTextFontId(id,"טוען",style)==414);assert(r.getTextWidth(id,"טוען",style,BidiUtils::BidiBaseDir::AUTO)==10);}
 assert(sd.calls==10);r.preferredFontMap_[14]=415;assert(r.resolveTextFontId(14,"טוען",EpdFontFamily::REGULAR)==415);r.preferredFontMap_.clear();
 s.beginStorageHandoff(r);assert(s.storageHandoff_.load());assert(r.cache.releases==1);
 assert((s.events==std::vector<int>{1,2}));assert(!r.fontMap.contains(414)&&!r.fontMap.contains(415));
 for(int id:{14,15,16,17,18}) for(auto style:{EpdFontFamily::REGULAR,EpdFontFamily::BOLD}) {assert(r.resolveTextFontId(id,"טוען",style)==212);assert(r.resolveTextFontId(id,"海",style)==116);assert(r.getTextWidth(id,"טוען",style,BidiUtils::BidiBaseDir::AUTO)==10);}
 assert(sd.calls==10);
 Storage.mounted=false;s.endStorageHandoff(r);assert(s.storageHandoff_.load()&&s.reloads==0);
 Storage.mounted=true;s.endStorageHandoff(r);assert(!s.storageHandoff_.load()&&s.reloads==1);
}
'''
        run_cpp(program, include_dirs=(ROOT/'lib/Utf8',), defines=('CROSSMUX_UI_PROFILE_HIGH_DPI','CROSSPOINT_VECTOR_FONTS=1'))

if __name__ == '__main__': unittest.main()
