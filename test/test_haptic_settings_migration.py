#!/usr/bin/env python3
"""Exercise production legacy haptic migration and board-specific action IDs."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "src/CrossPointSettings.h").read_text()
source = (ROOT / "src/CrossPointSettings.cpp").read_text()
selectors = (ROOT / "src/SettingsList.h").read_text()


def enum(name):
    return re.search(r"  enum " + name + r" \{.*?\};", header, re.S).group()


start = source.index('  if (doc["vibration"].isNull()')
end = source.index("  // Legacy migration: the aggregate statusBar", start)
migration = source[start:end]
clamp = re.search(r"  auto clamp = .*?; \};", source, re.S).group()
functions = []
for name in ("longPressMenuDisplayValue", "setLongPressMenuFromDisplayValue",
             "shortPowerButtonDisplayValue", "setShortPowerButtonFromDisplayValue"):
    functions.append(re.search(r"inline [^\n]* " + name + r"\(.*?\n\}", selectors, re.S).group())
program = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
struct JsonField {
  bool empty; int value;
  bool isNull() const { return empty; }
  template<class T> T as() const { return static_cast<T>(value); }
};
struct JsonDocument {
  std::map<std::string,int> values;
  JsonField operator[](const char* key) const {
    const auto it=values.find(key);return {it==values.end(),it==values.end()?0:it->second};
  }
};
struct CrossPointSettings {
@ENUMS@
  uint8_t vibration=VIBRATION_OFF, hapticIntensity=HAPTIC_HIGH;
  uint8_t longPressMenuFunction=LP_MENU_DISABLED, shortPwrBtn=IGNORE;
  bool migrate(const JsonDocument& doc) {
    bool needsResave=false;
@CLAMP@
@MIGRATION@
    return needsResave;
  }
} SETTINGS;
namespace BoardConfig {
  bool touch=false,home=false;
  bool hasTouch(){return touch;}bool hasHomeKey(){return home;}
}
@FUNCTIONS@
int main() {
  static_assert(CrossPointSettings::PWR_CONFIRM==5 && CrossPointSettings::CREATE_CLIPPING==6);
  static_assert(CrossPointSettings::LP_MENU_READER_MENU==4 && CrossPointSettings::LP_MENU_CREATE_CLIPPING==5);
  for(int legacy=0;legacy<4;++legacy) {
    CrossPointSettings s;JsonDocument doc{{{"hapticFeedbackLevel",legacy}}};
    assert(s.migrate(doc));
    assert(s.vibration==(legacy==0?CrossPointSettings::VIBRATION_OFF:CrossPointSettings::VIBRATION_TOUCH_PAGE));
    if(legacy)assert(s.hapticIntensity==legacy-1);
  }
  CrossPointSettings fresh; assert(!fresh.migrate({}));
  assert(fresh.vibration==CrossPointSettings::VIBRATION_OFF && fresh.hapticIntensity==CrossPointSettings::HAPTIC_HIGH);
  CrossPointSettings explicitNew;explicitNew.vibration=CrossPointSettings::VIBRATION_TOUCH;
  assert(!explicitNew.migrate({{{"vibration",0},{"hapticFeedbackLevel",3}}}));
  assert(explicitNew.vibration==CrossPointSettings::VIBRATION_TOUCH);
  CrossPointSettings invalid;assert(invalid.migrate({{{"hapticFeedbackLevel",255}}}));
  assert(invalid.vibration==CrossPointSettings::VIBRATION_TOUCH_PAGE && invalid.hapticIntensity==CrossPointSettings::HAPTIC_MEDIUM);
  for(bool home:{false,true})for(bool touch:{false,true}) {
    BoardConfig::home=home;BoardConfig::touch=touch;
    for(int display=0;display<(home?6:5);++display) {
      setLongPressMenuFromDisplayValue(display);assert(longPressMenuDisplayValue()==display);
      if(display==(home?5:4))assert(SETTINGS.longPressMenuFunction==CrossPointSettings::LP_MENU_CREATE_CLIPPING);
    }
    for(int display=0;display<(touch?7:6);++display) {
      setShortPowerButtonFromDisplayValue(display);assert(shortPowerButtonDisplayValue()==display);
      if(display==(touch?6:5))assert(SETTINGS.shortPwrBtn==CrossPointSettings::CREATE_CLIPPING);
    }
    SETTINGS.shortPwrBtn=CrossPointSettings::PWR_CONFIRM;
    assert(shortPowerButtonDisplayValue()==(touch?5:CrossPointSettings::IGNORE));
    assert(SETTINGS.shortPwrBtn==CrossPointSettings::PWR_CONFIRM);
    SETTINGS.longPressMenuFunction=CrossPointSettings::LP_MENU_READER_MENU;
    assert(longPressMenuDisplayValue()==(home?4:CrossPointSettings::LP_MENU_DISABLED));
    assert(SETTINGS.longPressMenuFunction==CrossPointSettings::LP_MENU_READER_MENU);
  }
}
'''
program = (program.replace("@ENUMS@", "\n".join(enum(n) for n in (
    "HAPTIC_FEEDBACK_LEVEL", "VIBRATION", "HAPTIC_INTENSITY", "LONG_PRESS_MENU_FUNCTION", "SHORT_PWRBTN")))
    .replace("@CLAMP@", clamp).replace("@MIGRATION@", migration)
    .replace("@FUNCTIONS@", "\n".join(functions)))
with tempfile.TemporaryDirectory(prefix="haptic-settings-") as directory:
    cpp = Path(directory) / "check.cpp"
    binary = Path(directory) / "check"
    cpp.write_text(program)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Legacy haptic levels, explicit new settings, defaults and 4 board-dependent selector mappings pass")
