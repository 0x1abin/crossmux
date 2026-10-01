#!/usr/bin/env python3
"""Exercise production keyboard layout, render setup and input with fixed host metrics."""
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
         'insertUtf8', 'backspaceUtf8', 'activateValue', 'clearAllOrAltOnSelected', 'keyboardRect')
methods = [method(name) for name in names]
render = source[source.index('  interactions.beginPublishCycle();'):source.index('  interactionsReady = true;')]
confirm = source[source.index('  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm))'):source.index('  if (mappedInput.wasReleased(MappedInputManager::Button::Back))')]
trace_target = (HERE / 'InxStyleParity.cpp').read_text().split('#ifdef UPSTREAM_THEME_PARITY')[0]
tables = source[source.index('namespace {'):source.index('void KeyboardEntryActivity::onEnter')]
layout_header = (ROOT / 'src/activities/util/KeyboardLayoutSet.h').read_text()
layout_source = (ROOT / 'src/activities/util/KeyboardLayoutSet.cpp').read_text()
layout_code = re.sub(r'^#(?:include|pragma)[^\n]*', '', layout_header + layout_source, flags=re.M)

harness = r'''
#include <cassert>
#include <string>
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
@LANGUAGES@
@TABLES@
struct Metrics {
  int keyboardKeySpacing=0, keyboardKeyHeight=48, keyboardWidthPercent=94;
  int buttonHintsHeight=40, verticalSpacing=0, keyboardVerticalOffset=-7;
} metrics;
struct UITheme {
  static UITheme& getInstance() { static UITheme value; return value; }
  const Metrics& getMetrics() const { return metrics; }
};
struct Renderer {
  int width=480, height=800; bool touch=false;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
} renderer;
namespace freeink::ui {
struct GfxRendererTarget : TraceTarget {
  static constexpr int FONT_SMALL=0;
  Renderer& renderer;
  explicit GfxRendererTarget(Renderer& r) : renderer(r) {}
  void setFont(int slot, int font) { std::printf("font %d %d\n", slot, font); }
  void setTextCentering(TextCentering mode) { std::printf("centering %d\n", int(mode)); }
  DeviceContext deviceContext() const {
    DeviceContext d; d.width=renderer.width; d.height=renderer.height; d.hasTouch=renderer.touch; return d;
  }
};
}
@ALIGNMENT@
constexpr int SMALL_FONT_ID=0, UI_12_FONT_ID=1;
constexpr const char *STR_OK_BUTTON="OK", *STR_KEY_SHIFT="Shift", *STR_KEY_MODE_ABC="abc", *STR_KEY_MODE_SYMBOLS="?123";
const char* tr(const char* s) { return s; }
unsigned long millis() { return 1000; }
struct MappedInputManager {
  enum class Button { Confirm };
  bool down=false, up=false, held=false; unsigned long time=0;
  bool wasPressed(Button) const { return down; }
  bool wasReleased(Button) const { return up; }
  bool isPressed(Button) const { return held; }
  unsigned long getHeldTime() const { return time; }
};
struct KeyboardEntryActivity {
  fui::KeyboardLayoutId layoutId=fui::KeyboardLayoutId::QwertyEn;
  InputType inputType=InputType::Text;
  bool shifted=false, symbols=false, showLangKey=true, urlPanel=false, cursorMode=false;
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
    @RENDER@
    assert(props.geometry==fui::KeyboardGeometry::Separated && props.rowGap==6 && props.keyRadius==3);
    for (size_t i=0; i<interactions.count(); ++i) {
      const auto& h=interactions.data()[i];
      assert(h.rect.x>=0 && h.rect.y>=0 && h.rect.right()<=renderer.width && h.rect.bottom()<=renderer.height);
      std::printf("hit %d %d %d %d %d %d\n", h.rect.x,h.rect.y,h.rect.width,h.rect.height,h.action,h.value);
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
  for (bool landscape : {false,true}) for (bool touch : {false,true}) {
    renderer.width=landscape?800:480; renderer.height=landscape?480:800; renderer.touch=touch;
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
          std::printf("SCENE %d %d\n",scene,theme);
          for (int r=0; r<layout.rowCount; ++r) for (int c=0; c<layout.rows[r].count; ++c) {
            const auto& k=layout.rows[r].keys[c];
            assert(kb.syncSelectionToValue(k.value));
            assert(kb.selectedKey()->value==k.value);
            std::printf("key %d %s %s\n",k.value,k.output?k.output:"",k.alt?k.alt:"");
          }
          kb.selRow=kb.selCol=0;
          kb.moveSelectionCol(-1); assert(kb.selCol==layout.rows[0].count-1);
          kb.moveSelectionCol(1); assert(kb.selCol==0);
          kb.moveSelectionRow(-1); assert(kb.selRow==layout.rowCount-1);
          kb.moveSelectionRow(1); assert(kb.selRow==0);
          kb.draw();
        }
  }
  std::puts("INPUT CHECKS");
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
replacements = {'TRACE': trace_target, 'LANGUAGES': layout_code, 'TABLES': tables, 'ALIGNMENT': alignment,
                'DECLARATIONS': '\n'.join(m.split('{', 1)[0].replace('KeyboardEntryActivity::', '') + ';' for m in methods),
                'CONFIRM': confirm, 'RENDER': render, 'METHODS': '\n'.join(methods)}
for name, code in replacements.items():
    harness = harness.replace('@' + name + '@', code)
with tempfile.TemporaryDirectory(prefix='unified-keyboard-', dir=os.environ.get('TMPDIR')) as tmp:
    cpp, binary = Path(tmp) / 'keyboard.cpp', Path(tmp) / 'keyboard'
    cpp.write_text(harness)
    ui = SDK / 'libs/ui/FreeInkUI'
    subprocess.run(['c++', '-std=c++20', '-I'+str(ui/'include'), str(cpp),
                    str(ui/'src/FreeInkUI.cpp'), '-o', str(binary)], check=True)
    output = subprocess.check_output([str(binary)], text=True)
    baseline = {}
    for block in output.split('INPUT CHECKS')[0].split('SCENE ')[1:]:
        name, trace = block.split('\n', 1)
        scene, theme = map(int, name.split())
        digest = hashlib.sha256(trace.encode()).hexdigest()
        if theme == 0:
            baseline[scene] = digest
        else:
            assert baseline[scene] == digest, (scene, theme)
    print(f'{len(baseline)} keyboard scenarios match across 7 themes; navigation, language switching and one-shot holds passed')
