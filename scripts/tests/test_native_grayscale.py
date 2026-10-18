"""Compile real renderer/decoders with a recording HAL; no physical display required."""
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
from native_reader_gray_contract import prepare as prepare_reader_contract


def main():
    ROOT = Path(__file__).resolve().parents[2]
    SIM = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/test/_deps/crosspoint_simulator-src'
    CXX = sys.argv[2] if len(sys.argv) > 2 else 'c++'
    jpeg_candidates = sorted(ROOT.glob('.pio/libdeps/*/JPEGDEC/src/JPEGDEC.cpp'))
    JPEG = (Path(sys.argv[3]) if len(sys.argv) > 3 else
            jpeg_candidates[0].parent if jpeg_candidates else ROOT / 'build/test/_deps/jpegdec-src/src')
    if not (JPEG / 'JPEGDEC.cpp').is_file():
        raise SystemExit('Configure host tests or build a firmware environment to provide the existing JPEGDEC dependency.')

    with tempfile.TemporaryDirectory(prefix='native-gray-') as temp:
        work = Path(temp)
        # Keep the simulator's complete HAL declarations so the entire production
        # renderer compiles; only the native transaction/geometry seams are replaced.
        hal = (SIM / 'src/HalDisplay.h').read_text().replace('EInkDisplay einkDisplay;', '')
        hal = hal.replace('EInkDisplay::DISPLAY_WIDTH', '128').replace('EInkDisplay::DISPLAY_HEIGHT', '32')
        if 'getGrayscaleLevels()' not in hal:
            hal = hal.replace('  void begin();', '''  uint8_t getGrayscaleLevels() const;
          uint8_t* beginGrayscale16();
          bool commitGrayscale16();
          void cancelGrayscale16();
          void begin();''')
        (work / 'HalDisplay.h').write_text(hal)
        (work / 'Logging.h').write_text('#pragma once\n#define LOG_DBG(...) ((void)0)\n#define LOG_ERR(...) ((void)0)\n#define LOG_INF(...) ((void)0)\n')
        settings = """#pragma once
    #include <cstdint>
    class CrossPointSettings {
     public:
      enum SLEEP_SCREEN_MODE : uint8_t { LIGHT, CUSTOM, TRANSPARENT, SLEEP_SCREEN_MODE_COUNT };
      enum class SLEEP_SCREEN_COVER_MODE { FIT, CROP };
      enum class SLEEP_SCREEN_COVER_FILTER { NO_FILTER, BLACK_AND_WHITE, INVERTED_BLACK_AND_WHITE };
      SLEEP_SCREEN_COVER_MODE sleepScreenCoverMode = SLEEP_SCREEN_COVER_MODE::FIT;
      SLEEP_SCREEN_COVER_FILTER sleepScreenCoverFilter = SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
      uint8_t sleepScreen = LIGHT;
      int getRefreshFrequency()const{return 7;}
      int failedSaves = 0;
      bool saveToFile() { if (failedSaves > 0) { --failedSaves; return false; } return true; }
      static CrossPointSettings& getInstance() { static CrossPointSettings value; return value; }
    };
    #define SETTINGS CrossPointSettings::getInstance()
    """
        (work / 'CrossPointSettings.h').write_text(settings)
        activity = (ROOT / 'src/activities/apps/airpage/AirPageActivity.cpp').read_text()
        qr = activity[activity.index('  char displayParams[48];'):activity.index('  uploadUrl_ += displayParams;')]
        (work / 'QrParams.h').write_text('std::string qrParams(GfxRenderer& renderer) {\n' + qr +
                                       '  return displayParams;\n}\n')
        sleep = (ROOT / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()
        placement = sleep[sleep.index('// Metalio:'):sleep.index('// Kept separate')]
        placement += sleep[sleep.index('struct BitmapPlacement {'):sleep.index('struct OverlayBmpInfo {')]
        placement += sleep[sleep.index('BitmapPlacement calculateBitmapPlacement('):sleep.index('bool parseOverlayBmpHeader(')]
        custom = sleep[sleep.index('void SleepActivity::renderCustomSleepScreen()'):sleep.index('// Sleep screens paint')]
        bitmap_sleep = sleep[sleep.index('void SleepActivity::renderBitmapSleepScreen('):sleep.index('bool SleepActivity::renderSleepOverlayFile(')]
        probe = """#include "util/SleepCover.h"
    #include <cmath>
    #include <Logging.h>
    enum class SleepRecentKind { Standard };
    inline bool selectRandomSleepFile(const char*, SleepRecentKind, std::string&) { return false; }
    class SleepProbe {
     public:
      explicit SleepProbe(GfxRenderer& value) : renderer(value) {}
      GfxRenderer& renderer;
      mutable int defaults = 0;
      void renderDefaultSleepScreen() const { ++defaults; }
      void renderCustomSleepScreen() const;
      void renderBitmapSleepScreen(const Bitmap&, bool = false, bool = false) const;
    };
    """
        (work / 'SleepProbe.h').write_text(probe + placement +
            (custom + bitmap_sleep).replace('SleepActivity::', 'SleepProbe::'))
        # Compile the new viewer's JPEG and publication methods with real renderer/decoder/storage.
        from test_reading_ui_regressions import method
        viewer = (ROOT / 'src/activities/util/ImageViewerActivity.cpp').read_text()
        constants = viewer[viewer.index('constexpr const char* IMAGE_PREVIEW_PATH'):viewer.index('Rect sleepCoverActionRect')]
        methods = '\n'.join(method(viewer, name) for name in (
            'RenderConfig fitImage(', 'bool ImageViewerActivity::renderJpeg(',
            'bool ImageViewerActivity::prepareJpegSleepCover(', 'void ImageViewerActivity::doSetSleepCover('))
        viewer_probe = r"""
#include <Memory.h>
#include <FsHelpers.h>
#include "components/Rect.h"
constexpr int STR_BACK=0, STR_SET_SLEEP_COVER=1, STR_LOADING_POPUP=2, STR_DONE=3, STR_FAILED_LOWER=4;
inline const char* tr(int) { return ""; }
struct ViewerGui {
  int hints=0,actions=0;
  void drawPopup(GfxRenderer&, const char*) {}
  void drawButtonHints(GfxRenderer&, const char*,const char*,const char*,const char*) { ++hints; }
  void drawActionButton(GfxRenderer&,const Rect&,const char*) { ++actions; }
} viewerGui;
#define GUI viewerGui
inline Rect sleepCoverActionRect(const GfxRenderer& renderer) { return Rect(0,0,renderer.getScreenWidth(),8); }
struct PngToBmpConverter {
  static bool pngFileToTransparentBmpFile(const char*,const char*,bool) { return false; }
};
struct ViewerInput {
  struct Labels { const char* btn1;const char* btn2;const char* btn3;const char* btn4; };
  bool touch=true;
  Labels mapLabels(const char* a,const char* b,const char* c,const char* d) const { return {a,b,c,d}; }
  bool hasTouch() const { return touch; }
};
class ImageViewerProbe {
 public:
  explicit ImageViewerProbe(GfxRenderer& renderer) : renderer(renderer) {}
  GfxRenderer& renderer;
  ViewerInput mappedInput;
  std::string filePath="/ramp.jpg";
  std::vector<std::string> siblingImages;
  int currentImageIndex=-1;
  bool imageReady=true;
  bool renderJpeg();
  bool prepareJpegSleepCover();
  void doSetSleepCover(const char*,bool);
};
"""
        (work / 'ViewerProbe.h').write_text(viewer_probe + constants + methods.replace('ImageViewerActivity::','ImageViewerProbe::'))
        reader_driver = prepare_reader_contract(work, ROOT)
        includes = [work, work/'reader-driver/include', JPEG, SIM / 'src', ROOT / 'lib/hal', ROOT / 'lib/GfxRenderer', ROOT / 'lib/EpdFont',
                    ROOT / 'lib/Epub', ROOT / 'lib/Memory', ROOT / 'lib/Utf8', ROOT / 'lib/MiniBidi',
                    ROOT / 'lib/InflateReader', ROOT / 'lib/ZipFile', ROOT / 'lib/Serialization',
                    ROOT / 'lib/uzlib/src', ROOT / 'lib/FsHelpers', ROOT / 'lib/JpegToBmpConverter', ROOT / 'src', ROOT / 'src/activities/apps/airpage']
        sources = [ROOT / name for name in (
            'test/native_grayscale/NativeGrayscaleTest.cpp', 'src/activities/reader/ReaderSession.cpp', 'lib/GfxRenderer/GfxRenderer.cpp',
            'lib/GfxRenderer/Bitmap.cpp', 'lib/GfxRenderer/BitmapHelpers.cpp',
            'lib/Epub/Epub/converters/JpegToFramebufferConverter.cpp',
            'lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp',
            'lib/JpegToBmpConverter/JpegToBmpConverter.cpp', 'lib/FsHelpers/FsHelpers.cpp', 'lib/Memory/BuildScratch.cpp',
            'lib/Epub/Epub/converters/ImageDimsProbe.cpp',
            'src/activities/apps/airpage/AirPageImageRenderer.cpp',
            'src/activities/apps/airpage/AirPageImageStore.cpp',
            'src/activities/apps/airpage/AirPageWallpaper.cpp', 'src/util/SleepCover.cpp')]
        sources += [reader_driver, JPEG / 'JPEGDEC.cpp', SIM / 'src/HalStorage.cpp']
        flags = ['-std=c++20', '-O1', '-ffunction-sections', '-fdata-sections', '-DCROSSPOINT_EMULATED=1',
                 '-DDESTRUCTOR_CLOSES_FILE=1', '-D__LINUX__=1', '-DSIMULATOR=1']
        flags += ['-DFREEINK_DRIVER_EPDIY_LCD=1','-DFREEINK_EPDIY_LCD_CONFIG=readerContractConfig']
        if platform.system()=='Linux':
            flags += ['-DREADER_TEST_WRAP_MALLOC=1','-Wl,--wrap=malloc','-Wl,--wrap=free']
        flags += ['-I' + str(p) for p in includes]
        flags += ['-Wl,-dead_strip' if platform.system() == 'Darwin' else '-Wl,--gc-sections']
        binary = work / 'test'
        subprocess.run([CXX, *flags, *map(str, sources), '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(ROOT / 'test/native_grayscale/gray16.jpg'), str(work)], check=True)

    print('Native BMP/JPEG, orientations, cancellation, OOM, AirPage and sleep/wallpaper transactions passed')


if __name__ == "__main__":
    main()
