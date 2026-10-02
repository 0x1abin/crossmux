#!/usr/bin/env python3
"""Exercise production keyboard layout, render setup and input with production theme metrics and bounded host font measurements."""
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SDK = Path(os.environ.get('FREEINK_SDK_ROOT', ROOT / 'freeink-sdk'))
source = (ROOT / 'src/activities/util/KeyboardEntryActivity.cpp').read_text()


def method(name):
    return re.search(r'^[^\n]*KeyboardEntryActivity::' + name + r'\(.*?\n}', source, re.M | re.S).group()


names = ('currentLayout', 'selectedKey', 'selectedLogicalIndex', 'clampSelection',
         'moveSelectionRow', 'moveSelectionCol', 'syncSelectionToValue', 'utf8Prev',
         'insertUtf8', 'backspaceUtf8', 'activateValue', 'clearAllOrAltOnSelected', 'keyboardRect', 'keyboardKeysRect', 'inputStartY', 'inputLineCount',
         'utf8Next', 'measureRange', 'lineBreakEnd', 'inputWindowStart')
methods = [method(name) for name in names]
render = source[source.index('  interactions.beginPublishCycle();'):source.index('  interactionsReady = true;')]
confirm = source[source.index('  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm))'):source.index('  if (mappedInput.wasReleased(MappedInputManager::Button::Back))')]
trace_target = (HERE / 'InxStyleParity.cpp').read_text().split('#ifdef UPSTREAM_THEME_PARITY')[0].replace('std::printf', 'tracePrintf')
tables = source[source.index('namespace {'):source.index('void KeyboardEntryActivity::onEnter')]
layout_header = (ROOT / 'src/activities/util/KeyboardLayoutSet.h').read_text()
layout_source = (ROOT / 'src/activities/util/KeyboardLayoutSet.cpp').read_text()
layout_code = re.sub(r'^#(?:include|pragma)[^\n]*', '', layout_header + layout_source, flags=re.M)

def metrics_code(ref=None):
    def read(path):
        return subprocess.check_output(['git', 'show', f'{ref}:{path}'], cwd=ROOT, text=True) if ref else (ROOT/path).read_text()
    base = (ROOT/'src/components/themes/BaseTheme.h').read_text()
    code = re.search(r'struct ThemeMetrics \{.*?\n};', base, re.S).group()
    code += (ROOT/'src/components/UiHighDpiProfile.h').read_text().replace('#pragma once', '')
    code += re.search(r'namespace UiHighDpiProfile \{.*?namespace UiHighDpiProfile', (ROOT/'src/components/themes/BaseTheme.h').read_text(), re.S).group() + '\n'
    for path, name in [('src/components/themes/BaseTheme.h','Base'),
                       ('src/components/themes/lyra/LyraTheme.h','Lyra'),
                       ('src/components/themes/lyra/Lyra3CoversTheme.h','Lyra3Covers'),
                       ('src/components/themes/lyra/LyraCarouselTheme.h','LyraCarousel'),
                       ('src/components/themes/roundedraff/RoundedRaffTheme.h','RoundedRaff'),
                       ('src/components/themes/inx/InxTheme.h','Inx')]:
        text = read(path) if name not in ('Inx','LyraCarousel') else (ROOT/path).read_text()
        start = text.index(f'namespace {name}Metrics {{')
        end = text.index('\nclass ', start)
        code += text[start:end] + '\n'
    return code

