from pathlib import Path
import subprocess
import sys
import tempfile

# Compile the production method with lightweight collaborators, not a second
# implementation of the retry policy. This does not model the full Activity lifecycle.
repo = Path(__file__).resolve().parents[2]
s = (repo / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
method = s[s.index('bool EpubReaderActivity::handleBuildFailure('):s.index('bool EpubReaderActivity::buildTickHeapGate(')]
# Compile the actual production method, with only its collaborators stubbed.
prelude = r'''
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <cstdio>
#define LOG_ERR(...) ((void)0)
struct { int embeddedStyle = 1; } SETTINGS;
struct { unsigned getFreeHeap(){return 65536;} unsigned getMinFreeHeap(){return 65536;} unsigned getMaxAllocHeap(){return 32768;} } ESP;
struct Css { int clears=0; void clear(){++clears;} };
struct Epub { Css css; Css* getCssParser(){return &css;} };
struct Cache { int releases=0; void releaseSdFontCaches(){++releases;} };
struct Renderer { Cache cache; Cache* getFontCacheManager(){return &cache;} };
struct Section {
  enum class BuildError { None, OutOfMemory, InvalidData, Io };
  BuildError error = BuildError::OutOfMemory;
  BuildError buildError(){return error;}
  void abandonBuild(){}
};
struct EpubReaderActivity {
  int failedBuildSpine_=-1, currentSpineIndex=5, renderedSpineIndex_=5, cachedSpineIndex=5, nextPageNumber=7;
  bool stylesDisabledForSession_=false, pendingPercentJump=false, buildHeapPaused=true, buildPopupPending=true;
  bool automaticPageTurnActive=true;
  float pendingSpineProgress=0;
  std::optional<uint16_t> pendingPageJump;
  std::optional<uint32_t> pendingOffsetJump, currentPageVisibleOffset=123, cachedVisibleTextOffset=456;
  std::string pendingAnchor;
  std::unique_ptr<Section> section=std::make_unique<Section>();
  std::unique_ptr<Epub> epub=std::make_unique<Epub>();
  Renderer renderer;
  std::vector<int> currentPageLinks{1}, currentPageFootnotes{1};
  int updates=0, discards=0, deferredClears=0;
  void clearDeferredReposition(){++deferredClears;}
  void discardOverlayPage(){++discards;}
  void requestUpdate(){++updates;}
  bool handleBuildFailure(const char* stage, Section::BuildError error);
};
'''
checks = r'''
int main() {
  for (auto error : {Section::BuildError::Io, Section::BuildError::InvalidData}) {
    EpubReaderActivity r; r.section->error=error;
    assert(!r.handleBuildFailure("error", error)); assert(!r.stylesDisabledForSession_);
    assert(!r.section && r.failedBuildSpine_==5 && !r.automaticPageTurnActive);
    assert(r.renderer.cache.releases==1 && r.currentPageLinks.empty() && r.currentPageFootnotes.empty());
    assert(!r.handleBuildFailure("repeat", Section::BuildError::OutOfMemory)); assert(r.renderer.cache.releases==1 && r.discards==1);
  }
  EpubReaderActivity r;
#ifdef BOARD_HAS_PSRAM
  assert(!r.handleBuildFailure("oom", Section::BuildError::OutOfMemory)); assert(!r.stylesDisabledForSession_);
#else
  assert(r.handleBuildFailure("oom", Section::BuildError::OutOfMemory)); assert(r.stylesDisabledForSession_);
  assert(r.pendingOffsetJump==123 && !r.pendingPageJump && r.nextPageNumber==0 && r.updates==1);
  r.section=std::make_unique<Section>();
  assert(!r.handleBuildFailure("basic oom", Section::BuildError::OutOfMemory)); assert(r.failedBuildSpine_==5 && r.updates==1);
  assert(r.renderer.cache.releases==2);
  assert(!r.handleBuildFailure("repeat", Section::BuildError::OutOfMemory)); assert(r.renderer.cache.releases==2);
  for (int target=0; target<5; ++target) {
    EpubReaderActivity p;
    switch (target) {
      case 0: p.pendingAnchor="note"; break;
      case 1: p.pendingPercentJump=true; p.pendingSpineProgress=0.75f; break;
      case 2: p.pendingOffsetJump=0; break;
      case 3: p.pendingPageJump=UINT16_MAX; break;
      case 4: p.renderedSpineIndex_=4; break;
    }
    assert(p.handleBuildFailure("target", Section::BuildError::OutOfMemory));
    switch (target) {
      case 0: assert(p.pendingAnchor=="note" && !p.pendingOffsetJump); break;
      case 1: assert(p.pendingPercentJump && p.pendingSpineProgress==0.75f && !p.pendingOffsetJump); break;
      case 2: assert(p.pendingOffsetJump==0); break;
      case 3: assert(p.pendingPercentJump && p.pendingSpineProgress==1.f && !p.pendingPageJump); break;
      case 4: assert(p.pendingOffsetJump==456); break;
    }
  }
#endif
  SETTINGS.embeddedStyle=0;
  EpubReaderActivity basic; assert(!basic.handleBuildFailure("disabled", Section::BuildError::OutOfMemory)); assert(!basic.stylesDisabledForSession_);
  SETTINGS.embeddedStyle=1;
  EpubReaderActivity noSection; noSection.section.reset();
#ifdef BOARD_HAS_PSRAM
  assert(!noSection.handleBuildFailure("allocation", Section::BuildError::OutOfMemory));
#else
  assert(noSection.handleBuildFailure("allocation", Section::BuildError::OutOfMemory));
  assert(noSection.pendingOffsetJump==123 && noSection.updates==1 && noSection.failedBuildSpine_==-1);
  assert(!noSection.handleBuildFailure("basic allocation", Section::BuildError::OutOfMemory));
  assert(noSection.failedBuildSpine_==5 && noSection.updates==1);
  assert(!noSection.handleBuildFailure("repeat allocation", Section::BuildError::OutOfMemory));
  assert(noSection.renderer.cache.releases==2 && noSection.discards==2);
#endif
  for (auto error : {Section::BuildError::Io, Section::BuildError::InvalidData}) {
    EpubReaderActivity empty; empty.section.reset();
    assert(!empty.handleBuildFailure("empty error", error));
    assert(!empty.stylesDisabledForSession_ && empty.failedBuildSpine_==5);
  }
  puts("Reader failure transition checks passed");
}
'''
with tempfile.TemporaryDirectory(prefix="reader-failure-") as directory:
    root = Path(directory)
    source = root / 'reader-failure-check.cpp'
    source.write_text(prelude + method + checks)
    for name, flags in [('c3', []), ('psram', ['-DBOARD_HAS_PSRAM'])]:
        binary = root / name
        subprocess.run([sys.argv[1], '-std=c++20', *flags, str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
