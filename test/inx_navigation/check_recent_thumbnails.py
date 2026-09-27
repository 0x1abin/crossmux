"""Compile the production size policy and cache cleanup with small collaborators."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[2]
source = (repo / 'src/activities/home/InxRecentActivity.cpp').read_text()


def method(name):
    return re.search(r'void InxRecentActivity::' + name + r'\(.*?\n}', source, re.S).group()


for layout in ('Flow', 'Grid', 'List', 'Icons', 'Cover'):
    calls = re.findall(r'setThumbnailHeight\((.*?)\);', method('draw' + layout))
    assert calls == ['center.height' if layout == 'Flow' else 'cover.height'], (layout, calls)

harness = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include "InxItemLayout.h"
struct Activity { int exits=0; void onExit(){++exits;} };
struct Tracked {
  static inline int alive=0;
  Tracked(){++alive;}
  ~Tracked(){--alive;}
};
struct InxRecentActivity : Activity {
  enum class CoverCacheState { Unchecked, Ready };
  struct CoverRamCache { std::unique_ptr<Tracked> bytes; size_t size=0; bool attempted=false; };
  int thumbnailHeight=0;
  const int* books=nullptr;
  std::array<const int*, 10> bookStats{};
  std::array<CoverCacheState, 10> targetCoverStates{}, fallbackCoverStates{};
  std::array<CoverRamCache, 10> targetCoverCaches{}, fallbackCoverCaches{};
  size_t cachedCoverBytes=0;
  void setThumbnailHeight(int displayHeight);
  void clearCoverCaches();
  void onExit();
  void seed() {
    targetCoverStates.fill(CoverCacheState::Ready);
    fallbackCoverStates.fill(CoverCacheState::Ready);
#if EXPECT_DRAW_HEIGHT
    targetCoverCaches[0]={std::make_unique<Tracked>(), 32, true};
    fallbackCoverCaches[0]={std::make_unique<Tracked>(), 32, true};
    cachedCoverBytes=64;
#endif
  }
  void checkCleared() const {
    for(auto state : targetCoverStates) assert(state==CoverCacheState::Unchecked);
    for(auto state : fallbackCoverStates) assert(state==CoverCacheState::Unchecked);
    for(const auto& cache : targetCoverCaches) assert(!cache.bytes && !cache.size && !cache.attempted);
    for(const auto& cache : fallbackCoverCaches) assert(!cache.bytes && !cache.size && !cache.attempted);
    assert(cachedCoverBytes==0 && Tracked::alive==0);
  }
};
@METHODS@
int main() {
  InxRecentActivity activity;
  // Flow/Grid/List/Icons/Cover at 800x480, then at 480x800.
  constexpr int heights[][2]={{175,199},{163,185},{64,73},{106,121},{344,390},
                             {326,370},{318,361},{128,146},{209,237},{550,624},{0,0},{-1,0}};
  for(const auto& pair : heights) {
    activity.seed();
    activity.setThumbnailHeight(pair[0]);
    const int expected=pair[0]<0 ? 0 : pair[EXPECT_DRAW_HEIGHT ? 0 : 1];
    assert(activity.thumbnailHeight==expected);
    if(pair[0]==-1) {
      // Both invalid heights select zero: no new cache generation or clearing.
      assert(activity.targetCoverStates[0]==InxRecentActivity::CoverCacheState::Ready);
    } else {
      activity.checkCleared();
    }
    activity.seed();
    activity.setThumbnailHeight(pair[0]);
    assert(activity.targetCoverStates[0]==InxRecentActivity::CoverCacheState::Ready);
#if EXPECT_DRAW_HEIGHT
    assert(activity.cachedCoverBytes==64 && Tracked::alive==2);
#endif
  }
  activity.setThumbnailHeight(550);
  activity.seed();
  int book=1;
  activity.books=&book;
  activity.bookStats.fill(&book);
  activity.onExit();
  activity.checkCleared();
  assert(!activity.books && activity.thumbnailHeight==0 && activity.exits==1);
  for(auto stats : activity.bookStats) assert(!stats);
}
'''
methods = '\n'.join(method(name) for name in ('setThumbnailHeight', 'clearCoverCaches', 'onExit'))
with tempfile.TemporaryDirectory(prefix='inx-thumbnails-') as directory:
    cpp = Path(directory) / 'check.cpp'
    cpp.write_text(harness.replace('@METHODS@', methods))
    for name, flags, direct in (
        ('c3', [], False),
        ('s3-psram', ['-DBOARD_HAS_PSRAM'], True),
        ('simulator', ['-DBOARD_HAS_PSRAM', '-DSIMULATOR'], False),
        ('emulated', ['-DBOARD_HAS_PSRAM', '-DCROSSPOINT_EMULATED'], False),
    ):
        binary = Path(directory) / name
        subprocess.run([sys.argv[1], '-std=c++20', '-I'+str(repo/'src'), *flags,
                        '-DEXPECT_DRAW_HEIGHT='+str(int(direct)), str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(name + ': size selection, reuse and cleanup passed')