harness = r'''
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
uint64_t traceHash=1469598103934665603ULL;
void tracePrintf(const char* format, ...) {
  char data[1024]; va_list args; va_start(args,format);
  const int n=vsnprintf(data,sizeof(data),format,args); va_end(args);
  assert(n>=0 && n<int(sizeof(data)));
  for(int i=0;i<n;++i) traceHash=(traceHash^uint8_t(data[i]))*1099511628211ULL;
}
@TRACE@
namespace fui = freeink::ui;
enum class InputType { Text, Password, Url };
struct CrossPointSettings {
  enum class UI_THEME { CLASSIC, LYRA, LYRA_3_COVERS, ROUNDEDRAFF, LYRA_CAROUSEL, INX };
  UI_THEME uiTheme = UI_THEME::CLASSIC;
  uint16_t keyboardLayouts = 0x1ff;
} SETTINGS;
enum class Language { EN, FR, DE, ES, RU, UK, BE, KK, HE, AR };
struct { Language getLanguage() const { return Language::EN; } } I18N;
@METRICS@
@LANGUAGES@
@TABLES@
constexpr int SMALL_FONT_ID=0, UI_12_FONT_ID=1;
struct EpdFontFamily { static constexpr int REGULAR=0; };
struct Renderer {
  int width=480, height=800, orientation=0; bool touch=false;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  int getLineHeight(int font) const { return UiHighDpiProfile::enabled ? (font==SMALL_FONT_ID?34:48) : (font==SMALL_FONT_ID?23:29); }
  int getTextAdvanceX(int, const char* text, int) const {
    int count=0; for (const auto* p=reinterpret_cast<const unsigned char*>(text); *p; ++p) count+=(*p&0xc0)!=0x80;
    return count*12;
  }
} renderer;
struct UITheme {
  static UITheme& getInstance() { static UITheme value; return value; }
  const ThemeMetrics& getMetrics() const {
    static ThemeMetrics metrics;
    const ThemeMetrics* all[]={&BaseMetrics::values,&LyraMetrics::values,&Lyra3CoversMetrics::values,
      &RoundedRaffMetrics::values,&LyraCarouselMetrics::values,&InxMetrics::values,&LyraMetrics::values};
    metrics=*all[int(SETTINGS.uiTheme)]; UiHighDpiProfile::apply(metrics); return metrics;
  }
  Rect getScreenSafeArea(const Renderer& r) const {
    // Actual ReadPico 5/5/8/5 board insets, rotated with the display.
    int t=5,ri=5,b=8,l=5;
    switch(r.orientation) {
      case 1:t=5;ri=5;b=5;l=8;break;
      case 2:t=8;ri=5;b=5;l=5;break;
      case 3:t=5;ri=8;b=5;l=5;break;
    }
    return {int16_t(l),int16_t(t),int16_t(r.width-l-ri),int16_t(r.height-t-b)};
  }
};
namespace freeink::ui {
struct GfxRendererTarget : TraceTarget {
  static constexpr int FONT_SMALL=0;
  Renderer& renderer;
  explicit GfxRendererTarget(Renderer& r) : renderer(r) {}
  int16_t lineHeight(FontId font) const override { return renderer.getLineHeight(font); }
  void text(Rect r, const char* s, TextStyle t) override {
    // A full font line box must remain in its published text rectangle.
    assert(r.height>=lineHeight(t.font)); TraceTarget::text(r,s,t);
  }
  void setFont(int slot, int font) { tracePrintf("font %d %d\n", slot, font); }
  void setTextCentering(TextCentering mode) { tracePrintf("centering %d\n", int(mode)); }
  DeviceContext deviceContext() const {
    DeviceContext d; d.width=renderer.width; d.height=renderer.height; d.hasTouch=renderer.touch; return d;
  }
};
}
@ALIGNMENT@
constexpr const char *STR_OK_BUTTON="OK", *STR_KEY_SHIFT="Shift", *STR_KEY_MODE_ABC="abc", *STR_KEY_MODE_SYMBOLS="?123";
const char* tr(const char* s) { return s; }
unsigned long millis() { return 1000; }
struct MappedInputManager {
  enum class Button { Confirm };
  bool down=false, up=false, held=false; unsigned long time=0;
  bool hasTouch() const { return renderer.touch; }
  bool wasPressed(Button) const { return down; }
  bool wasReleased(Button) const { return up; }
  bool isPressed(Button) const { return held; }
  unsigned long getHeldTime() const { return time; }
};
struct KeyboardEntryActivity {
  fui::KeyboardLayoutId layoutId=fui::KeyboardLayoutId::QwertyEn;
  InputType inputType=InputType::Text;
  bool shifted=false, symbols=false, showLangKey=true, urlPanel=false, cursorMode=false;
  bool interactionsReady=false;
  bool confirmHeld=false, confirmLongHandled=false, hintVisible=false, passwordVisible=false, togglePos=false;
  int selRow=0, selCol=0, delPressCount=0;
  size_t cursorPos=0, maxLength=0; unsigned long hintShowTime=0;
  std::string text;
  MappedInputManager mappedInput;
  fui::InteractionBuffer<56> interactions;
  static constexpr int URL_PANEL_KEY=-3, LONG_PRESS_MS=500, DEL_LONG_PRESS_MS=1500;
  void requestUpdate() {}
  void onComplete(std::string) {}
  @DECLARATIONS@
  void confirmStep() { @CONFIRM@ }
  void draw() {
    const auto kbRect=keyboardRect();
    const auto keysRect=keyboardKeysRect();
    const auto& metrics=UITheme::getInstance().getMetrics();
    assert(keysRect.height>0 && keysRect.width>0);
    if (UiHighDpiProfile::enabled) assert(keysRect.y>=inputStartY()+inputLineCount()*renderer.getLineHeight(UI_12_FONT_ID)+6);
    @RENDER@
    assert(props.geometry==fui::KeyboardGeometry::Separated && props.rowGap>=6 && props.keyRadius==3);
    for (size_t i=0; i<interactions.count(); ++i) {
      const auto& h=interactions.data()[i];
      assert(h.rect.x>=0 && h.rect.y>=0 && h.rect.right()<=renderer.width && h.rect.bottom()<=renderer.height);
      tracePrintf("hit %d %d %d %d %d %d\n", h.rect.x,h.rect.y,h.rect.width,h.rect.height,h.action,h.value);
      fui::InputSnapshot tap;
      tap.touchReleased=true; tap.touchX=h.rect.x; tap.touchY=h.rect.y;
      auto event=interactions.routePublished(tap); assert(event.action==h.action && event.value==h.value);
      tap.touchX=h.rect.right()-1; tap.touchY=h.rect.bottom()-1;
      event=interactions.routePublished(tap); assert(event.action==h.action && event.value==h.value);
      for (size_t j=0; j<i; ++j) {
        const auto& b=interactions.data()[j].rect;
        assert(h.rect.right()<=b.x || b.right()<=h.rect.x || h.rect.bottom()<=b.y || b.bottom()<=h.rect.y);
      }
    }
  }
};
@METHODS@
int main() {
  int scene=0;
  for (int device=0; device<(UiHighDpiProfile::enabled?1:2); ++device)
    for (int orientation=0; orientation<4; ++orientation) for (bool touch : {false,true}) {
    const bool landscape=orientation%2;
    renderer.orientation=orientation;
    renderer.width=UiHighDpiProfile::enabled?(landscape?1216:684):(landscape?(device?792:800):(device?528:480));
    renderer.height=UiHighDpiProfile::enabled?(landscape?684:1216):(landscape?(device?528:480):(device?792:800));
    renderer.touch=touch;
    for (auto type : {InputType::Text,InputType::Password,InputType::Url})
      for (const auto& language : keyboard_layouts::ALL) for (int flags=0; flags<16; ++flags,++scene)
        for (int theme=0; theme<7; ++theme) {
          SETTINGS.uiTheme=static_cast<CrossPointSettings::UI_THEME>(theme);
          KeyboardEntryActivity kb;
          kb.inputType=type; kb.layoutId=language.id;
          kb.shifted=flags&1; kb.symbols=flags&2; kb.showLangKey=flags&4; kb.urlPanel=flags&8;
          const auto& layout=kb.currentLayout();
          if (kb.symbols || type!=InputType::Url)
            assert(&layout==&fui::builtinKeyboardLayout(language.id,kb.shifted,kb.symbols,!kb.symbols,!kb.symbols&&kb.showLangKey));
          traceHash=1469598103934665603ULL;
          for (int r=0; r<layout.rowCount; ++r) for (int c=0; c<layout.rows[r].count; ++c) {
            const auto& k=layout.rows[r].keys[c];
            assert(kb.syncSelectionToValue(k.value));
            assert(kb.selectedKey()->value==k.value);
            tracePrintf("key %d %s %s\n",k.value,k.output?k.output:"",k.alt?k.alt:"");
          }
          const auto keysHash=traceHash;
          traceHash=1469598103934665603ULL;
          kb.selRow=kb.selCol=0;
          kb.moveSelectionCol(-1); assert(kb.selCol==layout.rows[0].count-1);
          kb.moveSelectionCol(1); assert(kb.selCol==0);
          kb.moveSelectionRow(-1); assert(kb.selRow==layout.rowCount-1);
          kb.moveSelectionRow(1); assert(kb.selRow==0);
          kb.draw();
          std::printf("SCENE %d %d %llu %llu\n",scene,theme,(unsigned long long)keysHash,(unsigned long long)traceHash);
        }
  }
  std::puts("INPUT CHECKS");
  KeyboardEntryActivity edit;
  std::string longText; for(int i=0;i<100;++i) longText+="a中é";
  for(size_t cursor=0;cursor<=longText.size();++cursor) {
    if(cursor<longText.size() && (uint8_t(longText[cursor])&0xc0)==0x80) continue;
    edit.cursorPos=cursor;
    int start=edit.inputWindowStart(longText,100), end=start;
    for(int line=0;line<edit.inputLineCount();++line) end=edit.lineBreakEnd(longText,end,100);
    assert(!UiHighDpiProfile::enabled || (size_t(start)<=cursor && cursor<=size_t(end)));
  }
  for (int theme=0; theme<7; ++theme) {
    SETTINGS.uiTheme=static_cast<CrossPointSettings::UI_THEME>(theme);
    KeyboardEntryActivity kb;
    for (const auto& language : keyboard_layouts::ALL) {
      kb.layoutId=language.id;
      assert(kb.activateValue(fui::QWERTY_KEY_LANG,false));
      assert(kb.layoutId==keyboard_layouts::next(language.id));
    }
    kb.layoutId=fui::KeyboardLayoutId::SpanishEs;
    assert(kb.syncSelectionToValue('a'));
    const std::string expected=fui::keyboardAltOutputFor(fui::builtinKeyboardLayout(kb.layoutId,false,false,true,true),'a');
    kb.mappedInput={true,false,true,0}; kb.confirmStep();
    kb.mappedInput={false,false,true,600}; kb.confirmStep(); assert(kb.text==expected);
    kb.mappedInput={false,false,true,1200}; kb.confirmStep(); assert(kb.text==expected);
    kb.mappedInput={false,true,false,1200}; kb.confirmStep(); assert(kb.text==expected);
    // Exercise touch hold against the same published key geometry.
    kb.text.clear(); kb.cursorPos=0;
    kb.draw();
    int x=0, y=0;
    for (size_t i=0; i<kb.interactions.count(); ++i) {
      const auto& h=kb.interactions.data()[i];
      if (h.value=='a') { x=h.rect.x+h.rect.width/2; y=h.rect.y+h.rect.height/2; }
    }
    assert(x>0 && y>0);
    auto& taps=kb.interactions;
    fui::TouchHoldRouter router;
    assert(!router.update(taps,true,x,y,false,0,0,true,1000).event);
    auto event=router.update(taps,true,x,y,false,0,0,true,1400).event;
    assert(event && event.longPress); assert(kb.activateValue(event.value,event.longPress)); assert(kb.text==expected);
    assert(!router.update(taps,true,x,y,false,0,0,true,1600).event);
    assert(!router.update(taps,false,0,0,true,x,y,false,1800).event);
  }
}
'''
alignment = (ROOT / 'src/components/UIThemeTokens.h').read_text()
alignment = re.search(r'inline void applyUiTextAlignment\(.*?\n}', alignment, re.S).group()
replacements = {'TRACE': trace_target, 'METRICS': metrics_code(), 'LANGUAGES': layout_code, 'TABLES': tables, 'ALIGNMENT': alignment,
                'DECLARATIONS': '\n'.join(('static ' if any(('::'+n+'(') in m for n in ('utf8Prev','utf8Next')) else '') + m.split('{', 1)[0].replace('KeyboardEntryActivity::', '') + ';' for m in methods),
                'CONFIRM': confirm, 'RENDER': render, 'METHODS': '\n'.join(methods)}
