"""Execute production OTA/SD writers and progress pages with a slow panel double."""
from pathlib import Path
import re
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class FirmwareUpdateRefreshTest(unittest.TestCase):
    def test_flash_ordering_failure_paths_and_complete_frames(self):
        ota = (ROOT / 'src/activities/settings/OtaUpdateActivity.cpp').read_text()
        sd = (ROOT / 'src/activities/settings/SdFirmwareUpdateActivity.cpp').read_text()
        updater = (ROOT / 'src/network/OtaUpdater.cpp').read_text()
        flasher = (ROOT / 'src/network/FirmwareFlasher.cpp').read_text()
        theme = (ROOT / 'src/components/themes/BaseTheme.cpp').read_text()
        production = '\n'.join((
            method(theme, 'int BaseTheme::measureProgressBarHeight('),
            method(theme, 'int BaseTheme::drawProgressBar('),
            method(ota, 'void OtaUpdateActivity::render('),
            method(sd, 'void SdFirmwareUpdateActivity::render('),
            method(ota, 'void OtaUpdateActivity::runUpdateInstall('),
            method(sd, 'void SdFirmwareUpdateActivity::onConfirmationResult('),
            method(sd, 'void SdFirmwareUpdateActivity::performUpdate('),
            method(updater, 'OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate('),
            'namespace firmware_flash {\n' + method(flasher, 'Result flashFromSdPath(') + '\n}',
        ))
        keys = sorted(set(re.findall(r'\bSTR_[A-Z_]+\b', production)))
        constants = re.search(r'constexpr unsigned int PROGRESS_REFRESH_STEP_PERCENT = \d+;', ota).group()
        sd_constants = re.search(r'constexpr unsigned int PROGRESS_REFRESH_STEP_PERCENT = \d+;', sd).group()
        self.assertEqual(constants, sd_constants)
        # Reuse the production SD chunk/erase sizes, not a rewritten writer loop.
        flash_constants = '\n'.join(re.findall(r'constexpr size_t (?:SEC|BLK|CHUNK) = [^;]+;', flasher))
        program = HARNESS + '\nenum { ' + ', '.join(keys) + ' };\n' + constants + '\n'
        program += 'namespace firmware_flash {\n' + flash_constants + '\n}\n'
        program += production + r'''
int main() {
  for (bool tabs : {false, true}) {
    GUI.tabs = tabs;
    for (Failure failure : {Failure::None, Failure::Download, Failure::Write,
                            Failure::Verify, Failure::Switch, Failure::WrongChip,
                            Failure::WrongBoard}) {
      reset(failure);
      OtaUpdateActivity activity;
      activity.updater.totalSize = 1000;
      activity.runUpdateInstall();
      assert(!refreshPending && !locked && activity.renderer.clears == activity.renderer.frames);
      if (failure == Failure::None) {
        assert(activity.state == OtaUpdateActivity::State::ShuttingDown && switches == 1);
        assert((activity.renderer.percentages == std::vector<int>{0,10,20,55,65,99,100}));
        assert(activity.renderer.text.back() == tr(STR_AUTO_RESTART_HINT));
      } else {
        assert(activity.state == OtaUpdateActivity::State::Failed && switches == 0 && restarts == 0);
        assert(activity.deferred && fontReloads == 1);
        const int before = activity.renderer.frames;
        activity.requestUpdateAndWait();
        assert(activity.renderer.frames == before + 1);
        assert(activity.failedDetail == ((failure == Failure::WrongChip || failure == Failure::WrongBoard)
                                        ? tr(STR_FIRMWARE_WRONG_DEVICE) : nullptr));
      }
      if (failure == Failure::Download || failure == Failure::Write ||
          failure == Failure::WrongChip || failure == Failure::WrongBoard) assert(aborts == 1);
    }

    for (Failure failure : {Failure::None, Failure::Read, Failure::Erase,
                            Failure::Write, Failure::Verify, Failure::Switch}) {
      reset(failure);
      SdFirmwareUpdateActivity activity;
      activity.onConfirmationResult(ActivityResult{});
      assert(!refreshPending && !locked && activity.renderer.clears == activity.renderer.frames);
      if (failure == Failure::None) {
        assert(activity.state == SdFirmwareUpdateActivity::State::SUCCESS && switches == 1 && restarts == 1);
        assert((activity.renderer.percentages == std::vector<int>{0,10,20,30,40,50,60,70,80,90,100}));
      } else {
        assert(activity.state == SdFirmwareUpdateActivity::State::FAILED && switches == 0 && restarts == 0);
        assert(activity.deferred && fontReloads == 1);
        activity.requestUpdateAndWait();
      }
    }

    // Repeated external render requests must also submit complete frames. The
    // live updater counters deliberately disagree with the protected snapshot.
    reset(Failure::None);
    OtaUpdateActivity activity;
    activity.state = OtaUpdateActivity::State::UpdateInProgress;
    activity.updater.processedSize = 999;
    activity.updater.totalSize = 1000;
    for (auto [done, total, percent] : {Sample{0,0,0}, Sample{19,100,19},
                                     Sample{110,100,100}, Sample{UINT32_MAX,UINT32_MAX,100}}) {
      activity.progressBytes = done;
      activity.progressTotalBytes = total;
      for (int repeat = 0; repeat < 2; ++repeat) {
        activity.requestUpdateAndWait();
        assert(activity.renderer.percentages.back() == percent);
        assert(activity.renderer.text.back() == std::to_string(done) + " / " + std::to_string(total));
        assert(activity.renderer.clears == activity.renderer.frames);
      }
    }
    SdFirmwareUpdateActivity sdActivity;
    sdActivity.state = SdFirmwareUpdateActivity::State::UPDATING;
    for (auto [done, total, percent] : {Sample{0,0,0}, Sample{19,100,19}, Sample{110,100,100},
                                     Sample{UINT32_MAX,UINT32_MAX,100}}) {
      sdActivity.writtenBytes = done;
      sdActivity.firmwareSize = total;
      for (int repeat = 0; repeat < 2; ++repeat) {
        sdActivity.requestUpdateAndWait();
        assert(sdActivity.renderer.percentages.back() == percent);
        assert(sdActivity.renderer.clears == sdActivity.renderer.frames);
      }
    }
  }

  // Exercise both actual callbacks at boundaries the real transport may skip
  // (duplicate reports, unknown totals, and values beyond the manifest size).
  reset(Failure::None);
  OtaUpdateActivity otaActivity;
  otaActivity.state = OtaUpdateActivity::State::UpdateInProgress;
  SdFirmwareUpdateActivity sdActivity;
  sdActivity.state = SdFirmwareUpdateActivity::State::UPDATING;
  auto otaProgress = ''' + method(ota, '[](void* ctx)') + r''';
  auto sdProgress = ''' + method(sd, '[](size_t written, size_t total, void* ctx)') + r''';
  for (auto [done, total, count] : {Sample{20,0,0}, Sample{9,100,0}, Sample{10,100,1},
                                  Sample{10,100,1}, Sample{19,100,1}, Sample{20,100,2},
                                  Sample{55,100,3}, Sample{64,100,3}, Sample{65,100,4},
                                  Sample{99,100,5}, Sample{100,100,6}, Sample{110,100,6}}) {
    otaActivity.updater.processedSize = done;
    otaActivity.updater.totalSize = total;
    otaProgress(&otaActivity);
    sdProgress(done, total, &sdActivity);
    assert(otaActivity.renderer.frames == count && sdActivity.renderer.frames == count);
    assert(otaActivity.progressBytes == done && otaActivity.progressTotalBytes == total);
    assert(sdActivity.writtenBytes == done && sdActivity.firmwareSize == total);
  }
}
'''
        run_cpp(program)


