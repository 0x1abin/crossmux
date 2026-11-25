"""Exercise real size selection and dynamic-setting callbacks for both profiles.

Only font discovery and the settings data holders are stubs. ReaderFontSizes.cpp
and buildFontSizeSetting() are production code. This does not exercise settings
JSON persistence or the complete SdCardFontSystem loading path.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function_definition(source: str, signature: str) -> str:
    start = source.index(signature)
    depth = 0
    for index in range(source.index('{', start), len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f'unbalanced function: {signature}')


with tempfile.TemporaryDirectory(prefix='reader-vector-sizes-') as temp:
    work = Path(temp)
    # SD discovery is the input boundary; selection and normalization stay real.
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
    setting_builder = function_definition(
        (ROOT / 'src/SettingsList.h').read_text(),
        'inline SettingInfo buildFontSizeSetting(')
    source = work / 'check.cpp'
    source.write_text(r'''
#include <ReaderFontSizes.h>
#include <cassert>
#include <functional>
#include <string>
#include <utility>
enum class StrId { STR_FONT_SIZE, STR_CAT_READER };
enum class SettingType { ENUM };
struct SettingInfo {
  StrId nameId, category;
  SettingType type;
  std::vector<std::string> enumStringValues;
  const char* key;
  bool inTextSettings;
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
};
struct Settings {
  char sdFontFamilyName[16] = "vector";
  uint8_t fontPointSize = 12;
} SETTINGS;
''' + setting_builder + r'''
int main() {
  SdCardFontRegistry registry;
  const auto sizes = readerFontPointSizes(&registry,"vector");
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
  const std::vector<uint8_t> expected = {
    8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
#else
  const std::vector<uint8_t> expected = {8,9,10,11,12,13,14,15,16,17,18,19,20,21,22};
#endif
  assert(sizes == expected);
  for (const uint8_t point : expected) {
    assert(snapToNearestPointSize(sizes,point) == point);
    assert(snapToNearestPointSize(sizes.data(),sizes.size(),point) == point);
  }
  assert(snapToNearestPointSize(sizes,7) == 8);
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
  assert(snapToNearestPointSize(sizes,33) == 32);
#else
  assert(snapToNearestPointSize(sizes,33) == 22);
#endif

  const auto bitmap = readerFontPointSizes(&registry,"bitmap");
  assert((bitmap == std::vector<uint8_t>{12,26,32}));
  assert(snapToNearestPointSize(bitmap,26) == 26);
  assert(snapToNearestPointSize(bitmap,32) == 32);
  assert(snapToNearestPointSize(bitmap,19) == 12);  // lower point wins a tie
  assert(snapToNearestPointSize(bitmap,29) == 26);
  for (const auto& fallback : {
      readerFontPointSizes(nullptr,""), readerFontPointSizes(&registry,""),
      readerFontPointSizes(&registry,"missing"), readerFontPointSizes(&registry,nullptr)}) {
    assert((fallback == std::vector<uint8_t>{12}));
    assert(snapToNearestPointSize(fallback,32) == 12);
  }

  auto setting = buildFontSizeSetting(&registry);
  for (const uint8_t inheritedPoint : {26,32}) {
    SETTINGS.fontPointSize = inheritedPoint;
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
    const uint8_t expectedIndex = inheritedPoint == 26 ? 18 : 24;
    const uint8_t expectedPoint = inheritedPoint;
#else
    const uint8_t expectedIndex = 14;
    const uint8_t expectedPoint = 22;
#endif
    // The production setting callbacks convert actual points to a UI index
    // and back. No test-side replacement for the setting getter/setter exists.
    assert(setting.valueGetter() == expectedIndex);
    assert(setting.enumStringValues[expectedIndex] == std::to_string(expectedPoint) + " pt");
    SETTINGS.fontPointSize = 12;
    setting.valueSetter(expectedIndex);
    assert(SETTINGS.fontPointSize == expectedPoint);
    // loadTtfFamily uses this raw-range API to normalize inherited bitmap sizes.
    // This verifies that helper, not font loading or settings serialization.
    SETTINGS.fontPointSize = snapToNearestPointSize(
        VECTOR_READER_POINT_SIZES,std::size(VECTOR_READER_POINT_SIZES),inheritedPoint);
    assert(SETTINGS.fontPointSize == expectedPoint);
    assert(setting.valueGetter() == expectedIndex);
    setting.valueSetter(255);
    assert(SETTINGS.fontPointSize == expectedPoint);
  }
  std::strcpy(SETTINGS.sdFontFamilyName,"bitmap");
  auto bitmapSetting = buildFontSizeSetting(&registry);
  for (const uint8_t index : {1,2}) {
    const uint8_t expectedPoint = index == 1 ? 26 : 32;
    bitmapSetting.valueSetter(index);
    assert(SETTINGS.fontPointSize == expectedPoint);
    assert(bitmapSetting.valueGetter() == index);
  }
}
''')
    for high_density in (False, True):
        profile = 'high' if high_density else 'normal'
        binary = work / profile
        flags = ['-DCROSSMUX_UI_PROFILE_HIGH_DPI=1'] if high_density else []
        print(f'Checking {profile} reader font size policy and setting callbacks', flush=True)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        *flags, '-I' + str(work), '-I' + str(ROOT / 'src'), str(source),
                        str(ROOT / 'src/ReaderFontSizes.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Reader vector sizes: normal 8-22, high-density 8-32 and real selection callbacks passed')