for name, code in replacements.items():
    harness = harness.replace('@' + name + '@', code)
def run_harness(code, tmp, name, high_dpi=False):
    cpp, binary = tmp / (name+'.cpp'), tmp / name
    cpp.write_text(code)
    ui = SDK / 'libs/ui/FreeInkUI'
    command = ['c++', '-std=c++20', '-I'+str(ui/'include'), str(cpp), str(ui/'src/FreeInkUI.cpp'), '-o', str(binary)]
    if high_dpi: command.append('-DCROSSMUX_UI_PROFILE_HIGH_DPI')
    subprocess.run(command, check=True)
    output = subprocess.check_output([str(binary)], text=True)
    return {tuple(map(int,row[:2])):tuple(map(int,row[2:])) for line in output.splitlines() if line.startswith('SCENE ') for row in [line.split()[1:]]}

with tempfile.TemporaryDirectory(prefix='unified-keyboard-', dir=os.environ.get('TMPDIR')) as tmp:
    tmp=Path(tmp)
    regular=run_harness(harness,tmp,'regular')
    high=run_harness(harness,tmp,'readpico',True)
    for scenes in (regular,high):
        for (scene,theme), (keys,draw) in scenes.items():
            assert scenes[scene,0][0]==keys, ('key tables',scene,theme)
            # INX chrome retains its own metrics; regular keyboard geometry is Lyra.
            if theme==5: assert scenes[scene,1][1]==draw, ('INX geometry',scene)
    # Build the fixed upstream with its own production metrics and keyboard code.
    ref=os.environ.get('FREEINK_UPSTREAM_READER_REF','38280863a50988683c2c63ccd7096e872a48c411')
    upstream=subprocess.check_output(['git','show',ref+':src/activities/util/KeyboardEntryActivity.cpp'],cwd=ROOT,text=True)
    code=harness.replace(metrics_code(),metrics_code(ref))
    for name in names:
        match=re.search(r'^[^\n]*KeyboardEntryActivity::'+name+r'\(.*?\n}',upstream,re.M|re.S)
        if match: code=code.replace(method(name),match.group())
    upstream_render=upstream[upstream.index('  interactions.beginPublishCycle();'):upstream.index('  interactionsReady = true;')]
    code=code.replace(render,upstream_render)
    reference=run_harness(code,tmp,'upstream')
    for (scene,theme),trace in regular.items():
        if theme!=5: assert reference[scene,theme]==trace, ('fixed upstream',scene,theme)
    print(f'{len(regular)//7} X3/X4 and {len(high)//7} ReadPico scenarios across 7 real theme metrics; four orientations, input layers, hit boundaries and one-shot holds passed')
    print('6 non-INX themes match fixed upstream; regular INX keys match Lyra. High-DPI geometry checked separately.')
