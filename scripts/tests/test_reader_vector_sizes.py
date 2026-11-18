"""Compile the real size policy for normal and high-density UI profiles."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix='reader-vector-sizes-') as temp:
    work = Path(temp)
    # Font discovery is the input boundary; size selection and snapping are real.
    (work / 'SdCardFontRegistry.h').write_text(r'''
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
struct SdCardFontFamilyInfo {
  bool vector;
  std::vector<uint8_t> availableSizes() const { return {12,26,32}; }
};
class SdCardFontRegistry {
 public:
  const SdCardFontFamilyInfo* findFamily(const char* name) const {
    static const SdCardFontFamilyInfo ttf{true}, bitmap{false};
    if (std::strcmp(name,"vector")==0) return &ttf;
    if (std::strcmp(name,"bitmap")==0) return &bitmap;
    return nullptr;
  }
};
''')
    source = work / 'check.cpp'
    source.write_text(r'''
#include <ReaderFontSizes.h>
#include <cassert>
int main() {
  SdCardFontRegistry registry;
  const auto sizes=readerFontPointSizes(&registry,"vector");
  assert(sizes.front()==8);
  for(size_t i=1;i<sizes.size();++i) assert(sizes[i-1]<sizes[i]);
  for(uint8_t point=8;point<=22;++point) assert(snapToNearestPointSize(sizes,point)==point);
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
  assert(snapToNearestPointSize(sizes,26)==26);
  assert(snapToNearestPointSize(sizes,32)==32);
  assert(sizes.back()==32);
#else
  assert(snapToNearestPointSize(sizes,26)==22);
  assert(sizes.back()==22);
#endif
  const auto bitmap=readerFontPointSizes(&registry,"bitmap");
  assert(snapToNearestPointSize(bitmap,26)==26);
  const auto fallback=readerFontPointSizes(nullptr,"");
  assert(fallback.size()==1 && fallback.front()==12);
}
''')
    for high_density in (False, True):
        binary = work / ('high' if high_density else 'normal')
        flags = ['-DCROSSMUX_UI_PROFILE_HIGH_DPI=1'] if high_density else []
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        *flags, '-I' + str(work), '-I' + str(ROOT / 'src'), str(source),
                        str(ROOT / 'src/ReaderFontSizes.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Reader vector size selection: normal22 cap and high-density26/32 preserved')