HARNESS = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define CROSSPOINT_VERSION "test"
#define SIMULATOR
#define SPI_FLASH_SEC_SIZE 4096
enum class Failure { None, Download, Read, Erase, Write, Verify, Switch, WrongChip, WrongBoard };
Failure fault = Failure::None;
std::mutex renderMutex;
thread_local bool locked = false, inRender = false;
std::atomic<bool> refreshPending = false;
int switches = 0, restarts = 0, aborts = 0, fontReloads = 0, writes = 0;
void flashOperation() { assert(!refreshPending && !locked); }
void reset(Failure f) {
  assert(!refreshPending && !locked);
  fault = f;
  switches = restarts = aborts = fontReloads = writes = 0;
}
void delay(int) { flashOperation(); }
struct { void restart() { flashOperation(); ++restarts; } } ESP;
const char* tr(int id) { static char labels[64][32]; snprintf(labels[id],32,"label%d",id); return labels[id]; }
struct Rect { int x,y,width,height; };
struct Sample { size_t done,total; int expected; };
constexpr int UI_10_FONT_ID=10, UI_12_FONT_ID=12, SMALL_FONT_ID=8;
struct EpdFontFamily { enum { REGULAR, BOLD }; };
struct GfxRenderer {
  int clears=0, frames=0;
  mutable std::vector<std::string> text;
  mutable std::vector<int> percentages;
  int getScreenWidth() const { return 684; }
  int getScreenHeight() const { return 1216; }
  int getLineHeight(int) const { return 20; }
  void clearScreen() { assert(locked); ++clears; text.clear(); }
  template<class... Args> void drawText(int,int,int,const char* value,Args...) const { text.emplace_back(value); }
  template<class... Args> void drawCenteredText(int,int,const char* value,Args...) const {
    text.emplace_back(value);
    if (strchr(value,'%')) percentages.push_back(std::stoi(value));
  }
  void drawRect(int,int,int,int) const {}
  void fillRect(int,int,int,int) const {}
  void displayBuffer() {
    assert(locked && refreshPending);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ++frames;
  }
  struct ClipScope { ClipScope(GfxRenderer&,int,int,int,int) {} };
};
struct BaseTheme {
  int measureProgressBarHeight(const GfxRenderer&,int,bool=true) const;
  int drawProgressBar(const GfxRenderer&,Rect,size_t,size_t,bool=true) const;
};
struct ThemeMetrics {
  int topPadding=5,headerHeight=40,verticalSpacing=10,contentSidePadding=10,progressBarHeight=20,tabBarHeight=30;
};
struct UITheme : BaseTheme {
  bool tabs=false;
  static UITheme& getInstance() { static UITheme theme; return theme; }
  bool hasMainTabs() const { return tabs; }
  const ThemeMetrics& getMetrics() const { static ThemeMetrics m; return m; }
  Rect getScreenSafeArea(const GfxRenderer&,bool,bool) const { return {5,5,674,1206}; }
  template<class... Args> void drawHeader(Args...) const {}
  template<class... Args> void drawSubHeader(Args...) const {}
  template<class... Args> void drawButtonHints(Args...) const {}
  template<class... Args> void drawList(Args...) const {}
  enum class TextVerticalAlignment { TOP };
  template<class... Args> static void drawCenteredText(GfxRenderer& r,Rect,int,int,const char* s,Args...) {
    r.text.emplace_back(s);
  }
  template<class... Args> static void drawCenteredWrappedText(GfxRenderer& r,Rect,int,const char* s,Args...) {
    r.text.emplace_back(s);
  }
};
#define GUI UITheme::getInstance()
namespace SubpageLayout {
Rect contentRect(Rect r,const ThemeMetrics&) { return r; }
Rect insetHorizontal(Rect r,int) { return r; }
int relatedGap(const ThemeMetrics&) { return 10; }
int sectionGap(const ThemeMetrics&) { return 20; }
int centeredTop(Rect r,int h) { return r.y+(r.height-h)/2; }
}
struct MappedInputManager {
  struct Labels { const char *btn1,*btn2,*btn3,*btn4; };
  Labels mapLabels(const char* a,const char* b,const char* c,const char* d) { return {a,b,c,d}; }
};
struct RenderLock;
struct Activity {
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  bool deferred=false;
  std::future<void> frame;
  virtual ~Activity() = default;
  virtual void render(RenderLock&&)=0;
  void requestUpdate(bool immediate=false);
  void requestUpdateAndWait();
  void finish() {}
};
struct RenderLock {
  explicit RenderLock(Activity&) { assert(!locked); renderMutex.lock(); locked=true; }
  ~RenderLock() { locked=false; renderMutex.unlock(); }
};
void Activity::requestUpdate(bool immediate) {
  if (!immediate) { deferred=true; return; }
  assert(!refreshPending.exchange(true));
  frame = std::async(std::launch::async,[this] {
    RenderLock lock(*this);
    inRender=true;
    render(std::move(lock));
    inRender=false;
    refreshPending=false;
  });
}
void Activity::requestUpdateAndWait() {
  assert(!locked);
  requestUpdate(true);
  frame.get();
}
struct { void ensureLoaded(GfxRenderer&,bool) { assert(locked); ++fontReloads; }
         void releaseLoadedFont(GfxRenderer&) { assert(locked); } } sdFontSystem;
