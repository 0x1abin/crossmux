"""Run the production header, resource-error and sync refresh code with small seams."""
from pathlib import Path
import importlib.util
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WEREAD = ROOT / 'src/activities/apps/weread/webapi'


def method(source, name):
    start = source.index(name)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def run_cpp(program):
    with tempfile.TemporaryDirectory(prefix='reading-ui-') as directory:
        cpp = Path(directory) / 'check.cpp'
        exe = Path(directory) / 'check'
        cpp.write_text(program)
        subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


class ReadingUiRegressionTest(unittest.TestCase):
    def test_header_subtitle_is_inside_clip(self):
        source = (ROOT / 'src/components/themes/inx/InxTheme.cpp').read_text()
        code = method(source, 'void InxTheme::drawHeader(')
        run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <initializer_list>
struct Rect { int x, y, width, height; };
constexpr int SMALL_FONT_ID = 1, NOTOSERIF_12_FONT_ID = 2, kIconGap = 8, kRowPadding = 20;
namespace EpdFontFamily { enum Style { REGULAR, BOLD }; }
struct CrossPointSettings { enum class HIDE_BATTERY_PERCENTAGE { HIDE_ALWAYS }; };
struct { CrossPointSettings::HIDE_BATTERY_PERCENTAGE hideBatteryPercentage{}; } SETTINGS;
namespace InxMetrics { struct { int batteryWidth=20, batteryHeight=10, batteryBarHeight=24, contentSidePadding=20; } values; }
struct GfxRenderer {
  mutable Rect clip{};
  mutable bool clipped=false;
  mutable int subtitles=0;
  struct ClipScope {
    const GfxRenderer& r;
    ClipScope(const GfxRenderer& r, int x, int y, int w, int h):r(r) { r.clip={x,y,w,h}; r.clipped=true; }
    ~ClipScope() { r.clipped=false; }
  };
  int getLineHeight(int font) const { return font == SMALL_FONT_ID ? 18 : 36; }
  int getTextWidth(int, const char* text) const { return static_cast<int>(strlen(text))*6; }
  void fillRect(int,int,int,int,bool) const {}
  void drawLine(int,int,int,int,bool) const {}
  void drawText(int font, int x, int y, const char* text, bool=true, EpdFontFamily::Style=EpdFontFamily::REGULAR) const {
    if (font != SMALL_FONT_ID) return;
    ++subtitles;
    assert(clipped && x >= clip.x && y >= clip.y);
    assert(x + getTextWidth(font,text) <= clip.x + clip.width);
    assert(y + getLineHeight(font) <= clip.y + clip.height);
  }
};
struct InxTheme {
  void drawBatteryRight(const GfxRenderer&, Rect, bool) const {}
  void drawHeader(const GfxRenderer&, Rect, const char*, const char*) const;
};
''' + code + r'''
int main() {
  GfxRenderer r;
  for (int width : {480, 800}) for (int y : {0, 12}) {
    for (const char* subtitle : {"更多详情", "More Details", "2026-09-16"})
      InxTheme{}.drawHeader(r, {0,y,width,66}, "Stats", subtitle);
  }
  assert(r.subtitles == 12);
  InxTheme{}.drawHeader(r, {0,0,480,66}, "Stats", nullptr);
  assert(r.subtitles == 12);
}
''')

    def test_sync_refresh_survives_wifi_child(self):
        source = (WEREAD / 'WeReadProgressSyncActivity.cpp').read_text()
        enter = method(source, 'void WeReadProgressSyncActivity::onEnter(')
        callback = method(source, 'void WeReadProgressSyncActivity::onWifiSelectionComplete(')
        render = method(source, 'void WeReadProgressSyncActivity::render(')
        display = render[render.rindex('  renderer.displayBuffer('):render.rindex('}')]
        run_cpp(r'''
