"""Execute production fallback selection, setup and unload with small font/I/O seams."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class UiFontFallbackTest(unittest.TestCase):
    def test_usb_status_text_keeps_hebrew_and_cjk_without_sd(self):
        """The USB screen and the handoff popup draw with the flash CJK face.

        That face (notosans_cjk_12, behind NOTOSANS_12_FONT_ID) covers CJK and
        Latin but no Hebrew, and the SD-backed fallback is unavailable for the
        whole USB session because the USB host owns the card. The frame guard
        temporarily detaches SD fallbacks; the subsequent handoff popup selects
        its flash script face directly. Execute resolveTextFontId() against the
        real interval tables with no SD font registered, and check that each
        sample is drawn in a face that actually covers it.
        """
        from test_builtin_font_shrink import rows

        fonts = ROOT / 'lib/EpdFont/builtinFonts'

        def intervals(*names):
            found = []
            for name in names:
                found += [(first, last) for first, last, _ in rows((fonts / f'{name}.h').read_text(), 'Intervals')]
            return found

        # notosans_cjk_12.h embeds the shared common-interval table, so that one
        # file is the CJK face's real coverage.
        cjk_face = intervals('notosans_cjk_common_intervals')
        ui_face = intervals('ubuntu_12_regular')
        high_ui_face = intervals('notosans_16_regular')
        high_cjk_face = intervals('notosans_cjk_16')
        self.assertEqual(ui_face, intervals('ubuntu_12_bold'))
        self.assertEqual(high_ui_face, intervals('notosans_16_bold'))

        def table(name, values):
            body = ','.join(f'{{{first}u,{last}u}}' for first, last in values)
            return f'static const uint32_t {name}[][2]={{{body}}};\n'

        # The two call sites must keep the registration; the runtime check below
        # then proves what it buys. The USB screen holds it for the whole frame
        # (UsbFrameFontScope), so the header and the button hints are covered too,
        # and it restores every mapping it changed instead of leaving them cleared.
        usb = (ROOT / 'src/activities/network/UsbDriveActivity.cpp').read_text()
        self.assertIn('renderer_.setFallbackFont(NOTOSANS_12_FONT_ID, UI_12_FONT_ID);', usb)
        self.assertIn('renderer_.setFallbackFont(NOTOSANS_12_FONT_ID, statusSaved_[0], statusSaved_[1]);', usb)
        self.assertIn('renderer_.setFallbackFont(kFontIds[i], saved_[i][0], saved_[i][1]);', usb)
        main = (ROOT / 'src/main.cpp').read_text()
        popup = method(main, 'static Rect drawStorageHandoffPopup(')
        self.assertIn('renderer.setFallbackFont(NOTOSANS_12_FONT_ID, UI_12_FONT_ID);', popup)

        scope = method(usb, 'class UsbFrameFontScope') + ';'
        ids = (ROOT / 'src/SdCardFontSystem.h').read_text()
        cjk_ids = '\n'.join(re.findall(r'^inline constexpr int CJK_UI_(?:12|16)_FONT_ID =.*$', ids, re.M))
        rtl_id = re.search(r'^static constexpr int kRtlFontId =.*$', main, re.M).group(0)
        renderer = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        program = (
            r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <map>
#include <set>
#include <string>
#define LOG_ERR(...) ((void)0)
namespace BidiUtils { enum class BidiBaseDir { AUTO }; }
const char* resolveVisualText(const char* text, std::string&, BidiUtils::BidiBaseDir) { return text; }
#include "Utf8.h"
#include "Utf8.cpp"
#include "fontIds.h"
#include "components/Rect.h"

'''
            + table('kCjk', cjk_face)
            + table('kUi', ui_face)
            + table('kHighUi', high_ui_face)
            + table('kHighCjk', high_cjk_face)
            + cjk_ids + '\n#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI\n' + rtl_id + '\n#endif\n'
            + r'''
constexpr int kNotoSans12 = 10;  // NOTOSANS_12_FONT_ID: the flash CJK face
constexpr int kUi12 = 11;        // UI_12_FONT_ID: the flash Ubuntu face

std::set<uint32_t> coverage(const uint32_t (*table)[2], const size_t count) {
  std::set<uint32_t> set;
  for (size_t i = 0; i < count; ++i) {
    for (uint32_t cp = table[i][0]; cp <= table[i][1]; ++cp) set.insert(cp);
  }
  return set;
}

struct EpdFontFamily {
  enum Style { REGULAR, BOLD };
  std::set<uint32_t> coverage;
  bool hasCodepoint(uint32_t cp, Style) const { return coverage.count(cp) != 0; }
};
struct GfxRenderer {
  std::map<int, EpdFontFamily> fontMap;
  std::map<int, std::array<int, 2>> fallbackFontMap_;
  std::map<int, int> preferredFontMap_;
  std::set<int> sdFonts;
  std::map<int, int> ttfFonts;
  bool usedSdFont = false;
  int measuredFont = 0, drawnFont = 0, displays = 0;
  bool isSdCardFont(int id) const { return sdFonts.count(id) != 0; }
  const auto& getTtfFonts() const { return ttfFonts; }
  void setFallbackFont(int id, int fallback, int backup = 0) { fallbackFontMap_[id] = {fallback, backup}; }
  std::array<int, 2> getFallbackFont(int id) const {
    const auto found = fallbackFontMap_.find(id);
    return found == fallbackFontMap_.end() ? std::array<int, 2>{0, 0} : found->second;
  }
  int getScreenWidth() const { return 684; }
  int getScreenHeight() const { return 1216; }
  int getLineHeight(int) const { return 24; }
  int getTextWidth(int id, const char* text, EpdFontFamily::Style style) {
    measuredFont = resolveTextFontId(id, text, style);
    usedSdFont |= isSdCardFont(measuredFont);
    return 96;
  }
  void drawText(int id, int, int, const char* text, bool, EpdFontFamily::Style style) {
    drawnFont = resolveTextFontId(id, text, style);
    usedSdFont |= isSdCardFont(drawnFont);
  }
  template <typename... Args> void fillRect(Args...) {}
  template <typename... Args> void fillRoundedRect(Args...) {}
  void displayBuffer() { ++displays; }
  void ensureSdGlyphsResident(int, const char*, EpdFontFamily::Style, bool) const {}
  int resolveFontFamilyId(int) const;
  int resolveTextFontId(int, const char*, EpdFontFamily::Style = EpdFontFamily::REGULAR) const;
};
'''
            + method(renderer, 'int GfxRenderer::resolveFontFamilyId(')
            + method(renderer, 'int GfxRenderer::resolveTextFontId(')
            + scope
            + r'''
enum class Color { White, Black };
struct PopupMetrics {
  int popupMarginX = 12, popupMarginY = 10, popupFrameThickness = 2;
  bool popupTextBold = false, popupTextInverted = true;
  float popupTopOffsetRatio = 0.3f;
  int popupCornerRadius = 0, popupTextBaselineOffsetY = 0;
};
struct UITheme {
  PopupMetrics metrics;
  static UITheme& getInstance() { static UITheme theme; return theme; }
  const PopupMetrics& getMetrics() const { return metrics; }
};
'''
            + popup
            + r'''
bool faceCovers(const GfxRenderer& r, const int faceId, const char* text) {
  const auto face = r.fontMap.find(faceId);
  if (face == r.fontMap.end()) return false;
  const char* cursor = text;
  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&cursor)))) {
    if (!face->second.hasCodepoint(cp, EpdFontFamily::REGULAR)) return false;
  }
  return true;
}

int main() {
  GfxRenderer r;
  r.fontMap[kNotoSans12] = {coverage(kCjk, std::size(kCjk))};
  r.fontMap[kUi12] = {coverage(kUi, std::size(kUi))};
  // No SD face is registered: beginUsbDrive() detached the card, so anything
  // the resolver returns has to live in flash.

  const char* kHebrew = "\xD7\x9E\xD7\xA2\xD7\x91\xD7\x99\xD7\xA8";  // מעביר
  const char* kChinese = "\xE6\xB5\xB7\xE6\xA2\xA6";                  // 海梦
  const char* kLatin = "USB";

  // Without the registration the Hebrew run is handed to the CJK face, which
  // has no Hebrew: this is the reported defect.
  assert(r.resolveTextFontId(kNotoSans12, kHebrew) == kNotoSans12);
  assert(!faceCovers(r, kNotoSans12, kHebrew));

  // Both call sites register the UI face; the run now resolves to a flash face
  // that covers every one of its codepoints.
  r.fallbackFontMap_[kNotoSans12] = {kUi12, 0};
  assert(r.resolveTextFontId(kNotoSans12, kHebrew) == kUi12);
  assert(faceCovers(r, kUi12, kHebrew));

  // CJK and Latin keep the CJK face, so the fix does not trade one script for
  // the other.
  assert(r.resolveTextFontId(kNotoSans12, kChinese) == kNotoSans12);
  assert(faceCovers(r, kNotoSans12, kChinese));
  assert(r.resolveTextFontId(kNotoSans12, kLatin) == kNotoSans12);

  // A style other than REGULAR resolves through the same chain.
  assert(r.resolveTextFontId(kNotoSans12, kHebrew, EpdFontFamily::BOLD) == kUi12);

  // Exercise the complete popup after the frame guard restores the SD chain.
  GfxRenderer handoff;
  constexpr int sd = 9001;
  handoff.sdFonts.insert(sd);
  handoff.fontMap[NOTOSANS_12_FONT_ID] = {coverage(kCjk, std::size(kCjk))};
  handoff.fontMap[sd] = {coverage(kUi, std::size(kUi))};
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
  handoff.fontMap[UI_12_FONT_ID] = {coverage(kHighUi, std::size(kHighUi))};
  handoff.fontMap[CJK_UI_16_FONT_ID] = {coverage(kHighCjk, std::size(kHighCjk))};
  handoff.fontMap[CJK_UI_12_FONT_ID] = {coverage(kCjk, std::size(kCjk))};
  handoff.fontMap[kRtlFontId] = {coverage(kUi, std::size(kUi))};
  handoff.setFallbackFont(UI_12_FONT_ID, CJK_UI_16_FONT_ID, sd);
  handoff.setFallbackFont(CJK_UI_16_FONT_ID, CJK_UI_12_FONT_ID);
  handoff.setFallbackFont(CJK_UI_12_FONT_ID, kRtlFontId);
#else
  handoff.fontMap[UI_12_FONT_ID] = {coverage(kUi, std::size(kUi))};
  handoff.setFallbackFont(UI_12_FONT_ID, sd);
#endif
  const auto enteredUi = handoff.getFallbackFont(UI_12_FONT_ID);
  for (const bool bold : {false, true}) {
    for (const int radius : {0, 6}) {
      for (const char* text : {"טוען", "加载中", "Loading..."}) {
        {
          UsbFrameFontScope frame(handoff);
          assert(!handoff.isSdCardFont(handoff.resolveTextFontId(NOTOSANS_12_FONT_ID, text)));
        }
        assert(handoff.getFallbackFont(UI_12_FONT_ID) == enteredUi);
        UITheme::getInstance().metrics.popupTextBold = bold;
        UITheme::getInstance().metrics.popupCornerRadius = radius;
        handoff.usedSdFont = false;
        const int before = handoff.displays;
        const Rect rect = drawStorageHandoffPopup(handoff, text);
        assert(!handoff.usedSdFont);
        assert(handoff.measuredFont == handoff.drawnFont);
        assert(faceCovers(handoff, handoff.drawnFont, text));
        assert(handoff.displays == before + 1 && rect.width > 0 && rect.height > 0);
        assert(handoff.getFallbackFont(UI_12_FONT_ID) == enteredUi);
      }
    }
  }
}
'''
        )
        for defines in ((), ('CROSSMUX_UI_PROFILE_HIGH_DPI',)):
            run_cpp(program, include_dirs=(ROOT / 'lib/Utf8', ROOT / 'src'), defines=defines)

    def test_storage_handoff_waits_for_frame_until_restart(self):
        """The main-loop popup must wait for rendering and hold the lock through reset."""
        main = (ROOT / 'src/main.cpp').read_text()
        manager = (ROOT / 'src/activities/ActivityManager.cpp').read_text()
        program = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include "activities/RenderLock.h"
#include "components/Rect.h"

std::mutex eventMutex;
std::condition_variable event;
std::atomic<bool> attempted{false}, popupStarted{false}, frameActive{false}, overlap{false};
std::atomic<bool> popupLocked{false}, delayLocked{false}, usbLocked{false}, resetLocked{false};
thread_local bool isWorker = false, holdsRender = false;
struct { std::mutex renderingMutex; } activityManager;
constexpr int pdTRUE = 1, portMAX_DELAY = -1;
int xSemaphoreTake(std::mutex& mutex, int ticks) {
  if (isWorker) {
    std::lock_guard<std::mutex> signal(eventMutex);
    attempted = true;
    event.notify_one();
  }
  if (ticks == 0) {
    if (!mutex.try_lock()) return 0;
  } else {
    mutex.lock();
  }
  holdsRender = true;
  return pdTRUE;
}
void xSemaphoreGive(std::mutex& mutex) { holdsRender = false; mutex.unlock(); }
'''
        program += method(manager, 'RenderLock::RenderLock(Mode')
        program += method(manager, 'RenderLock::~RenderLock()')
        program += r'''
struct GfxRenderer {} renderer;
enum class SilentRebootTarget { Home };
bool deepSleepInProgress = false;
uint32_t silentRebootTarget = 0, silentRebootFontPointSize = 0, silentRebootPayload = 0, silentRebootMagic = 0;
constexpr uint32_t SILENT_REBOOT_LIGHT_ON = 1, SILENT_REBOOT_MAGIC = 2;
struct { bool isOn() const { return false; } } Frontlight;
void delay(unsigned) { delayLocked = holdsRender; }
void handoffUsbOtgToSerialJtag() { usbLocked = holdsRender; }
struct { void restart() { resetLocked = holdsRender; } } ESP;
#define LOG_DBG(...) ((void)0)
#define STR_LOADING_POPUP 1
#define tr(id) "loading"
static Rect drawStorageHandoffPopup(GfxRenderer&, const char*) {
  std::lock_guard<std::mutex> signal(eventMutex);
  overlap = frameActive.load();
  popupLocked = holdsRender;
  popupStarted = true;
  event.notify_one();
  return Rect{};
}
'''
        program += method(main, 'void restartToHomeAfterStorageHandoff()')
        program += r'''
int main() {
  std::optional<RenderLock> frame(std::in_place);
  frameActive = true;
  std::thread worker([] {
    isWorker = true;
    restartToHomeAfterStorageHandoff();
  });
  {
    std::unique_lock<std::mutex> wait(eventMutex);
    assert(event.wait_for(wait, std::chrono::seconds(5), [] {
      return attempted.load() || popupStarted.load();
    }));
  }
  if (attempted) {
    assert(!popupStarted);
    frameActive = false;
    frame.reset();
    worker.join();
  } else {
    worker.join();
    frameActive = false;
    frame.reset();
  }
  std::fprintf(stderr, "handoff overlap=%d popupLock=%d delayLock=%d usbLock=%d resetLock=%d\n",
               overlap.load(), popupLocked.load(), delayLocked.load(), usbLocked.load(), resetLocked.load());
  assert(!overlap && popupLocked && delayLocked && usbLocked && resetLocked);
}
'''
        run_cpp(program, include_dirs=(ROOT / 'src',))


    def test_high_dpi_whole_run_coverage(self):
        from test_builtin_font_shrink import rows
        directory = ROOT / 'lib/EpdFont/builtinFonts'
        for size in (12, 14, 16):
            source = (directory / f'notosans_cjk_{size}.h').read_text()
            if size == 12:
                source += (directory / 'notosans_cjk_common_intervals.h').read_text()
            coverage = {cp for first, last, _ in rows(source, 'Intervals')
                        for cp in range(first, last + 1)}
            self.assertIn(ord('海'), coverage)
            self.assertEqual(ord('梦') in coverage, size == 12)
        renderer = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        run_cpp(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <set>
#include <string>
#define LOG_ERR(...) ((void)0)
namespace BidiUtils { enum class BidiBaseDir { AUTO }; }
const void* measured=nullptr;
const char* resolveVisualText(const char* text,std::string& visual,BidiUtils::BidiBaseDir) {
  if (std::string(text)=="ا") { visual="ﺍ";return visual.c_str(); }
  return text;
}
#include "Utf8.h"
#include "Utf8.cpp"
struct EpdFontFamily {
  enum Style { REGULAR, BOLD };
  std::set<uint32_t> coverage;
  bool hasCodepoint(uint32_t cp, Style) const { return coverage.contains(cp); }
  void getTextDimensions(const char*,int* w,int* h,Style) const { measured=this;*w=20;*h=20; }
};
struct GfxRenderer {
  std::map<int,EpdFontFamily> fontMap;
  std::map<int,std::array<int,2>> fallbackFontMap_;
  std::map<int,int> preferredFontMap_;
  void ensureSdGlyphsResident(int,const char*,EpdFontFamily::Style,bool) const {}
  int getTextWidth(int,const char*,EpdFontFamily::Style=EpdFontFamily::REGULAR,
                   BidiUtils::BidiBaseDir=BidiUtils::BidiBaseDir::AUTO) const;
  int resolveFontFamilyId(int) const;
  int resolveTextFontId(int,const char*,EpdFontFamily::Style=EpdFontFamily::REGULAR) const;
};
''' + method(renderer, 'int GfxRenderer::resolveFontFamilyId(')
            + method(renderer, 'int GfxRenderer::resolveTextFontId(')
            + method(renderer, 'int GfxRenderer::getTextWidth(') + r'''
int main() {
  GfxRenderer r;
  r.fontMap[14]={{'A'}}; // NotoSans
  r.fontMap[114]={{'A',0x6d77}}; // CJK UI subset has 海, but not 梦
  r.fontMap[12]={{'A',0x6d77,0x68a6}}; // common CJK
  r.fontMap[212]={{'A',0x05d0,0xfe8d}}; // Ubuntu Hebrew / shaped Arabic
  r.fallbackFontMap_[14]={114,0};
  r.fallbackFontMap_[114]={12,0};
  r.fallbackFontMap_[12]={212,0};
  for(auto style : {EpdFontFamily::REGULAR,EpdFontFamily::BOLD}) {
    assert(r.resolveTextFontId(14,"海",style)==114);
    assert(r.resolveTextFontId(14,"梦海",style)==12);
    assert(r.resolveTextFontId(14,"A梦海",style)==12);
    assert(r.resolveTextFontId(14,"א",style)==212);
    assert(r.resolveTextFontId(14,"ﺍ",style)==212);
    assert(r.resolveTextFontId(14,"A",style)==14);
  }
  assert(r.getTextWidth(14,"ا")==20);
  assert(measured==&r.fontMap.at(212)); // selection sees shaped Arabic, as drawing does
  r.fontMap[414]={{'A',0x6d77,0x68a6,0x9f98}}; // complete SD fallback
  r.fallbackFontMap_[14]={114,414};
  assert(r.resolveTextFontId(14,"海")==114); // same-size CJK UI remains first
  assert(r.resolveTextFontId(14,"梦海")==414);
  assert(r.resolveTextFontId(14,"龘海")==414);
  r.fontMap.erase(414); // SD removal keeps common CJK / Ubuntu fallback
  r.fallbackFontMap_[14]={114,0};
  r.fontMap[314]={{0x6d77}}; // incomplete SD face
  r.preferredFontMap_[14]=314;
  r.fallbackFontMap_[14]={14,114};
  assert(r.resolveTextFontId(14,"A")==14);
  assert(r.resolveTextFontId(14,"梦海")==12);
  r.fallbackFontMap_[212]={14,0}; // bounded even if a chain cycles
  assert(r.resolveTextFontId(14,"龘")==314);
  r.preferredFontMap_.clear(); // unloading SD preserves fixed fallbacks
  assert(r.resolveTextFontId(14,"梦海")==12);
}
''', include_dirs=(ROOT / 'lib/Utf8',), defines=('CROSSMUX_UI_PROFILE_HIGH_DPI',))

    def test_high_dpi_reader_family_does_not_replace_ui(self):
        system = (ROOT / 'src/SdCardFontSystem.cpp').read_text()
        table = system[system.index('struct UiFontSize'):system.index('}  // namespace', system.index('struct UiFontSize'))]
        run_cpp(r'''
#define ENABLE_CHINESE_VERSION 1
#define CROSSMUX_UI_PROFILE_HIGH_DPI 1
#define SIMULATOR 1
#define FREEINK_DEVICE_READPICO 1
#define LOG_DBG(...) ((void)0)
#include <cassert>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
constexpr int SMALL_FONT_ID=1, UI_10_FONT_ID=2, UI_12_FONT_ID=3;
constexpr int CJK_UI_12_FONT_ID=6;
constexpr int READER_STATUS_FONT_ID=7, READER_ESTIMATE_FONT_ID=8;
constexpr int CJK_UI_14_FONT_ID=14, CJK_UI_16_FONT_ID=16;
struct GfxRenderer {
  std::vector<int> preferred, fallback;
  void clearPreferredFonts() { preferred.clear(); }
  void setPreferredFont(int id,int) { preferred.push_back(id); }
  void setFallbackFont(int id,int,int) { fallback.push_back(id); }
};
struct Family {};
struct Registry { Family family; const Family* findFamily(const std::string&) { return &family; } };
struct Manager {
  std::string family="Reader A";
  std::vector<int> sizes;
  const std::string& currentFamilyName() { return family; }
  int loadFamilyExtraSize(const Family&,GfxRenderer&,int size) { sizes.push_back(size); return 100+size; }
};
struct SdCardFontSystem {
  Registry registry_; Manager manager_;
  void setupUiFallbacks(GfxRenderer&);
};
''' + table + method(system, 'void SdCardFontSystem::setupUiFallbacks(') + r'''
int main() {
  SdCardFontSystem system; GfxRenderer renderer;
  for (const char* family : {"Reader A", "Reader B"}) {
    system.manager_.family=family; system.manager_.sizes.clear(); renderer.fallback.clear();
    system.setupUiFallbacks(renderer);
    assert((system.manager_.sizes==std::vector<int>{12,14,16,12,14}));
    assert((renderer.preferred==std::vector<int>{READER_STATUS_FONT_ID,READER_ESTIMATE_FONT_ID}));
    assert((renderer.fallback==std::vector<int>{SMALL_FONT_ID,UI_10_FONT_ID,UI_12_FONT_ID,READER_STATUS_FONT_ID,READER_ESTIMATE_FONT_ID}));
  }
}
''')

    def test_high_dpi_vector_ui_keeps_builtin_backup(self):
        system = (ROOT / 'src/SdCardFontSystem.cpp').read_text()
        table = system[system.index('struct UiFontSize'):system.index('}  // namespace', system.index('struct UiFontSize'))]
        run_cpp(r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
constexpr int SMALL_FONT_ID=1,UI_10_FONT_ID=2,UI_12_FONT_ID=3;
constexpr int CJK_UI_12_FONT_ID=12,CJK_UI_14_FONT_ID=14,CJK_UI_16_FONT_ID=16;
constexpr int READER_STATUS_FONT_ID=7,READER_ESTIMATE_FONT_ID=8;
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_INTERNAL=2,MALLOC_CAP_8BIT=4;
size_t heap_caps_get_largest_free_block(int) { return 1024*1024; }
size_t heap_caps_get_free_size(int) { return 1024*1024; }
struct TtfEpdFont {
  static inline int failSize=0;
  int size=0;
  bool load(int pt,bool,int,int) { size=pt;return pt!=failSize; }
  int family() const { return size; }
};
template<class T> auto makeUniqueNoThrow() { return std::make_unique<T>(); }
int computeTtfFontId(const char*,int pt) { return 100+pt; }
struct GfxRenderer {
  std::map<int,std::array<int,2>> fallback;
  std::map<int,int> fonts;
  const auto& getFontMap() const { return fonts; }
  void insertFont(int id,int size) { assert(fonts.emplace(id,size).second); }
  void registerTtfFont(int,TtfEpdFont*) {}
  void setFallbackFont(int id,int primary,int backup) { fallback[id]={primary,backup}; }
};
struct SdCardFontSystem {
  std::string ttfFamily_="SD";
  std::vector<std::unique_ptr<TtfEpdFont>> ttfUi_;
  std::vector<int> ttfUiIds_;
  void addTtfSources(TtfEpdFont&) {}
  void setupTtfUiFallbacks(GfxRenderer&);
};
''' + table + method(system, 'void SdCardFontSystem::setupTtfUiFallbacks(') + r'''
int main() {
  for(int fail : {0,16}) {
    TtfEpdFont::failSize=fail;
    SdCardFontSystem system; GfxRenderer r;
    r.fallback[UI_12_FONT_ID]={16,0};
    system.setupTtfUiFallbacks(r);
    assert((r.fallback[SMALL_FONT_ID]==std::array<int,2>{12,112}));
    assert((r.fallback[UI_10_FONT_ID]==std::array<int,2>{14,114}));
    assert((r.fallback[UI_12_FONT_ID]==std::array<int,2>{16,fail?0:116}));
    assert((r.fallback[READER_STATUS_FONT_ID]==std::array<int,2>{12,112}));
    assert((r.fallback[READER_ESTIMATE_FONT_ID]==std::array<int,2>{14,114}));
    assert(system.ttfUi_.size()==(fail?2U:3U));
    assert(system.ttfUi_.capacity()>=5 && system.ttfUiIds_.capacity()>=5);
  }
}
''', defines=('CROSSMUX_UI_PROFILE_HIGH_DPI','FREEINK_DEVICE_READPICO=1'))

    def test_high_dpi_vector_unload_preserves_fixed_ui(self):
        system = (ROOT / 'src/SdCardFontSystem.cpp').read_text()
        run_cpp(r'''
#include <cassert>
#include <string>
#include <vector>
struct GfxRenderer {
  std::vector<int> removed;
  bool fixedUiFallback=true;
  void unregisterTtfFont(int) {}
  void removeFont(int id) { removed.push_back(id); }
  void clearFallbackFonts() { fixedUiFallback=false; }
};
struct OwnedFont { bool freed=false; void reset() { freed=true; } };
struct SdCardFontSystem {
  std::string ttfFamily_="Reader";
  int ttfFontId_=114, ttfPointSize_=14;
  std::vector<int> ttfUiIds_{108,110}, ttfUi_{8,10};
  OwnedFont ttf_; bool sourcesFreed=false;
  void freeTtfSources() { sourcesFreed=true; }
  void unloadTtf(GfxRenderer&);
};
''' + method(system, 'void SdCardFontSystem::unloadTtf(') + r'''
int main() {
  GfxRenderer renderer; SdCardFontSystem system;
  system.unloadTtf(renderer);
  assert(renderer.fixedUiFallback);
  assert((renderer.removed==std::vector<int>{108,110,114}));
  assert(system.ttf_.freed && system.sourcesFreed && system.ttfFamily_.empty());
  assert(system.ttfUiIds_.empty() && system.ttfUi_.empty() && system.ttfFontId_==0);
  system.unloadTtf(renderer);
  assert(renderer.removed.size()==3);
}
''', defines=('CROSSMUX_UI_PROFILE_HIGH_DPI',))

    def test_missing_outline_geometry(self):
        renderer = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        program = r'''
#include <cassert>
#include "MissingGlyph.h"
namespace BidiUtils { bool isTransparentMark(uint32_t) { return false; } }
enum class TextRotation { None, Rotated90CW };
struct GfxRenderer {
  mutable int calls=0, x=0, y=0, w=0, h=0;
  mutable bool black=false;
  bool visible=true;
  bool glyphIntersectsStrip(int,int,int,int) const { return visible; }
  void drawRect(int px,int py,int pw,int ph,bool state) const {
    ++calls; x=px; y=py; w=pw; h=ph; black=state;
  }
};
template <TextRotation rotation = TextRotation::None>
''' + method(renderer, 'static void renderMissingGlyph(') + r'''
int main() {
  EpdFontData font{}; font.ascender=12;
  GfxRenderer r;
  renderMissingGlyph(r,font,0x1F9EA,20,30,true);
  assert(r.calls==1 && r.x==21 && r.y==21 && r.w==9 && r.h==9 && r.black);
  renderMissingGlyph(r,font,0x1F9EA,20,30,false,true);
  assert(r.calls==2 && r.x==20 && r.y==26 && r.w==5 && r.h==5 && !r.black);
  renderMissingGlyph<TextRotation::Rotated90CW>(r,font,0x1F9EA,20,30,true);
  assert(r.calls==3 && r.x==23 && r.y==21 && r.w==9 && r.h==9);
  for (uint32_t cp : {0x20,0xA0,0x200D,0xFE0F,0x301}) renderMissingGlyph(r,font,cp,20,30,true);
  assert(r.calls==3);
  r.visible=false;
  renderMissingGlyph(r,font,0x1F9EA,20,30,true);
  assert(r.calls==3);
}
'''
        with tempfile.TemporaryDirectory(prefix='missing-outline-') as directory:
            cpp = Path(directory) / 'check.cpp'
            exe = Path(directory) / 'check'
            cpp.write_text(program)
            subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'lib/EpdFont'), '-I', str(ROOT / 'lib/Utf8'),
                            '-I', str(ROOT / 'lib/MiniBidi'), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_fallback_lifecycle(self):
        renderer = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        header = (ROOT / 'lib/GfxRenderer/GfxRenderer.h').read_text()
        system = (ROOT / 'src/SdCardFontSystem.cpp').read_text()
        manager = (ROOT / 'lib/EpdFont/SdCardFontManager.cpp').read_text()
        table = system[system.index('struct UiFontSize'):system.index('}  // namespace', system.index('struct UiFontSize'))]
        table = table.replace('constexpr UiFontSize', '[[maybe_unused]] constexpr UiFontSize')
        program = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "Utf8.h"
#include "MissingGlyph.h"
constexpr int trackingBetween(const uint32_t leftCp, const uint32_t rightCp, const int8_t tracking) {
  const auto isSpace = [](const uint32_t cp) { return cp == ' ' || cp == 0xA0 || cp == 0x3000; };
  return leftCp == 0 || isSpace(leftCp) || isSpace(rightCp) ? 0 : tracking;
}
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
inline constexpr int SMALL_FONT_ID=1, UI_10_FONT_ID=2, UI_12_FONT_ID=3;
inline constexpr int CJK_UI_8_FONT_ID=4, CJK_UI_10_FONT_ID=5, CJK_UI_12_FONT_ID=6;
constexpr int READER_STATUS_FONT_ID=7, READER_ESTIMATE_FONT_ID=8;
namespace memory {
bool healthy = true;
bool psramHasHeadroom(size_t, size_t, size_t) { return healthy; }
}
namespace BidiUtils {
enum class BidiBaseDir { AUTO };
bool isTransparentMark(uint32_t) { return false; }
}
const char* resolveVisualText(const char* text, std::string&, BidiUtils::BidiBaseDir) { return text; }
const void* measured=nullptr;
const void* drawn=nullptr;
std::vector<int> drawnXs;
struct EpdFontFamily {
  enum Style { REGULAR=0, BOLD=1, SUP=16, SUB=32 };
  std::set<uint32_t> coverage;
  const EpdFontData* getData(Style) const { static EpdFontData data{}; data.ascender=12; return &data; }
  bool hasCodepoint(uint32_t cp, Style = REGULAR) const { return coverage.contains(cp); }
  void getTextDimensions(const char*, int* w, int* h, Style) const { measured=this; *w=8; *h=8; }
  const EpdGlyph* getGlyph(uint32_t cp, Style style, bool* replaced=nullptr) const {
    static EpdGlyph glyph{8,8,128,0,8,0,0};
    const bool found=hasCodepoint(cp,style);
    if (replaced) *replaced=!found;
    return found ? &glyph : nullptr;
  }
  const EpdGlyph* getGlyphMetrics(uint32_t cp, EpdGlyph&, Style style) const { return getGlyph(cp,style); }
  uint32_t applyLigatures(uint32_t cp, const char*&, Style) const { return cp; }
  int getKerning(uint32_t,uint32_t,Style) const { return 0; }
};
enum class TextRotation { None };
template<TextRotation> void renderCharImpl(const auto&, int, const EpdFontFamily& font, uint32_t,
                                         int x,int,bool,EpdFontFamily::Style,uint8_t) { drawn=&font; drawnXs.push_back(x); }
void renderCharScaled(const auto&, int, const EpdFontFamily& font, uint32_t,
                      int x,int,bool,EpdFontFamily::Style,uint8_t) { drawn=&font; drawnXs.push_back(x); }
struct FontCacheManager {
  bool isScanning() const { return false; }
  void recordText(const char*,int,EpdFontFamily::Style) {}
  void reportMissingChineseCodepoint(int,uint32_t) {}
};
struct SdCardFont {
  int batchPrewarms=0;
  void prewarm(const char* (*getter)(const void*, uint32_t), const void* ctx, uint32_t count,
               uint8_t, bool metadataOnly, bool loadKernLig) {
    ++batchPrewarms;
    assert(count==3 && !metadataOnly && !loadKernLig);
    assert(std::string(getter(ctx,0))=="一");
    assert(std::string(getter(ctx,1))=="α");
    assert(std::string(getter(ctx,2))=="…");
  }
};
struct GfxRenderer {
  int syntheticBoldPixels=0, renderMode=0;
  FontCacheManager* fontCacheManager_=nullptr;
  mutable int warmed=0;
  int getFontAscenderSize(int) const { return 8; }
  int getLineHeight(int) const { return 8; }
  void ensureSdGlyphsResident(int id,const char*,EpdFontFamily::Style,bool) const { warmed=id; }
  int getTextWidth(int,const char*,EpdFontFamily::Style=EpdFontFamily::REGULAR,
                   BidiUtils::BidiBaseDir=BidiUtils::BidiBaseDir::AUTO) const;
  void drawText(int,int,int,const char*,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR,
                BidiUtils::BidiBaseDir=BidiUtils::BidiBaseDir::AUTO,int8_t=0) const;
  void prewarmFallbackText(int,const char*,EpdFontFamily::Style=EpdFontFamily::REGULAR) const;
  std::map<int, EpdFontFamily> fontMap;
  std::map<int, std::array<int, 2>> fallbackFontMap_;
  std::map<int, int> preferredFontMap_;
  using TextGetter = const char* (*)(const void*, uint32_t);
  void prewarmFallbackText(int,TextGetter,const void*,uint32_t,EpdFontFamily::Style=EpdFontFamily::REGULAR) const;
  std::map<int, SdCardFont*> sdCardFonts_;
  std::map<int, int> sdCardFontScales_;
  const auto& getFontMap() const { return fontMap; }
  int resolveTextFontId(int, const char*, EpdFontFamily::Style = EpdFontFamily::REGULAR) const;
  int resolveFontFamilyId(int) const;
  void clearPreferredFonts() { preferredFontMap_.clear(); }
  void setPreferredFont(int id,int preferred) { preferredFontMap_[id]=preferred; }
  void clearSdCardFonts() { sdCardFonts_.clear(); sdCardFontScales_.clear(); }
''' + method(header, 'void setFallbackFont(') + '\n' + method(header, 'void removeFont(') + r'''
};
struct SdCardFontFileInfo { uint8_t pointSize; };
struct SdCardFontFamilyInfo {
  std::string name="test";
  std::map<uint8_t, SdCardFontFileInfo> files{{8,{8}},{10,{10}},{12,{12}},{14,{14}},{20,{20}}};
  const SdCardFontFileInfo* findFile(uint8_t size) const {
    auto it=files.find(size); return it == files.end() ? nullptr : &it->second;
  }
  const SdCardFontFileInfo* findNearestSize(uint8_t size) const { return findFile(size); }
};
struct SdCardFontManager {
  struct LoadedFont { SdCardFont* font; int fontId; uint8_t size; };
  std::vector<LoadedFont> loaded_;
  std::string loadedFamilyName_;
  uint8_t loadedPointSize_=0;
  int failSize=0, loads=0, readerCacheLoads=0;
  std::set<uint32_t> coverage{'A', 0x4E00, 0x3042, 0x03B1, 0x0627};
  const std::string& currentFamilyName() const { return loadedFamilyName_; }
  int getFontId(const std::string&) const;
  int loadFile(const SdCardFontFileInfo& file, const char*, GfxRenderer& r, bool flash, bool cache) {
    if (file.pointSize==failSize) return 0;
    assert(loaded_.size()<loaded_.capacity()); // every face fits the preallocated slots
    ++loads;
    readerCacheLoads += cache;
    assert(!flash);
    int id=100+file.pointSize;
    r.fontMap[id]={coverage};
    auto* font=new SdCardFont;
    r.sdCardFonts_[id]=font;
    loaded_.push_back({font, id, file.pointSize});
    return id;
  }
  bool loadFamily(const SdCardFontFamilyInfo&, GfxRenderer&, uint8_t, bool=false);
  int loadFamilyExtraSize(const SdCardFontFamilyInfo&, GfxRenderer&, uint8_t);
  void unloadAll(GfxRenderer&);
};
struct Registry {
  SdCardFontFamilyInfo family;
  const SdCardFontFamilyInfo* findFamily(const std::string& name) const {
    return name==family.name ? &family : nullptr;
  }
};
struct SdCardFontSystem {
  Registry registry_;
  SdCardFontManager manager_;
  void setupUiFallbacks(GfxRenderer&);
};
''' + table + '\n' + method(renderer, 'int GfxRenderer::resolveFontFamilyId(') + '\n'
        program += method(renderer, 'int GfxRenderer::resolveTextFontId(') + '\n'
        for name in ('bool SdCardFontManager::loadFamily(', 'int SdCardFontManager::loadFamilyExtraSize(',
                     'void SdCardFontManager::unloadAll(', 'int SdCardFontManager::getFontId('):
            program += method(manager, name) + '\n'
        for name in ('int GfxRenderer::getTextWidth(', 'void GfxRenderer::drawText(',
                     'void GfxRenderer::prewarmFallbackText(const int fontId, const TextGetter getter,',
                     'void GfxRenderer::prewarmFallbackText(const int fontId, const char* text,'):
            program += method(renderer, name) + '\n'
        program += method(system, 'void SdCardFontSystem::setupUiFallbacks(') + r'''
int main() {
  for (int scenario=0; scenario<8; ++scenario) {
    GfxRenderer r;
    for (int id=1; id<=3; ++id) {
      r.fontMap[id]={{'A'}};
      r.fontMap[id+3]={{'A',0x4E00}};
      r.setFallbackFont(id,id+3);
    }
    r.fontMap[READER_STATUS_FONT_ID]=r.fontMap[SMALL_FONT_ID];
    r.fontMap[READER_ESTIMATE_FONT_ID]=r.fontMap[UI_10_FONT_ID];
    r.setFallbackFont(READER_STATUS_FONT_ID,CJK_UI_8_FONT_ID);
    r.setFallbackFont(READER_ESTIMATE_FONT_ID,CJK_UI_10_FONT_ID);
    SdCardFontSystem s;
    if (scenario==1) s.registry_.family.files.erase(10);
    if (scenario==2) s.manager_.failSize=8;
    if (scenario==3) s.manager_.coverage={'A'};
    if (scenario==6) s.manager_.coverage={'A',0x03B1};
    memory::healthy = scenario!=4;
    const int readerSize = scenario==7 ? 20 : scenario==5 ? 12 : 14;
    assert(s.manager_.loadFamily(s.registry_.family, r, readerSize));
    const auto capacity=s.manager_.loaded_.capacity();
    assert(capacity==(EXPECT_READPICO ? 5 : 4));
    s.setupUiFallbacks(r);
    assert(s.manager_.loaded_.capacity()==capacity);
    constexpr bool enabled = EXPECT_ENABLED;
    if constexpr (EXPECT_READPICO) {
#ifdef SIMULATOR
      const bool active=true;
#else
      const bool active=memory::healthy;
#endif
      const int extraSizes=scenario==7 ? 4 : (scenario==1 || scenario==2) ? 2 : 3;
      assert(s.manager_.loads==(active ? 1+extraSizes : 1));
      assert(s.manager_.readerCacheLoads==1); // footer faces have no PSRAM glyph arena
      for (int id=1; id<=3; ++id) {
        const int chosen=active ? (id==3 ? 114 : 112) : id;
        assert(r.resolveFontFamilyId(id)==chosen);
        measured=drawn=nullptr;
        r.getTextWidth(id,"A");
        r.drawText(id,0,0,"A");
        assert(measured==&r.fontMap.at(chosen) && drawn==measured);
      }
      const int loads=s.manager_.loads;
      s.setupUiFallbacks(r);
      assert(s.manager_.loads==loads); // already-resident 8/10 pt faces are reused
      for (int id : {READER_STATUS_FONT_ID,READER_ESTIMATE_FONT_ID}) {
        const int pt=id==READER_STATUS_FONT_ID ? 8 : 10;
        const int fallback=id==READER_STATUS_FONT_ID ? CJK_UI_8_FONT_ID : CJK_UI_10_FONT_ID;
        const bool loaded=active && !(scenario==1 && pt==10) && !(scenario==2 && pt==8);
        const int chosen=loaded ? 100+pt : id;
        assert(r.resolveFontFamilyId(id)==chosen);
        for (const char* text : {"A","一"}) {
          const int resolved=r.resolveTextFontId(id,text);
          const int expected=std::string(text)=="一" && (!loaded || scenario==3 || scenario==6) ? fallback : chosen;
          assert(resolved==expected);
          measured=drawn=nullptr;
          r.getTextWidth(id,text);
          r.drawText(id,0,0,text);
          assert(measured==&r.fontMap.at(expected) && drawn==measured);
        }
        if (loaded) {
          r.fontMap.at(chosen).coverage.erase('A');
          assert(r.resolveTextFontId(id,"A")==id);
        }
      }
      if (active) {
        r.fontMap.at(112).coverage.erase('A');
        assert(r.resolveTextFontId(2,"A")==2); // SD faces may omit ASCII too
      }
      s.manager_.unloadAll(r);
      assert(r.preferredFontMap_.empty());
      for (int id=1; id<=3; ++id) assert(r.resolveFontFamilyId(id)==id);
      assert(r.resolveFontFamilyId(READER_STATUS_FONT_ID)==READER_STATUS_FONT_ID);
      assert(r.resolveTextFontId(READER_STATUS_FONT_ID,"一")==CJK_UI_8_FONT_ID);
      assert(r.resolveFontFamilyId(READER_ESTIMATE_FONT_ID)==READER_ESTIMATE_FONT_ID);
      memory::healthy=true;
      s.registry_.family.files.erase(12);
      assert(s.manager_.loadFamily(s.registry_.family,r,14));
      s.setupUiFallbacks(r);
      assert(r.resolveFontFamilyId(1)==1 && r.resolveFontFamilyId(2)==2);
      assert(r.resolveFontFamilyId(3)==114);
      assert(r.resolveFontFamilyId(READER_STATUS_FONT_ID)==(scenario==2 ? READER_STATUS_FONT_ID : 108));
      assert(r.resolveFontFamilyId(READER_ESTIMATE_FONT_ID)==(scenario==1 ? READER_ESTIMATE_FONT_ID : 110));
      s.manager_.unloadAll(r);
      s.registry_.family.name="other";
      s.registry_.family.files.erase(8);
      assert(s.manager_.loadFamily(s.registry_.family,r,14));
      s.setupUiFallbacks(r);
      assert(r.resolveFontFamilyId(READER_STATUS_FONT_ID)==READER_STATUS_FONT_ID);
      assert(r.resolveTextFontId(READER_STATUS_FONT_ID,"一")==CJK_UI_8_FONT_ID);
      s.manager_.unloadAll(r);
      continue;
    }
    const bool active = enabled && scenario!=3 && scenario!=4;
    assert(s.manager_.loads == (active ? (scenario==0 || scenario==6 || scenario==7 ? 4 : 3) : 1));
    assert(s.manager_.readerCacheLoads==1);
    assert(r.resolveTextFontId(1,"A")==1);
    assert(r.resolveTextFontId(1,"")==1);
    assert(r.resolveTextFontId(1,nullptr)==1);
    assert(r.resolveTextFontId(99,"一")==99);
    assert(r.resolveTextFontId(1,"龘")==1); // no candidate covers this glyph
    // A built-in fallback on the first row must not hide an SD fallback later.
    const char* titles[]={"一","α"};
    r.prewarmFallbackText(1, [](const void* ctx,uint32_t i) {
      return static_cast<const char* const*>(ctx)[i];
    }, titles, 2);
    if (active && scenario!=2) assert(r.sdCardFonts_.at(108)->batchPrewarms==1);
    for (int id=1; id<=3; ++id) {
      const int pt= id==1 ? 8 : id==2 ? 10 : 12;
      const bool loaded = active && !(scenario==1 && pt==10) && !(scenario==2 && pt==8);
      assert(r.resolveTextFontId(id,"一") == (loaded && scenario!=6 ? 100+pt : id+3));
      assert(r.resolveTextFontId(id,"α") == (loaded ? 100+pt : id));
      assert(r.resolveTextFontId(id,"ا") == (loaded && scenario!=6 ? 100+pt : id));
      assert(r.resolveTextFontId(id,"あ") == (loaded && scenario!=6 ? 100+pt : id));
      for (const char* text : {"A", "一", "α", "ا", "あ", "龘"}) {
        measured=drawn=nullptr;
        r.warmed=0;
        const int chosen=r.resolveTextFontId(id,text);
        assert(r.getTextWidth(id,text)==8);
        r.drawText(id,0,0,text);
        r.prewarmFallbackText(id,text);
        assert(measured==&r.fontMap.at(chosen) && drawn==measured);
        assert(r.warmed==(chosen==id ? 0 : chosen));
      }
      // SD font without Han must still leave the embedded Chinese face usable.
      if (loaded) {
        r.fontMap[100+pt].coverage.erase(0x4E00);
        assert(r.resolveTextFontId(id,"一")==id+3);
      }
    }
    drawnXs.clear();
    r.drawText(1,0,0,"A龘龘A");
    assert((drawnXs==std::vector<int>{0,8,19,30}));
    drawnXs.clear();
    r.drawText(1,0,0,"A龘龘A",true,EpdFontFamily::SUP);
    assert((drawnXs==std::vector<int>{0,4,10,16}));
    s.manager_.unloadAll(r);
    assert(r.sdCardFonts_.empty());
    for (int id=1; id<=3; ++id) assert(r.resolveTextFontId(id,"一")==id+3);
    // Switching to a Latin-only family must not lose the embedded mapping.
    s.manager_.coverage={'A'};
    assert(s.manager_.loadFamily(s.registry_.family,r,14));
    s.setupUiFallbacks(r);
    assert(r.resolveTextFontId(1,"一")==4);
    s.manager_.failSize=14;
    assert(!s.manager_.loadFamily(s.registry_.family,r,14));
    assert(r.resolveTextFontId(1,"一")==4);
    s.manager_.unloadAll(r);
    // The primary may cover the representative probe but lack other script glyphs.
    memory::healthy=true;
    s.manager_.failSize=0;
    s.manager_.coverage={0x03B1,0x03B2};
    for (int id=1; id<=3; ++id) r.fontMap[id].coverage.insert(0x03B1);
    assert(s.manager_.loadFamily(s.registry_.family,r,14));
    s.setupUiFallbacks(r);
    assert(r.resolveTextFontId(1,"α")==1);
    assert(r.resolveTextFontId(1,"β")== (enabled ? 108 : 1));
    s.manager_.unloadAll(r);
    r.removeFont(4);
    assert(r.resolveTextFontId(1,"一")==1);
    r.removeFont(2);
    assert(!r.fallbackFontMap_.contains(2));
  }
}
'''
        main = (ROOT / 'src/main.cpp').read_text()
        registration = next(line.strip() for line in main.splitlines()
                            if 'setFallbackFont(NOTOSANS_18_FONT_ID,' in line)
        program = program.replace('constexpr int SMALL_FONT_ID=1,',
                                  'constexpr int NOTOSANS_18_FONT_ID=9;\nconstexpr int SMALL_FONT_ID=1,')
        program = program.replace('    SdCardFontSystem s;', '''
    r.fontMap[NOTOSANS_18_FONT_ID]={{'A'}};
    ''' + registration.replace('renderer.', 'r.') + '''
    assert(r.resolveTextFontId(NOTOSANS_18_FONT_ID,"A")==NOTOSANS_18_FONT_ID);
    assert(r.resolveTextFontId(NOTOSANS_18_FONT_ID,"一")==CJK_UI_12_FONT_ID);
    measured=drawn=nullptr;
    r.getTextWidth(NOTOSANS_18_FONT_ID,"一");
    r.drawText(NOTOSANS_18_FONT_ID,0,0,"一");
    assert(measured==&r.fontMap.at(CJK_UI_12_FONT_ID) && drawn==measured);
    SdCardFontSystem s;''')
        configurations = (
            ('readpico', ['CONFIG_IDF_TARGET_ESP32S3=1', 'BOARD_HAS_PSRAM', 'FREEINK_DEVICE_READPICO=1'], True),
            ('readpico_simulator', ['SIMULATOR', 'CROSSPOINT_EMULATED=1', 'FREEINK_DEVICE_READPICO=1'], True),
            ('s3', ['CONFIG_IDF_TARGET_ESP32S3=1', 'BOARD_HAS_PSRAM'], True),
            ('c3', ['CONFIG_IDF_TARGET_ESP32C3=1'], False),
            ('s3_no_psram', ['CONFIG_IDF_TARGET_ESP32S3=1'], False),
            ('simulator', ['CONFIG_IDF_TARGET_ESP32S3=1', 'BOARD_HAS_PSRAM', 'SIMULATOR'], True),
            ('emulated', ['CONFIG_IDF_TARGET_ESP32S3=1', 'BOARD_HAS_PSRAM', 'CROSSPOINT_EMULATED'], True),
        )
        with tempfile.TemporaryDirectory(prefix='ui-fallback-') as directory:
            cpp = Path(directory) / 'check.cpp'
            exe = Path(directory) / 'check'
            cpp.write_text(program)
            for name, defines, enabled in configurations:
                with self.subTest(target=name):
                    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                    '-DENABLE_CHINESE_VERSION=1', f'-DEXPECT_ENABLED={int(enabled)}',
                                    f'-DEXPECT_READPICO={int(name.startswith("readpico"))}',
                                    *[f'-D{value}' for value in defines],
                                    '-I', str(ROOT / 'lib/Utf8'), '-I', str(ROOT / 'lib/EpdFont'),
                                    '-I', str(ROOT / 'lib/MiniBidi'), str(cpp),
                                    str(ROOT / 'lib/Utf8/Utf8.cpp'), '-o', str(exe)], check=True)
                    subprocess.run([str(exe)], check=True)



if __name__ == '__main__':
    unittest.main()