namespace NetworkStartup { void prepare(GfxRenderer&) { flashOperation(); } }
struct OptionPopup { bool processRender(GfxRenderer&,MappedInputManager&) { return false; } };
struct OtaUpdater {
  enum class Channel { Stable, Nightly };
  enum OtaUpdaterError { OK, UPDATE_OLDER_ERROR, INTERNAL_UPDATE_ERROR, WRONG_DEVICE_ERROR, HTTP_ERROR };
  using ProgressCallback=void(*)(void*);
  size_t processedSize=0,totalSize=0;
  std::string otaUrl="firmware";
  bool isUpdateNewer() const { return true; }
  size_t getProcessedSize() const { assert(!inRender); return processedSize; }
  size_t getTotalSize() const { assert(!inRender); return totalSize; }
  const std::string& getLatestVersion() const { static std::string version="next"; return version; }
  OtaUpdaterError installUpdate(ProgressCallback,void*);
};
struct OtaUpdateActivity : Activity {
  enum class State { Ready,WifiSelection,CheckingForUpdate,UpdateAvailable,ConfirmingUpdate,
                    UpdateInProgress,NoUpdate,Failed,Finished,ShuttingDown };
  State state=State::Ready;
  OtaUpdater::Channel selectedChannel=OtaUpdater::Channel::Stable;
  int selectedReadyRow=0;
  size_t progressBytes=0,progressTotalBytes=0;
  unsigned int lastProgressRefreshPercent=0;
  OtaUpdater updater;
  OptionPopup updateConfirmation;
  const char* failedDetail=nullptr;
  void renderUpdateAvailable(Rect) {}
  void render(RenderLock&&) override;
  void runUpdateInstall();
};
enum { CHECK_UPDATES_ROW,NIGHTLY_ROW,READY_ROW_COUNT };
Rect getReadyListRect(const GfxRenderer&) { return {0,0,400,100}; }
struct ActivityResult { bool isCancelled=false; };
struct SdFirmwareUpdateActivity : Activity {
  enum class State { PICKING,VALIDATING,CONFIRMING,UPDATING,SUCCESS,FAILED };
  State state=State::PICKING;
  bool recoveryMode=false;
  std::string firmwarePath="/firmware.bin",errorMessage;
  size_t writtenBytes=0,firmwareSize=409600;
  unsigned int lastProgressRefreshPercent=0;
  void launchPicker() {}
  void render(RenderLock&&) override;
  void onConfirmationResult(const ActivityResult&);
  void performUpdate();
};
constexpr int ESP_OK=0, OTA_SIZE_UNKNOWN=-1, WIFI_PS_MIN_MODEM=0;
using esp_err_t=int;
using esp_ota_handle_t=int;
struct esp_partition_t { size_t size=1024*1024; const char* label="ota1"; int address=0x650000; };
const esp_partition_t* esp_ota_get_next_update_partition(void*) { static esp_partition_t p; return &p; }
const char* esp_err_to_name(int) { return "failure"; }
int esp_ota_begin(const esp_partition_t*,int,esp_ota_handle_t*) { flashOperation(); return ESP_OK; }
int esp_ota_write(esp_ota_handle_t,const uint8_t*,size_t) {
  flashOperation(); ++writes; return fault==Failure::Write && writes==5 ? -1 : ESP_OK;
}
void esp_ota_abort(esp_ota_handle_t) { flashOperation(); ++aborts; }
int esp_ota_end(esp_ota_handle_t) { flashOperation(); return fault==Failure::Verify ? -1 : ESP_OK; }
int esp_ota_set_boot_partition(const esp_partition_t*) {
  flashOperation(); if (fault==Failure::Switch) return -1; ++switches; return ESP_OK;
}
void esp_wifi_set_ps(int) {}
namespace board_tag {
struct Scanner {
  void feed(const uint8_t*,size_t) {}
  bool mismatch() const { return fault==Failure::WrongBoard; }
  const char* foundName() const { return "other"; }
};
}
namespace HttpDownloader {
template<class F> bool fetchUrl(const std::string&,F write) {
  size_t previous=0;
  for (size_t current : {10,90,100,190,200,550,640,650,990,1000}) {
    std::vector<uint8_t> chunk(current-previous,0);
    if (fault==Failure::WrongChip && previous<=12 && current>12) chunk[12-previous]=1;
    if (!write(chunk.data(),chunk.size())) return false;
    flashOperation();
    previous=current;
    if (fault==Failure::Download && current==550) return false;
  }
  return true;
}
}
template<class T> auto makeUniqueNoThrow(size_t n) { return std::make_unique<T>(n); }
struct HalFile {
  explicit operator bool() const { return true; }
  size_t fileSize() const { return 409600; }
  int read(uint8_t* data,size_t n) {
    if (fault==Failure::Read && writes>=11) return 0;
    memset(data,0,n); return static_cast<int>(n);
  }
  void close() {}
};
struct { bool openFileForRead(const char*,const char*,HalFile&) { return true; } } Storage;
int esp_partition_erase_range(const esp_partition_t*,size_t offset,size_t) {
  flashOperation(); return fault==Failure::Erase && offset>0 ? -1 : ESP_OK;
}
int esp_partition_write(const esp_partition_t*,size_t,const uint8_t*,size_t) {
  flashOperation(); ++writes; return fault==Failure::Write && writes==15 ? -1 : ESP_OK;
}
namespace ota_boot {
bool switchTo(const esp_partition_t* p) { return esp_ota_set_boot_partition(p)==ESP_OK; }
}
namespace firmware_flash {
enum class Result { OK,NO_PARTITION,OPEN_FAIL,OOM,READ_FAIL,ERASE_FAIL,WRITE_FAIL,OTADATA_FAIL,BAD_CHIP,WRONG_BOARD,BAD_SHA };
using ProgressCb=void(*)(size_t,size_t,void*);
const char* resultName(Result) { return "failure"; }
uint16_t runningPartitionChipId() { return 0; }
Result validateImageFile(const char*,size_t) { return fault==Failure::Verify ? Result::BAD_SHA : Result::OK; }
Result flashFromSdPath(const char*,ProgressCb,void*,bool=false);
}
'''


if __name__ == '__main__':
    unittest.main()