#include <atomic>
#include <cassert>
#include <initializer_list>
namespace HalDisplay { enum RefreshMode { FULL_REFRESH, FAST_REFRESH }; }
struct Renderer {
  HalDisplay::RefreshMode last = HalDisplay::FAST_REFRESH;
  void displayBuffer(HalDisplay::RefreshMode mode=HalDisplay::FAST_REFRESH) { last=mode; }
};
struct Activity { void onEnter() {} };
namespace ReaderUtils { void applyOrientation(Renderer&, int) {} }
struct { int orientation=0; } SETTINGS;
bool loggedIn=true;
namespace WeReadStore {
struct Session { bool valid() { return true; } void clear() {} };
bool loadSession(Session&) { return loggedIn; }
}
namespace NetworkStartup { void prepare(Renderer&) {} }
constexpr int WL_CONNECTED=1;
struct { int connected=1; int status() { return connected; } } WiFi;
struct WeReadProgressSyncActivity : Activity {
  enum class State { WifiSelection, Starting, LoginRequired };
  State state_ = State::WifiSelection;
  std::atomic<bool> fullRefreshPending_{true};
  Renderer renderer;
  bool wifiActivated_=false, returned=false, child=false;
  void requestUpdate() {}
  void launchWifiSelection() { child=true; }
  void returnToReader() { returned=true; }
  void onEnter();
  void onWifiSelectionComplete(bool);
  void renderRefresh() { DISPLAY }
};
'''.replace('DISPLAY', display) + enter + callback + r'''
int main() {
  for (bool login : {false,true}) for (int connected : {0,1}) {
    loggedIn=login; WiFi.connected=connected;
    WeReadProgressSyncActivity page;
    page.onEnter();
    if (page.child) {
      page.renderer.displayBuffer(); // A child paint cannot consume the parent's flag.
      WiFi.connected=1;
      page.onWifiSelectionComplete(true);
    }
    page.renderRefresh(); assert(page.renderer.last==HalDisplay::FULL_REFRESH);
    page.renderRefresh(); assert(page.renderer.last==HalDisplay::FAST_REFRESH);
    WiFi.connected=1;
    page.onWifiSelectionComplete(true);
    page.renderRefresh(); assert(page.renderer.last==HalDisplay::FULL_REFRESH);
    page.renderRefresh(); assert(page.renderer.last==HalDisplay::FAST_REFRESH);
    page.onWifiSelectionComplete(false); assert(page.returned);
  }
}
''')

    def test_resource_error_messages_and_layout(self):
        source = (WEREAD / 'WeReadActivity.cpp').read_text()
        message = method(source, 'const char* WeReadActivity::errorMessage(')
        render = method(source, 'void WeReadActivity::render(')
        error = render.split('    case State::Error: {', 1)[1].split('    case State::LogoutError:', 1)[0]
        keys = ('STR_WEREAD_STORAGE_ERROR', 'STR_WEREAD_CHECK_STORAGE_SPACE', 'STR_WEREAD_CHECK_SD_CARD',
                'STR_WEREAD_MEMORY_ERROR', 'STR_WEREAD_RESTART_HINT', 'STR_WEREAD_HTTP_ERROR',
                'STR_WEREAD_NO_WIFI', 'STR_WEREAD_CACHE_NOT_AVAILABLE', 'STR_WEREAD_CACHE_WHOLE_BOOK_ONLY')
        for language in ('chinese', 'english'):
            spec = importlib.util.spec_from_file_location('gen_i18n', ROOT / 'scripts/gen_i18n.py')
            generator = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(generator)
            translations = generator.parse_yaml_file(str(ROOT / f'lib/I18n/translations/{language}.yaml'))
            strings = '\n'.join(f'const char* {key} = {json.dumps(translations[key], ensure_ascii=False)};' for key in keys)
            run_cpp(r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <initializer_list>
''' + strings + r'''
#define tr(key) key
namespace WeReadClient { enum class Error { SdCard, OutOfMemory, Network, Unavailable, WholeBookOnly, Protocol }; }
namespace EpdFontFamily { enum Style { BOLD, REGULAR }; }
constexpr int UI_10_FONT_ID=10, WL_CONNECTED=1;
struct { int connected=1; int status() const { return connected; } } WiFi;
struct Rect { int x,y,width,height; };
struct Metrics { int contentSidePadding=20; };
struct Renderer {
  struct ClipScope { ClipScope(const Renderer&,int,int,int,int) {} };
  int getLineHeight(int) const { return 26; }
  int getTextWidth(const char* text) const {
    int width=0;
    for (; *text; ++text) {
      unsigned char c=*text;
      if (c < 128) width+=12;
      else if ((c & 0xc0) != 0x80) width+=26;
    }
    return width;
  }
};
using GfxRenderer=Renderer;
std::vector<std::string> shown;
struct { void drawPopup(const Renderer&, const char* s) { shown.emplace_back(s); } } GUI;
namespace SubpageLayout {
int sectionGap(const Metrics&) { return 12; }
int centeredTop(Rect r,int h) { return r.y + std::max(0,(r.height-h)/2); }
Rect insetHorizontal(Rect r,int n) { return {r.x+n,r.y,r.width-2*n,r.height}; }
}
namespace UITheme {
void drawCenteredText(const Renderer& r,Rect rect,int,int y,const char* text,bool,EpdFontFamily::Style) {
  assert(r.getTextWidth(text)<=rect.width);
  assert(y>=rect.y && y+26<=rect.y+rect.height);
  shown.emplace_back(text);
}
}
struct WeReadActivity {
  WeReadClient::Error error_;
  Renderer renderer;
  Metrics metrics;
  Rect content;
  enum class State { Error };
  const char* errorMessage() const;
  void renderError() { switch (State::Error) { case State::Error: { ERROR } }
};
'''.replace('ERROR', error) + message + r'''
int main() {
  using E=WeReadClient::Error;
  for (Rect bounds : {Rect{0,66,480,694},Rect{0,66,800,374}}) {
    WeReadActivity page{E::SdCard,{}, {},bounds};
    shown.clear(); page.renderError();
    assert(shown==std::vector<std::string>({STR_WEREAD_STORAGE_ERROR,STR_WEREAD_CHECK_STORAGE_SPACE,STR_WEREAD_CHECK_SD_CARD}));
    page.error_=E::OutOfMemory; shown.clear(); page.renderError();
    assert(shown==std::vector<std::string>({STR_WEREAD_MEMORY_ERROR,STR_WEREAD_RESTART_HINT}));
    page.error_=E::Network; shown.clear(); page.renderError();
    assert(shown==std::vector<std::string>({STR_WEREAD_HTTP_ERROR}));
    WiFi.connected=0; shown.clear(); page.renderError();
    assert(shown==std::vector<std::string>({STR_WEREAD_NO_WIFI})); WiFi.connected=1;
  }
}
''')


if __name__ == '__main__':
    unittest.main()
