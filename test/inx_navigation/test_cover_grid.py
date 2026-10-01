#!/usr/bin/env python3
"""Exercise production theme selection and persisted label mapping with/without PSRAM."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def method(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end+1]
    raise ValueError(marker)

settings = (ROOT / 'src/CrossPointSettings.h').read_text()
ui = (ROOT / 'src/components/UITheme.cpp').read_text()
labels = (ROOT / 'src/SettingsList.h').read_text()
enum = re.search(r'enum UI_THEME \{.*?\};', settings, re.S).group()
label_method = method(labels, 'inline std::vector<StrId> homeThemeValues(')
ids = sorted(set(re.findall(r'StrId::(\w+)', label_method)))
program = r'''
#include <cassert>
#include <memory>
#include <vector>
#include <iterator>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
bool psram = false, oom = false;
struct CrossPointSettings { @ENUM@ int uiTheme=0; } SETTINGS;
enum class StrId { @IDS@ };
struct HalMemory { struct Heap {size_t totalBytes;}; static Heap getPsramHeap(){return {psram?8388608u:0u};} };
struct ThemeMetrics {};
struct BaseTheme {virtual ~BaseTheme()=default;};
struct LyraTheme:BaseTheme{}; struct RoundedRaffTheme:BaseTheme{};
struct Lyra3CoversTheme:BaseTheme{}; struct LyraCarouselTheme:BaseTheme{}; struct InxTheme:BaseTheme{};
namespace BaseMetrics {const ThemeMetrics values{};}
namespace LyraMetrics {const ThemeMetrics values{};}
namespace RoundedRaffMetrics {const ThemeMetrics values{};}
namespace Lyra3CoversMetrics {const ThemeMetrics values{};}
namespace LyraCarouselMetrics {const ThemeMetrics values{};}
namespace InxMetrics {const ThemeMetrics values{};}
template<class T> std::unique_ptr<T> makeUniqueNoThrow(){return oom?nullptr:std::make_unique<T>();}
struct Font {const int* data; Font(const int* p=nullptr):data(p){} }; using EpdFont=Font;
const int ubuntu_10_regular=1, ubuntu_10_bold=2, ubuntu_12_regular=3, ubuntu_12_bold=4;
const int notosans_18_regular=5, notosans_18_bold=6, fallback=7;
@FONT_BINDINGS@
Font ui18RegularFont{}, ui18BoldFont{};
Font offlineReaderFont{&fallback};
struct UITheme {
 BaseTheme fallbackTheme; std::unique_ptr<BaseTheme> ownedTheme;
 BaseTheme* currentTheme=nullptr; const ThemeMetrics* currentMetrics=nullptr;
 CrossPointSettings::UI_THEME currentType=CrossPointSettings::CLASSIC; bool metricsValid=true;
 static bool supportsCoverGrid(); static bool hasCoverGridHome();
 void setTheme(CrossPointSettings::UI_THEME); void reload();
};
@METHODS@
@LABELS@
int main(){
 static_assert(CrossPointSettings::LYRA_CAROUSEL==4 && CrossPointSettings::INX==5 && CrossPointSettings::COVER_GRID==6);
 for(bool available:{false,true}) for(int saved=0;saved<=6;++saved){
  psram=available; SETTINGS.uiTheme=saved; UITheme theme;
  theme.reload();
  const bool inx=saved==5;
  assert(ui18RegularFont.data==(inx?&fallback:&notosans_18_regular));
  assert(ui18BoldFont.data==(inx?&fallback:&notosans_18_bold));
  assert(ui10RegularFont.data==&ubuntu_10_regular && ui10BoldFont.data==&ubuntu_10_bold);
  assert(ui12RegularFont.data==&ubuntu_12_regular && ui12BoldFont.data==&ubuntu_12_bold);
  assert(SETTINGS.uiTheme==saved);
  assert(theme.currentType==(saved==6&&!psram?1:saved));
  assert(UITheme::hasCoverGridHome()==(saved==6&&psram));
  const auto values=homeThemeValues(); assert(values.size()==(psram?7:6));
  assert(values[4]==StrId::STR_THEME_LYRA_CAROUSEL && values[5]==StrId::STR_THEME_INX);
  if(psram) assert(values[6]==StrId::STR_THEME_COVER_GRID);
 }
 oom=true; SETTINGS.uiTheme=6; UITheme theme; theme.setTheme(CrossPointSettings::COVER_GRID);
 assert(theme.currentTheme==&theme.fallbackTheme && theme.currentType==CrossPointSettings::CLASSIC);
 assert(SETTINGS.uiTheme==6);
}
'''
for key, value in {'ENUM':enum,'IDS':','.join(ids),'LABELS':label_method,
 'FONT_BINDINGS':'\n'.join(re.findall(r'^EpdFont ui(?:10|12)(?:Regular|Bold)Font\([^\n]+', ui, re.M)),
 'METHODS':'\n'.join(method(ui, name) for name in ['bool UITheme::supportsCoverGrid(', 'bool UITheme::hasCoverGridHome(', 'void UITheme::setTheme(', 'void UITheme::reload('])}.items():
    program=program.replace('@'+key+'@',value)
with tempfile.TemporaryDirectory(prefix='cover-grid-') as tmp:
    source=Path(tmp)/'check.cpp'; binary=Path(tmp)/'check';source.write_text(program)
    subprocess.run(['c++','-std=c++20',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('Cover Grid IDs, capability fallback, saved values and allocation failure passed')
