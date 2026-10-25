"""Execute both production preload callbacks: Flash may resume only after refresh."""
from pathlib import Path
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class FontPreloadRefreshTest(unittest.TestCase):
    def test_callbacks_wait_outside_lock_and_remain_throttled(self):
        for path, activity in (
            ('settings/TextSettingsActivity.cpp', 'TextSettingsActivity'),
            ('boot_sleep/BootActivity.cpp', 'BootActivity'),
        ):
            with self.subTest(activity=activity):
                source = (ROOT / 'src/activities' / path).read_text()
                callback = method(source, '[](size_t completed, size_t total, void* context)')
                run_cpp(r'''
#include <cassert>
#include <cstddef>
#include <atomic>
static bool locked = false;
enum class Stage { Copying, Verifying };
struct Activity {
 std::atomic<size_t> preloadTotal_{1}, preloadCompleted_{0}, total_{1}, completed_{0};
 std::atomic<Stage> stage_{Stage::Copying};
 bool preloadVerifying_ = false;
 unsigned lastPreloadPercent_ = 0, lastRequestedPercent_ = 0;
 int framesCompleted = 0;
 void requestUpdateAndWait() { assert(!locked); ++framesCompleted; }
 void requestUpdate(bool) { assert(false && "asynchronous refresh overlaps next Flash write"); }
};
using TextSettingsActivity = Activity;
using BootActivity = Activity;
struct RenderLock {
 explicit RenderLock(Activity&) { assert(!locked); locked = true; }
 ~RenderLock() { locked = false; }
};
int main() {
 Activity activity;
 auto progress = ''' + callback + r''';
 progress(0, 0, &activity); assert(activity.framesCompleted == 0);
 progress(1, 1000, &activity); assert(activity.framesCompleted == 0);
 progress(100, 1000, &activity); assert(activity.framesCompleted == 1);
 progress(199, 1000, &activity); assert(activity.framesCompleted == 1);
 progress(500, 1000, &activity); assert(activity.framesCompleted == 2);
 progress(501, 1000, &activity); assert(activity.framesCompleted == 3);
 progress(599, 1000, &activity); assert(activity.framesCompleted == 3);
 progress(600, 1000, &activity); assert(activity.framesCompleted == 4);
 progress(1000, 1000, &activity); assert(activity.framesCompleted == 5);
 assert(!locked);
}
''')

    def test_full_preload_rebuilds_long_path_before_releasing_reader_font(self):
        source = (ROOT / 'src/activities/settings/TextSettingsActivity.cpp').read_text()
        registry = (ROOT / 'lib/EpdFont/SdCardFontRegistry.cpp').read_text()
        header = (ROOT / 'src/activities/settings/TextSettingsActivity.h').read_text()
        settings = (ROOT / 'src/CrossPointSettings.h').read_text()
        preload_member = next(line.strip() for line in header.splitlines() if 'preloadFamilyName_ =' in line)
        settings_member = next(line.strip() for line in settings.splitlines() if 'char sdFontFamilyName[' in line)
        run_cpp(r'''
#include <cassert>
#include <atomic>
#include <cstring>
#include <functional>
#include <SdCardFontRegistry.h>
#include <SdCardFontCache.h>
#define LOG_INF(...) ((void)0)
static bool locked=false;
int releases=0,preloads=0;
std::string writtenPath;
SdCardFontCache::Result preloadResult=SdCardFontCache::Result::Ok;
std::function<void()> onRelease;
struct Settings {
''' + settings_member + r'''
} SETTINGS;
namespace NetworkStartup {void logMemory(const char*){}}
struct TextSettingsActivity;
struct RenderLock {explicit RenderLock(TextSettingsActivity&){assert(!locked);locked=true;}~RenderLock(){locked=false;}};
struct {void releaseLoadedFont(int){assert(locked);++releases;if(onRelease)onRelease();}} sdFontSystem;
namespace SdCardFontCache {
Result preload(const char* path,ProgressCallback progress,void* context){
 assert(!locked);++preloads;writtenPath=path;progress(100,100,context);return preloadResult;
}
}
struct TextSettingsActivity {
 enum class FontLoadState {Idle,Preloading,Ready};
 int renderer=0,frames=0;
''' + preload_member + r'''
 uint8_t preloadPointSize_=0;
 bool preloadVerifying_=false;
 std::atomic<size_t> preloadCompleted_{0},preloadTotal_{1};
 unsigned lastPreloadPercent_=0;
 std::atomic<FontLoadState> fontLoadState_{FontLoadState::Idle};
 void requestUpdateAndWait(){
   assert(!locked);
   // The real render path consumes this pointer for every Preloading/Ready frame.
   assert(preloadFamilyName_ == SETTINGS.sdFontFamilyName);
   assert(std::strcmp(preloadFamilyName_,"SelectedFamily")==0);
   ++frames;
 }
 SdCardFontCache::Result preloadFont(const SdCardFontFamilyInfo&,const SdCardFontFileInfo&);
};
''' + method(registry,'void cpfontTail(') + '\n' +
            'constexpr size_t CPFONT_TAIL_SIZE = sizeof("_255.cpfont");\n' +
            method(registry,'bool SdCardFontFamilyInfo::filePath(') +
            method(source,'SdCardFontCache::Result TextSettingsActivity::preloadFont(') + r'''
int main(){
 SdCardFontFamilyInfo family;family.name=std::string(127,'F');family.root="/.fonts";
 SdCardFontFileInfo file;const std::string filename=std::string(117,'L')+"_14.cpfont";
 file.name=std::make_unique<char[]>(filename.size()+1);std::memcpy(file.name.get(),filename.c_str(),filename.size()+1);file.pointSize=14;
 TextSettingsActivity activity;
 std::strcpy(SETTINGS.sdFontFamilyName,"SelectedFamily");
 const std::string originalName=family.name;
 const std::string fullPath="/.fonts/"+family.name+"/"+filename;
 onRelease=[&]{family.name=std::string(200,'R');}; // Rescan can replace registry-owned strings.
 assert(activity.preloadFont(family,file)==SdCardFontCache::Result::Ok);
 assert(writtenPath==fullPath&&writtenPath.size()>160);
 assert(activity.preloadFamilyName_==SETTINGS.sdFontFamilyName);
 onRelease={};family.name=originalName;
 assert(releases==1&&preloads==1&&activity.fontLoadState_.load()==TextSettingsActivity::FontLoadState::Ready);
 releases=preloads=0;family.root=nullptr;
 assert(activity.preloadFont(family,file)==SdCardFontCache::Result::InvalidFont);
 assert(releases==0&&preloads==0);
 family.root="/.fonts";preloadResult=SdCardFontCache::Result::ReadFailed;
 assert(activity.preloadFont(family,file)==SdCardFontCache::Result::ReadFailed);
 assert(releases==1&&preloads==1);
}
''', include_dirs=(ROOT/'lib/EpdFont',))

    def test_percentage_text_boundaries(self):
        source = (ROOT / 'src/components/themes/BaseTheme.cpp').read_text()
        draw = method(source, 'int BaseTheme::drawProgressBar(')
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
struct Rect { int x, y, width, height; };
constexpr int UI_10_FONT_ID = 0;
struct GfxRenderer {
 mutable std::string text;
 void drawRect(int,int,int,int) const {}
 void fillRect(int,int,int,int) const {}
 void drawCenteredText(int,int,const char* value) const { text = value; }
};
struct BaseTheme {
 int measureProgressBarHeight(const GfxRenderer&,int,bool) const { return 1; }
 int drawProgressBar(const GfxRenderer&,Rect,size_t,size_t,bool) const;
};
''' + draw + r'''
int main() {
 BaseTheme theme; GfxRenderer renderer; Rect rect{0,0,400,20};
 theme.drawProgressBar(renderer,rect,0,1,true); assert(renderer.text == "0%");
 theme.drawProgressBar(renderer,rect,6549504,13099008,true); assert(renderer.text == "50%");
 theme.drawProgressBar(renderer,rect,6549505,13099008,true); assert(renderer.text == "50%");
 theme.drawProgressBar(renderer,rect,13099008,13099008,true); assert(renderer.text == "100%");
 renderer.text.clear(); theme.drawProgressBar(renderer,rect,0,0,true); assert(renderer.text.empty());
}
''')


if __name__ == '__main__':
    unittest.main()
