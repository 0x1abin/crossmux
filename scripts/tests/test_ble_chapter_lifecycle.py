"""Execute the production chapter/lifecycle methods with deterministic host doubles."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
EPUB = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def execute(source, c3=1, ble=1, psram=0):
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / 'check.cpp'
        exe = Path(directory) / 'check'
        cpp.write_text(source)
        result = subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
            '-std=c++17', f'-DCONFIG_IDF_TARGET_ESP32C3={c3}',
            f'-DFREEINK_CAP_BLE_HID_HOST={ble}', f'-DCROSSPOINT_BLE_HOST_PSRAM={psram}',
            str(cpp), '-o', str(exe)],
            capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        subprocess.run([str(exe)], check=True)


class ChapterLifecycleTest(unittest.TestCase):
    def test_readiness_build_boundaries_and_retry(self):
        constants = '\n'.join(re.findall(r'constexpr int (?:BUILD_WINDOW_AHEAD|PARTIAL_REBUILD_START_MARGIN|BACKGROUND_BUILD_PAGES_PER_TICK) = \d+;', (ROOT / 'src/activities/reader/EpubReaderActivity.h').read_text()))
        self.assertEqual(constants.count(';'), 3)
        harness = r'''
#include <cassert>
#include <optional>
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
struct { bool bluetoothEnabled = true; } SETTINGS;
constexpr bool hasBle = FREEINK_CAP_BLE_HID_HOST;
constexpr bool keepBleDuringBuild = hasBle && (CONFIG_IDF_TARGET_ESP32C3 || CROSSPOINT_BLE_HOST_PSRAM);
constexpr int buildStops = hasBle && !keepBleDuringBuild ? 1 : 0;
bool running = false;
namespace bleinput {
 enum class StartContext { Reader };
 enum class StartResult { Started, LowMemory, Failed, Unavailable };
 int starts=0, stops=0;
 StartResult result=StartResult::Started;
 bool isRunning() { return hasBle && running; }
 void stop() { ++stops; running=false; }
 void logDiagnostics(const char*) {}
 template<class T> StartResult ensureStarted(T&, StartContext) {
   if (!hasBle) return StartResult::Unavailable;
   ++starts; running=result==StartResult::Started; return result;
 }
}
struct Cache { int releases=0; void releaseSdFontCaches() { assert(!running || keepBleDuringBuild); ++releases; } };
struct { Cache cache; Cache* getFontCacheManager() { return &cache; } } renderer;
struct RenderLock {
 static inline bool held=false;
 static inline int depth=0;
 static inline void (*onAcquire)()=nullptr;
 RenderLock() { if(onAcquire) onAcquire(); ++depth; }
 ~RenderLock() { --depth; }
 static bool peek() { return held; }
};
using ReaderRenderSpec=int;
struct Section {
 enum class BuildError { None, Io };
 bool building=false, partial=false, complete=false, fail=false;
 int currentPage=0, pageCount=30, end=100, begins=0, suspends=0;
 BuildError error=BuildError::None;
 bool isBuilding() const { return building; }
 bool isPartial() const { return partial; }
 bool isBuildComplete() const { return complete; }
 BuildError buildError() const { return error; }
 bool startBuild(int) { assert((!running || keepBleDuringBuild) && RenderLock::depth==1); ++begins; building=true; return true; }
 bool buildSomeMore(int pages) {
   assert((!running || keepBleDuringBuild) && RenderLock::depth==1);
   if(fail) { building=false; error=BuildError::Io; return false; }
   pageCount+=pages;
   if(pageCount>=end) { complete=true; building=false; partial=false; }
   return true;
 }
 void suspendBuild() { assert(building && RenderLock::depth==1); building=false; partial=true; ++suspends; }
};
struct EpubReaderActivity {
 Section* section=nullptr;
 int failedBuildSpine_=-1, currentSpineIndex=0, buildViewportWidth=400, buildViewportHeight=600;
 std::optional<unsigned> cachedVisibleTextOffset;
 int cachedChapterTotalPageCount=0, failures=0, updates=0;
 bool deferBluetoothStart() const;
 void prepareChapterBuild();
 bool needsPartialRebuild() const;
 bool updateChapterBuild();
 bool buildTickHeapGate() { return true; }
 int effectiveRenderSpec(int,int) { return 0; }
 bool handleBuildFailure(const char*,Section::BuildError) { ++failures; return false; }
 void requestUpdate() { ++updates; }
 bool applyDeferredReposition() { cachedVisibleTextOffset.reset(); cachedChapterTotalPageCount=0; return true; }
} reader;
struct {
 bool keep=true, exclusive=false, isReader=true;
 bool keepsBluetoothAlive() { return keep; }
 bool requiresExclusiveStorageLoop() { return exclusive; }
 bool isReaderActivity() { return isReader; }
 bool deferBluetoothStart() { return reader.deferBluetoothStart(); }
} activityManager;
constexpr int WIFI_MODE_NULL=0;
struct { int mode=0; int getMode() { return mode; } } WiFi;
unsigned long now=0;
unsigned long millis() { return now; }
'''
        production = constants + '\n' + method(EPUB, 'bool EpubReaderActivity::deferBluetoothStart() const') + '\n' + method(EPUB, 'void EpubReaderActivity::prepareChapterBuild()')
        production += '\n' + method(EPUB, 'bool EpubReaderActivity::needsPartialRebuild() const')
        production += '\n' + method(EPUB, 'bool EpubReaderActivity::updateChapterBuild()')
        production += method((ROOT / 'src/main.cpp').read_text(), 'void updateBluetoothLifecycle()')
        checks = r'''
int main() {
 Section section; reader.section=&section;
 assert(!reader.deferBluetoothStart());
 reader.section=nullptr; assert(reader.deferBluetoothStart());
 updateBluetoothLifecycle(); assert(bleinput::starts==0); // Initial open / chapter selection.
 running=hasBle; updateBluetoothLifecycle(); assert(running==hasBle); // Menu / cancellation holds link.
 reader.section=&section; updateBluetoothLifecycle(); assert(running==hasBle);
 const int stopsBeforePrepare=bleinput::stops;
 reader.prepareChapterBuild(); assert(running==keepBleDuringBuild);
 assert(bleinput::stops==stopsBeforePrepare+buildStops);
 assert(renderer.cache.releases==(CONFIG_IDF_TARGET_ESP32C3 && hasBle ? 1 : 0));
 section.building=true; updateBluetoothLifecycle(); assert(bleinput::starts==0);
 section.building=false; section.pageCount=0;
 assert(reader.deferBluetoothStart()); // Failed build cannot reconnect into unreadable state.
 section.pageCount=30; section.partial=true;
 assert(!reader.deferBluetoothStart()); // Far from partial watermark is ready.
 updateBluetoothLifecycle();
#if FREEINK_CAP_BLE_HID_HOST
 assert(running);
 WiFi.mode=1; updateBluetoothLifecycle(); assert(!running); WiFi.mode=0;
 RenderLock::onAcquire=[] { reader.section=nullptr; };
 updateBluetoothLifecycle(); assert(!running); // Preparation began while taking the render lock.
 RenderLock::onAcquire=nullptr; reader.section=&section;
 bleinput::result=bleinput::StartResult::LowMemory;
 updateBluetoothLifecycle(); int attempts=bleinput::starts;
 now=1999; updateBluetoothLifecycle(); assert(bleinput::starts==attempts);
 now=2000; updateBluetoothLifecycle(); assert(bleinput::starts==attempts+1);
 bleinput::result=bleinput::StartResult::Started; now=4000;
 updateBluetoothLifecycle(); assert(running);
#endif
 const int stopsBeforeBackground=bleinput::stops;
 RenderLock::held=true;
 assert(reader.updateChapterBuild());
 assert(section.begins==0); // Busy rendering must never trigger construction.
 RenderLock::held=false;
 // A render that invalidates the section before lock acquisition must not
 // dereference the old section or start a build.
 RenderLock::onAcquire=[] { reader.section=nullptr; };
 assert(reader.updateChapterBuild());
 RenderLock::onAcquire=nullptr; reader.section=&section;
 assert(bleinput::stops==stopsBeforeBackground);
 section.currentPage=15; assert(reader.deferBluetoothStart());
 reader.updateChapterBuild(); assert(running==keepBleDuringBuild && section.begins==1);
 for(int i=0;i<100 && section.building;++i) reader.updateChapterBuild();
 assert(running==keepBleDuringBuild && bleinput::stops==stopsBeforeBackground+buildStops);
#if CONFIG_IDF_TARGET_ESP32C3 && FREEINK_CAP_BLE_HID_HOST
 assert(section.partial && section.suspends==1 && section.pageCount<section.end);
#else
 assert(section.complete && section.suspends==0);
#endif
 assert(!reader.deferBluetoothStart());
 now=6000; updateBluetoothLifecycle();
#if FREEINK_CAP_BLE_HID_HOST
 assert(running);
#endif
 const int stopsBeforeFailure=bleinput::stops;
 section.partial=true; section.currentPage=section.pageCount-1; section.fail=true;
 reader.updateChapterBuild(); assert(running==keepBleDuringBuild && !section.building && reader.failures==1);
 assert(bleinput::stops==stopsBeforeFailure+buildStops);
 section.fail=false; section.partial=false; section.complete=false; section.building=true;
 section.currentPage=0; section.pageCount=2; section.end=8;
 reader.cachedVisibleTextOffset=42;
 for(int i=0;i<10 && section.building;++i) reader.updateChapterBuild();
#if CONFIG_IDF_TARGET_ESP32C3 && FREEINK_CAP_BLE_HID_HOST
 assert(section.complete && !reader.cachedVisibleTextOffset); // Reposition waits for final count.
#else
 assert(section.building && reader.cachedVisibleTextOffset); // Original idle window stays unchanged.
#endif
}
'''
        for c3, ble, psram in ((1, 1, 0), (0, 1, 0), (0, 1, 1), (1, 0, 0)):
            with self.subTest(c3=c3, ble=ble, psram=psram):
                execute(harness + production + checks, c3, ble, psram)

    def test_book_index_preserves_connection_and_framebuffer_loan(self):
        # Execute the real loadBook prefix through indexing, before reader-position setup.
        source = method(EPUB, 'bool EpubReaderActivity::loadBook()')
        source = source[:source.index('  if (!loaded) {')] + '  return loaded;\n}'
        harness = r'''
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#define LOG_ERR(...) ((void)0)
constexpr bool hasBle = FREEINK_CAP_BLE_HID_HOST;
constexpr bool keepBle = hasBle && (CONFIG_IDF_TARGET_ESP32C3 || CROSSPOINT_BLE_HOST_PSRAM);
bool running=false, cacheExists=false, borrowed=false, loadSuccess=true;
int stops=0, loads=0, popups=0, refreshDisables=0;
namespace bleinput {
 void stop() { ++stops; running=false; }
 void logDiagnostics(const char*) {}
}
struct { bool exists(const char*) { return cacheExists; } } Storage;
struct GfxRenderer {
 struct FrameBufferLoan {
  explicit FrameBufferLoan(GfxRenderer&) { assert(!borrowed); borrowed=true; }
  ~FrameBufferLoan() { assert(borrowed); borrowed=false; }
 };
} renderer;
constexpr int STR_INDEXING=1;
int tr(int value) { return value; }
struct { void drawPopup(GfxRenderer&,int) { assert(!borrowed); ++popups; } } GUI;
struct { int embeddedStyle=1; } SETTINGS;
struct Epub {
 Epub(const std::string&,const char*) {}
 std::string getCachePath() { return "/test"; }
 bool load(bool,bool) {
  ++loads;
  assert(borrowed==!cacheExists);
  assert(running==(cacheExists ? hasBle : keepBle));
  return loadSuccess;
 }
};
template<class T,class... Args> auto makeUniqueNoThrow(Args&&... args) {
 return std::make_unique<T>(std::forward<Args>(args)...);
}
struct EpubReaderActivity {
 std::string bookPath="test.epub";
 bool loadBook();
 void disableFastInitialRefresh() { ++refreshDisables; }
};
'''
        checks = r'''
int main() {
 EpubReaderActivity reader;
 for(bool cached : {false,true}) for(bool success : {false,true}) {
  cacheExists=cached; loadSuccess=success; running=hasBle;
  stops=loads=popups=refreshDisables=0;
  assert(reader.loadBook()==success);
  assert(!borrowed && loads==1);
  assert(stops==(!cached && hasBle && !keepBle ? 1 : 0));
  assert(popups==(!cached ? 1 : 0) && refreshDisables==popups);
 }
}
'''
        for c3, ble, psram in ((1, 1, 0), (0, 1, 0), (0, 1, 1), (1, 0, 0)):
            with self.subTest(c3=c3, ble=ble, psram=psram):
                execute(harness + source + checks, c3, ble, psram)

    def test_every_build_entry_prepares_before_allocating(self):
        for match in re.finditer(r'section->(?:startBuild|createSectionFile)\(', EPUB):
            prefix = EPUB[max(0, match.start()-500):match.start()]
            self.assertIn('prepareChapterBuild();', prefix)
        self.assertEqual(len(re.findall(r'section->(?:startBuild|createSectionFile)\(', EPUB)), 4)

    def test_partial_commit_failure_releases_and_invalidates(self):
        source = (ROOT / 'lib/Epub/Epub/Section.cpp').read_text()
        harness = r'''
#include <cassert>
#include <cstdint>
#define LOG_INF(...) ((void)0)
constexpr int SECTION_FILE_PARTIAL_VERSION=1;
struct Parser { unsigned parseBytesConsumed() { return 100; } } parser;
struct Build { Parser* parser=&::parser; unsigned totalBytes=1000; } build;
struct Section {
 enum class BuildError { None, Io };
 BuildError buildError_=BuildError::None;
 Build* build_=&build;
 bool partial_=false, buildComplete_=false, commit=true;
 unsigned builtPageCount_=20, partialPageCount_=0, partialBytesConsumed_=0, partialTotalBytes_=0, pageCount=20;
 int commits=0, releases=0;
 bool commitBuildFile(int,unsigned,unsigned) { ++commits; return commit; }
 void releaseBuildResources() { ++releases; build_=nullptr; }
 void suspendBuild();
};
'''
        checks = r'''
int main() {
 Section ok; ok.suspendBuild();
 assert(ok.partial_ && ok.pageCount==20 && !ok.build_ && ok.releases==1);
 ok.suspendBuild(); assert(ok.releases==1);
 Section replay; replay.partial_=true; replay.partialPageCount_=30;
 replay.suspendBuild(); assert(replay.commits==0 && replay.pageCount==30 && !replay.build_);
 Section fail; fail.partial_=true; fail.partialPageCount_=10; fail.commit=false;
 fail.suspendBuild(); assert(!fail.partial_ && fail.pageCount==0 && !fail.build_);
 assert(fail.buildError_==Section::BuildError::Io && fail.releases==1);
}
'''
        execute(harness + method(source, 'void Section::suspendBuild()') + checks)


if __name__ == '__main__':
    unittest.main()
