"""Exercise production retry handling and ImageBlock's optional error propagation."""
from pathlib import Path
import re
import unittest
from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class AirPageDisplayRetryTest(unittest.TestCase):
    def test_display_failure_keeps_store_and_stops_rendering(self):
        source = (ROOT / 'src/activities/apps/airpage/AirPageActivity.cpp').read_text()
        header = (ROOT / 'src/activities/apps/airpage/AirPageActivity.h').read_text()
        enums = '\n'.join(re.findall(r'  enum class (?:Screen|Notice|WallpaperResult|ImageDisplayResult) .*?};', header, re.S))
        run_cpp(r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include "CancelCheck.h"
#include <initializer_list>
struct Selected { bool current = true; };
namespace airpage {
struct AirPageImageRenderer { static void resetSessionFailures() {} };
struct AirPageWallpaper { static bool install(Selected) { return true; } };
}
struct Store {
 bool pending = true;
 int commits = 0;
 bool hasPendingDownload() const { return pending; }
 void commitDisplayedDownload(uint64_t) { pending = false; ++commits; }
 bool selectCurrent(Selected&) { return true; }
 // No reject/delete API: a transient failure must not call one.
};
struct AirPageActivity {
''' + enums + r'''
 std::atomic<ImageDisplayResult> imageDisplayResult_{ImageDisplayResult::None};
 Store imageStore_;
 Selected selectedImage_;
 bool autoSleepWallpaper_ = false, imageNeedsDisplay_ = true;
 int historySelection_ = 3, updates = 0;
 Screen screen_ = Screen::Image;
 Notice notice_ = Notice::None;
 WallpaperResult wallpaperResult_ = WallpaperResult::None;
 uint64_t currentArchiveDateKey() { return 0; }
 void rebuildHistoryRows() {}
 void requestUpdate() { ++updates; }
 void setAirPageScreen(Screen screen) { screen_ = screen; }
 bool processImageDisplayResult();
};
''' + method(source, 'bool AirPageActivity::processImageDisplayResult()') + r'''
int main() {
 using A = AirPageActivity;
 for (bool current : {false, true})
 for (auto failure : {A::ImageDisplayResult::OutOfMemory, A::ImageDisplayResult::Failure}) {
   A activity;
   activity.selectedImage_.current = current;
   activity.imageDisplayResult_ = failure;
   assert(activity.processImageDisplayResult());
   assert(activity.screen_ == A::Screen::History && !activity.imageNeedsDisplay_);
   assert(activity.imageStore_.pending && activity.imageStore_.commits == 0);
   assert(activity.historySelection_ == (current ? 0 : 3));
   assert(activity.notice_ == (failure == A::ImageDisplayResult::OutOfMemory
                              ? A::Notice::ImageOutOfMemory : A::Notice::ImageDisplayFailed));
   assert(!activity.processImageDisplayResult() && activity.updates == 1);
   activity.imageDisplayResult_ = A::ImageDisplayResult::Success;
   assert(activity.processImageDisplayResult());
   assert(activity.imageStore_.commits == (current ? 1 : 0));
 }
}
''', (ROOT / 'lib/Memory',))

    def test_history_page_actions_and_image_offsets(self):
        source = (ROOT / 'src/activities/apps/airpage/AirPageActivity.cpp').read_text()
        run_cpp(r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include "CancelCheck.h"
namespace fui { struct ActionEvent { int value; }; }
namespace airpage { struct AirPageImageRenderer { static void resetSessionFailures() {} }; }
struct Nav { void reset() {} };
struct Store {
 int page = 0, selected = -1;
 size_t historyCount() const { return 20; }
 bool hasPreviousHistoryPage() const { return page > 0; }
 bool hasNextHistoryPage() const { return page < 2; }
 bool previousHistoryPage() { --page; return true; }
 bool nextHistoryPage() { ++page; return true; }
 bool selectHistory(size_t index, int&) { selected = index; return index < 20; }
};
struct AirPageActivity {
 enum class Screen { History, Image };
 enum class Notice { InvalidImage, None };
 enum class WallpaperResult { None };
 struct RenderLock { explicit RenderLock(AirPageActivity&) {} };
 struct App { void clearTapFlash() {} } app;
 Screen screen_ = Screen::History;
 Notice notice_ = Notice::None;
 WallpaperResult wallpaperResult_ = WallpaperResult::None;
 Store imageStore_;
 Nav historyNav_;
 int historySelection_ = 0, selectedImage_ = 0, updates = 0;
 bool imageNeedsDisplay_ = false;
 void rebuildHistoryRows() {}
 void requestUpdate() { ++updates; }
 void setAirPageScreen(Screen screen) { screen_ = screen; }
 void moveHistorySelection(int index) { historySelection_ = index; }
 size_t historyRowCount() const;
 void openSelectedHistoryImage();
 static void onHistoryRow(const fui::ActionEvent&, void*);
};
''' + method(source, 'size_t AirPageActivity::historyRowCount()') +
            method(source, 'void AirPageActivity::openSelectedHistoryImage()') +
            method(source, 'void AirPageActivity::onHistoryRow(') + r'''
int main() {
 AirPageActivity a;
 assert(a.historyRowCount() == 21);
 // The same activation method is used by physical Confirm and touch rows.
 a.historySelection_ = 20;
 a.openSelectedHistoryImage();
 assert(a.imageStore_.page == 1 && a.historyRowCount() == 22);
 assert(a.historySelection_ == 0 && a.screen_ == AirPageActivity::Screen::History);
 AirPageActivity::onHistoryRow({1}, &a);
 assert(a.imageStore_.selected == 0 && a.screen_ == AirPageActivity::Screen::Image);
 a.screen_ = AirPageActivity::Screen::History;
 AirPageActivity::onHistoryRow({20}, &a);
 assert(a.imageStore_.selected == 19);
 a.screen_ = AirPageActivity::Screen::History;
 AirPageActivity::onHistoryRow({21}, &a);
 assert(a.imageStore_.page == 2 && a.historyRowCount() == 21);
 AirPageActivity::onHistoryRow({0}, &a);
 assert(a.imageStore_.page == 1);
 AirPageActivity::onHistoryRow({0}, &a);
 assert(a.imageStore_.page == 0 && a.historyRowCount() == 21);
 int updates = a.updates;
 AirPageActivity::onHistoryRow({21}, &a);
 AirPageActivity::onHistoryRow({-1}, &a);
 assert(a.updates == updates && a.imageStore_.page == 0);
}
''', (ROOT / 'lib/Memory',))

    def test_image_block_forwards_memory_failure_without_poisoning_retry(self):
        source = (ROOT / 'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
        decoder = (ROOT / 'lib/Epub/Epub/converters/ImageToFramebufferDecoder.h').read_text()
        types = decoder[decoder.index('enum class DecodeOutput'):decoder.index('class ImageToFramebufferDecoder')]
        run_cpp(r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include "CancelCheck.h"
#include <string>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
''' + types + r'''
struct FontCacheManager { bool isScanning() { return false; } };
struct GfxRenderer {
 FontCacheManager* getFontCacheManager() { return nullptr; }
 int getScreenWidth() { return 800; }
 int getScreenHeight() { return 480; }
 bool glyphIntersectsStrip(int,int,int,int) { return true; }
};
struct HalFile { size_t size() { return 42; } };
struct StorageStub { bool openFileForRead(const char*,const std::string&,HalFile&) { return true; } } Storage;
static bool remembered = false;
bool imageFailedThisRender(const std::string&) { return remembered; }
void rememberImageFailure(const std::string&) { remembered = true; }
std::string getCachePath(const std::string&) { return "image.pxc"; }
struct ImageBlock {
 enum class PixelCachePolicy { Stream };
 std::string imagePath = "image.jpg", srcPath;
 int width=100, height=100;
 bool hasValidCache() { return false; }
 bool ensureExtracted(CancelCheck) { return true; }
 void renderPlaceholder(GfxRenderer&,int,int) {}
 bool bilinearScalingEnabled() { return false; }
 bool renderInternal(GfxRenderer&,int,int,PixelCachePolicy,DecodeOutput,ImageRenderError*,CancelCheck={});
};
bool renderFromCache(GfxRenderer&,const std::string&,int,int,int,int,ImageBlock::PixelCachePolicy) { return false; }
struct ImageToFramebufferDecoder {
 bool fail = true;
 ImageRenderError failure=ImageRenderError::OutOfMemory;
 bool decodeToFramebuffer(const std::string&,GfxRenderer&,const RenderConfig& config) {
   if (config.error) *config.error = fail ? failure : ImageRenderError::None;
   return !fail;
 }
};
static ImageToFramebufferDecoder decoder;
struct ImageDecoderFactory { static ImageToFramebufferDecoder* getDecoder(const std::string&) { return &decoder; } };
''' + method(source, 'bool ImageBlock::renderInternal(') + r'''
int main() {
 ImageBlock block;
 GfxRenderer renderer;
 ImageRenderError error = ImageRenderError::None;
 assert(!block.renderInternal(renderer,0,0,ImageBlock::PixelCachePolicy::Stream,
                              DecodeOutput::FrameBufferAndCache,&error));
 assert(error == ImageRenderError::OutOfMemory && !remembered);
 decoder.failure = ImageRenderError::Failed;
 assert(!block.renderInternal(renderer,0,0,ImageBlock::PixelCachePolicy::Stream,
                             DecodeOutput::CacheOnly,&error));
 assert(!remembered); // Optional cache allocation/I/O failure must permit foreground retry.
 decoder.fail = false;
 assert(block.renderInternal(renderer,0,0,ImageBlock::PixelCachePolicy::Stream,
                             DecodeOutput::FrameBufferAndCache,&error));
 assert(error == ImageRenderError::None);
 CancelCheck cancelled{nullptr,[](void*){return true;}};
 assert(!block.renderInternal(renderer,0,0,ImageBlock::PixelCachePolicy::Stream,
                             DecodeOutput::FrameBufferAndCache,&error,cancelled));
 assert(error == ImageRenderError::Cancelled && !remembered);
 assert(!block.renderInternal(renderer,-1,0,ImageBlock::PixelCachePolicy::Stream,
                              DecodeOutput::FrameBufferAndCache,&error));
 assert(error == ImageRenderError::Failed);
}
''', (ROOT / 'lib/Memory',))

    def test_new_download_cleanup_is_consumed_once_and_exit_cleans_before_next_frame(self):
        source = (ROOT / 'src/activities/apps/airpage/AirPageActivity.cpp').read_text()
        start = source.index('      const bool cleanBeforeDisplay = imageNeedsClean_;')
        end = source.index('      if (rendered == ', start)
        consumption = source[start:end]
        run_cpp(r'''
#include <cassert>
#include <cstdint>
#include "CancelCheck.h"
#include <string>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define FREEINK_DEVICE_EEGO_A4 1
std::vector<char> events;
struct Activity { void onExit() { events.push_back('E'); } };
namespace airpage {
struct AirPageImageStore {
 enum class StageResult { Failed, Unchanged, PendingDisplay };
 static constexpr const char* kDownloadPartPath = "latest.part";
 StageResult result = StageResult::PendingDisplay;
 bool ensureDirectories() { return true; }
 StageResult stageDownloadedImage(uint64_t) { return result; }
 bool selectCurrent(int&) { return true; }
};
struct AirPageImageRenderer {
 static void resetSessionFailures() {}
 static void releaseSessionResources() { events.push_back('R'); }
 static void cleanScreen(int&) { events.push_back('C'); }
 static bool render(int&, int, int, bool clean) { if (clean) events.push_back('C'); return true; }
};
}
struct HttpDownloader {
 enum DownloadError { OK };
 static DownloadError downloadToFile(const std::string&, const char*) { return OK; }
};
struct AirPageActivity : Activity {
 enum class Screen { Qr, Image };
 enum class Notice { None, DownloadFailed };
 struct Connection {
   bool wifiConnected() { return true; }
   int handleWifiFailure() { return 0; }
   void stop() { events.push_back('S'); }
 } connection_;
 airpage::AirPageImageStore imageStore_;
 int renderer = 0, selectedImage_ = 0, fullScreen = 0;
 bool imageNeedsDisplay_ = false, imageNeedsClean_ = false;
 Screen screen_ = Screen::Qr;
 Notice notice_ = Notice::None;
 std::string downloadUrl_, legacyDownloadUrl_;
 uint64_t currentArchiveDateKey() { return 0; }
 void applyConnectionEvent(int) {}
 void closeRouting() {}
 void setAirPageScreen(Screen);
 void doFetch();
 void onExit();
 void display() {
''' + consumption + r'''
 (void)rendered;
 }
};
''' + method(source, 'void AirPageActivity::doFetch()') +
            method(source, 'void AirPageActivity::setAirPageScreen(') +
            method(source, 'void AirPageActivity::onExit()') + r'''
int main() {
 using Stage = airpage::AirPageImageStore::StageResult;
 for (auto stage : {Stage::Failed, Stage::Unchanged, Stage::PendingDisplay}) {
   AirPageActivity a;
   a.imageStore_.result = stage;
   a.doFetch();
   assert(a.imageNeedsClean_ == (stage == Stage::PendingDisplay));
   events.clear();
   a.display();
   a.display(); // A retry or popup redraw must not repeat the two fast clears.
   assert(events.size() == (stage == Stage::PendingDisplay ? 1 : 0));
   assert(!a.imageNeedsClean_);
 }
 AirPageActivity a;
 events.clear();
 a.onExit();
 events.push_back('N'); // ActivityManager renders the next activity after onExit returns.
 assert((events == std::vector<char>{'S', 'R', 'E', 'C', 'N'}));
 events.clear();
 a.screen_ = AirPageActivity::Screen::Image;
 a.setAirPageScreen(AirPageActivity::Screen::Qr);
 assert(a.screen_ == AirPageActivity::Screen::Qr && events.empty());
}
''', (ROOT / 'lib/Memory',))

if __name__ == '__main__':
    unittest.main()
