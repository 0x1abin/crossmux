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
''')

    def test_image_block_forwards_memory_failure_without_poisoning_retry(self):
        source = (ROOT / 'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
        decoder = (ROOT / 'lib/Epub/Epub/converters/ImageToFramebufferDecoder.h').read_text()
        types = decoder[decoder.index('enum class DecodeOutput'):decoder.index('class ImageToFramebufferDecoder')]
        run_cpp(r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
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
 bool ensureExtracted() { return true; }
 void renderPlaceholder(GfxRenderer&,int,int) {}
 bool bilinearScalingEnabled() { return false; }
 bool renderInternal(GfxRenderer&,int,int,PixelCachePolicy,DecodeOutput,ImageRenderError*);
};
bool renderFromCache(GfxRenderer&,const std::string&,int,int,int,int,ImageBlock::PixelCachePolicy) { return false; }
struct ImageToFramebufferDecoder {
 bool fail = true;
 bool decodeToFramebuffer(const std::string&,GfxRenderer&,const RenderConfig& config) {
   if (config.error) *config.error = fail ? ImageRenderError::OutOfMemory : ImageRenderError::None;
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
 decoder.fail = false;
 assert(block.renderInternal(renderer,0,0,ImageBlock::PixelCachePolicy::Stream,
                             DecodeOutput::FrameBufferAndCache,&error));
 assert(error == ImageRenderError::None);
 assert(!block.renderInternal(renderer,-1,0,ImageBlock::PixelCachePolicy::Stream,
                              DecodeOutput::FrameBufferAndCache,&error));
 assert(error == ImageRenderError::Failed);
}
''')


if __name__ == '__main__':
    unittest.main()
