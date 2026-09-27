"""Execute production fallback selection, setup and unload with small font/I/O seams."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_reading_ui_regressions import method

ROOT = Path(__file__).resolve().parents[2]


class UiFontFallbackTest(unittest.TestCase):
    def test_fallback_lifecycle(self):
        renderer = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
        header = (ROOT / 'lib/GfxRenderer/GfxRenderer.h').read_text()
        system = (ROOT / 'src/SdCardFontSystem.cpp').read_text()
        manager = (ROOT / 'lib/EpdFont/SdCardFontManager.cpp').read_text()
        table = system[system.index('#if !defined(ENABLE_CHINESE_VERSION)'):system.index('}  // namespace')]
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
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
constexpr int SMALL_FONT_ID=1, UI_10_FONT_ID=2, UI_12_FONT_ID=3;
constexpr int CJK_UI_8_FONT_ID=4, CJK_UI_10_FONT_ID=5, CJK_UI_12_FONT_ID=6;
namespace memory {
bool healthy = true;
bool psramHasHeadroom(size_t, size_t, size_t) { return healthy; }
}
namespace BidiUtils {
enum class BidiBaseDir { AUTO };
bool isTransparentMark(uint32_t) { return false; }
}
const char* resolveVisualText(const char* text, std::string&, BidiUtils::BidiBaseDir) { return text; }
namespace combiningMark {
int anchorFor(uint32_t) { return 0; }
int raiseAboveBase(int,int,int,int) { return 0; }
int anchorOver(int,int,int,int,int,int) { return 0; }
}
namespace fp4 { int toPixel(int value) { return value/16; } }
struct EpdGlyph { int top=0, height=8, left=0, width=8, advanceX=128; };
const void* measured=nullptr;
const void* drawn=nullptr;
struct EpdFontFamily {
  enum Style { REGULAR=0, BOLD=1, SUP=16, SUB=32 };
  std::set<uint32_t> coverage;
  bool hasCodepoint(uint32_t cp, Style = REGULAR) const { return coverage.contains(cp); }
  void getTextDimensions(const char*, int* w, int* h, Style) const { measured=this; *w=8; *h=8; }
  const EpdGlyph* getGlyph(uint32_t, Style, bool* replaced=nullptr) const {
    static EpdGlyph glyph; if (replaced) *replaced=false; return &glyph;
  }
  uint32_t applyLigatures(uint32_t cp, const char*&, Style) const { return cp; }
  int getKerning(uint32_t,uint32_t,Style) const { return 0; }
};
enum class TextRotation { None };
template<TextRotation> void renderCharImpl(const auto&, int, const EpdFontFamily& font, uint32_t,
                                         int,int,bool,EpdFontFamily::Style,uint8_t) { drawn=&font; }
void renderCharScaled(const auto&, int, const EpdFontFamily& font, uint32_t,
                      int,int,bool,EpdFontFamily::Style,uint8_t) { drawn=&font; }
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
                BidiUtils::BidiBaseDir=BidiUtils::BidiBaseDir::AUTO) const;
  void prewarmFallbackText(int,const char*,EpdFontFamily::Style=EpdFontFamily::REGULAR) const;
  std::map<int, EpdFontFamily> fontMap;
  std::map<int, std::array<int, 2>> fallbackFontMap_;
  using TextGetter = const char* (*)(const void*, uint32_t);
  void prewarmFallbackText(int,TextGetter,const void*,uint32_t,EpdFontFamily::Style=EpdFontFamily::REGULAR) const;
  std::map<int, SdCardFont*> sdCardFonts_;
  std::map<int, int> sdCardFontScales_;
  const auto& getFontMap() const { return fontMap; }
  int resolveTextFontId(int, const char*, EpdFontFamily::Style = EpdFontFamily::REGULAR) const;
  void clearSdCardFonts() { sdCardFonts_.clear(); sdCardFontScales_.clear(); }
''' + method(header, 'void setFallbackFont(') + '\n' + method(header, 'void removeFont(') + r'''
};
struct SdCardFontFileInfo { uint8_t pointSize; };
struct SdCardFontFamilyInfo {
  std::string name="test";
  std::map<uint8_t, SdCardFontFileInfo> files{{8,{8}},{10,{10}},{12,{12}},{14,{14}}};
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
''' + table + '\n' + method(renderer, 'int GfxRenderer::resolveTextFontId(') + '\n'
        for name in ('bool SdCardFontManager::loadFamily(', 'int SdCardFontManager::loadFamilyExtraSize(',
                     'void SdCardFontManager::unloadAll(', 'int SdCardFontManager::getFontId('):
            program += method(manager, name) + '\n'
        for name in ('int GfxRenderer::getTextWidth(', 'void GfxRenderer::drawText(',
                     'void GfxRenderer::prewarmFallbackText(const int fontId, const TextGetter getter,',
                     'void GfxRenderer::prewarmFallbackText(const int fontId, const char* text,'):
            program += method(renderer, name) + '\n'
        program += method(system, 'void SdCardFontSystem::setupUiFallbacks(') + r'''
int main() {
  for (int scenario=0; scenario<7; ++scenario) {
    GfxRenderer r;
    for (int id=1; id<=3; ++id) {
      r.fontMap[id]={{'A'}};
      r.fontMap[id+3]={{'A',0x4E00}};
      r.setFallbackFont(id,id+3);
    }
    SdCardFontSystem s;
    if (scenario==1) s.registry_.family.files.erase(10);
    if (scenario==2) s.manager_.failSize=8;
    if (scenario==3) s.manager_.coverage={'A'};
    if (scenario==6) s.manager_.coverage={'A',0x03B1};
    memory::healthy = scenario!=4;
    const int readerSize = scenario==5 ? 12 : 14;
    assert(s.manager_.loadFamily(s.registry_.family, r, readerSize));
    s.setupUiFallbacks(r);
    constexpr bool enabled = EXPECT_ENABLED;
    const bool active = enabled && scenario!=3 && scenario!=4;
    assert(s.manager_.loads == (active ? (scenario==0 || scenario==6 ? 4 : 3) : 1));
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
        configurations = (
            ('s3', ['CONFIG_IDF_TARGET_ESP32S3=1', 'BOARD_HAS_PSRAM'], True),
            ('c3', ['CONFIG_IDF_TARGET_ESP32C3=1'], False),
            ('s3_no_psram', ['CONFIG_IDF_TARGET_ESP32S3=1'], False),
            ('simulator', ['CONFIG_IDF_TARGET_ESP32S3=1', 'BOARD_HAS_PSRAM', 'SIMULATOR'], False),
            ('emulated', ['CONFIG_IDF_TARGET_ESP32S3=1', 'BOARD_HAS_PSRAM', 'CROSSPOINT_EMULATED'], False),
        )
        with tempfile.TemporaryDirectory(prefix='ui-fallback-') as directory:
            cpp = Path(directory) / 'check.cpp'
            exe = Path(directory) / 'check'
            cpp.write_text(program)
            for name, defines, enabled in configurations:
                with self.subTest(target=name):
                    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                    '-DENABLE_CHINESE_VERSION=1', f'-DEXPECT_ENABLED={int(enabled)}',
                                    *[f'-D{value}' for value in defines],
                                    '-I', str(ROOT / 'lib/Utf8'), str(cpp),
                                    str(ROOT / 'lib/Utf8/Utf8.cpp'), '-o', str(exe)], check=True)
                    subprocess.run([str(exe)], check=True)



if __name__ == '__main__':
    unittest.main()
