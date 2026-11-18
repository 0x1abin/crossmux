"""Compile production control grouping and cached dictionary visibility."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

activity = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text()
header = (ROOT / "src/activities/settings/SettingsActivity.h").read_text()
settings = (ROOT / "src/SettingsList.h").read_text()
grouping = method(activity, "bool belongsToControlsGroup(")
settings_header = (ROOT / "src/CrossPointSettings.h").read_text()
power_enum = re.search(r"enum SHORT_PWRBTN[^}]+};", settings_header).group()
power_helpers = "\n".join(method(settings, name) for name in [
    "inline std::vector<StrId> buildShortPowerButtonValues()",
    "inline std::span<const uint8_t> doublePowerButtonValues()",
    "inline SettingInfo buildDoublePowerButtonSetting()"] )
fields = sorted(set(re.findall(r"&CrossPointSettings::(\w+)", grouping)) | {"vocabularyHistoryEnabled", "doublePwrBtn"})
code = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <initializer_list>
#include <string>
#include <vector>
struct CrossPointSettings {
""" + power_enum + "\n" + "".join("uint8_t " + field + " = 0;\n" for field in fields) + r"""
} SETTINGS;
enum class SettingAction { None, TapZones };
enum class StrId { STR_NONE_OPT, STR_CAT_READER, STR_CAT_CONTROLS,
 STR_IGNORE,STR_SLEEP,STR_PAGE_TURN,STR_FORCE_REFRESH,STR_FOOTNOTES,STR_CONFIRM,STR_SAVE_CLIPPING,
 STR_FRONTLIGHT,STR_POWER_DOUBLE_CLICK };
struct SettingInfo {
 const char* key = nullptr;
 uint8_t CrossPointSettings::* valuePtr = nullptr;
 SettingAction action = SettingAction::None;
 StrId category = StrId::STR_CAT_CONTROLS;
 bool hidden = false, available = true;
 std::vector<StrId> labels;
 std::function<uint8_t()> getter;
 std::function<void(uint8_t)> setter;
 uint8_t valueCount = 0;
 static SettingInfo DynamicEnum(StrId,std::vector<StrId> values,std::function<uint8_t()> get,
  std::function<void(uint8_t)> set,const char* key,StrId category,uint8_t CrossPointSettings::* ptr,uint8_t count){
  SettingInfo s;s.labels=std::move(values);s.getter=std::move(get);s.setter=std::move(set);
  s.key=key;s.category=category;s.valuePtr=ptr;s.valueCount=count;return s;
 }
};
""" + re.search(r"enum class ControlsGroup[^;]+;", header).group() + "\n" + grouping + r"""
struct UITheme {
 bool mainTabs = false;
 static UITheme& getInstance(){static UITheme theme;return theme;}
 bool hasMainTabs()const{return mainTabs;}
};
struct SettingsActivity {bool usesControlsGroups()const;};
""" + method(activity, "bool SettingsActivity::usesControlsGroups()") + r"""
struct SdCardFontRegistry {};
struct DictionaryEntry {};
constexpr const char* DICTIONARY_SETTING_KEY = "dictionaryName";
struct BoardConfig {
 static inline bool touch=false;
 static bool hasHomeKey(){return false;}
 static bool hasTouch(){return touch;}
};
namespace home_button {
 constexpr const char* KEYS[3] = {};
 constexpr int GESTURE_LABELS[3] = {}, ACTION_LABELS[3] = {};
 uint8_t CrossPointSettings::* FIELDS[3] = {};
}
struct VisitorSetting : SettingInfo {};
std::vector<SettingInfo> base;
SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>&){return {DICTIONARY_SETTING_KEY};}
bool isHiddenOnDevice(const SettingInfo& s){return s.hidden;}
bool isSettingAvailableOnBoard(const SettingInfo& s){return s.available;}
void buildFontOptions(SettingInfo&,const SdCardFontRegistry*){}
template<class F>void forEachBaseSetting(F f){for(auto s:base)f(s);}
"""
# The unreachable Home-key branch still needs its factory declaration.
code = code.replace(" bool hidden = false, available = true;", r"""
 bool hidden = false, available = true;
 static SettingInfo StaticEnum(int,uint8_t CrossPointSettings::*,const int*,const char*,StrId){return {};}
""")
start = settings.index("template <typename Wants, typename Visit>\nvoid forEachSetting(")
code += power_helpers + "\n"
code += method(settings[start:], "template <typename Wants, typename Visit>")
code += r"""
int main(){
 SettingsActivity activity;
 for(bool touch:{false,true}){
  BoardConfig::touch=touch;
  auto descriptor=buildDoublePowerButtonSetting();
  auto values=doublePowerButtonValues();
  assert(descriptor.labels.size()==values.size());
  assert(descriptor.labels.back()==StrId::STR_FRONTLIGHT);
  assert(descriptor.valuePtr==&CrossPointSettings::doublePwrBtn);
  assert(descriptor.valueCount==CrossPointSettings::SHORT_PWRBTN_COUNT);
  for(size_t i=0;i<values.size();++i){
   descriptor.setter(i);assert(SETTINGS.doublePwrBtn==values[i]);assert(descriptor.getter()==i);
  }
  descriptor.setter(255);assert(SETTINGS.doublePwrBtn==CrossPointSettings::IGNORE);
  if(!touch){SETTINGS.doublePwrBtn=CrossPointSettings::PWR_CONFIRM;assert(descriptor.getter()==0);}
 }
 for(bool inx:{false,true}){
  UITheme::getInstance().mainTabs=inx;
  assert(activity.usesControlsGroups()==!inx);
 }
 using G=ControlsGroup;
 auto field=[](uint8_t CrossPointSettings::* ptr){SettingInfo s;s.valuePtr=ptr;return s;};
 for(auto ptr:{&CrossPointSettings::sideButtonLayout,&CrossPointSettings::sideButtonFollowOrientation}){
  assert(belongsToControlsGroup(field(ptr),G::Side));
  assert(!belongsToControlsGroup(field(ptr),G::Root));
 }
 auto shared=field(&CrossPointSettings::longPressButtonBehavior);
 assert(belongsToControlsGroup(shared,G::Front)&&belongsToControlsGroup(shared,G::Side));
 for(const char* key:{"shortPwrBtn","doublePwrBtn"}){
  SettingInfo s;s.key=key;
  assert(belongsToControlsGroup(s,G::Power)&&!belongsToControlsGroup(s,G::Root));
 }
 SettingInfo zones;zones.action=SettingAction::TapZones;
 assert(belongsToControlsGroup(zones,G::Touch));
 assert(!belongsToControlsGroup(zones,G::Root));
 assert(belongsToControlsGroup(field(&CrossPointSettings::touchReaderControls),G::Touch));
 assert(belongsToControlsGroup(field(&CrossPointSettings::frontButtonFollowOrientation),G::Front));
 SettingInfo extension;extension.key="slideSwitchAction";
 assert(belongsToControlsGroup(extension,G::Root));
 SettingInfo history=field(&CrossPointSettings::vocabularyHistoryEnabled);
 history.key="vocabularyHistoryEnabled";history.category=StrId::STR_CAT_READER;
 SettingInfo controls;controls.key="controls";
 base={history,controls};
 for(bool discovered:{false,true})for(bool cached:{false,true})for(bool filter:{false,true}){
  std::vector<DictionaryEntry> dictionaries(discovered?1:0);
  std::vector<std::string> seen;
  forEachSetting(nullptr,&dictionaries,[&](const char* key){return !filter||std::strcmp(key,DICTIONARY_SETTING_KEY)!=0;},
   [&](SettingInfo& s){seen.push_back(s.key);},cached);
  const std::vector<std::string> expected=discovered&&!filter
   ?std::vector<std::string>{"dictionaryName","vocabularyHistoryEnabled","controls"}
   :discovered||cached?std::vector<std::string>{"vocabularyHistoryEnabled","controls"}
   :std::vector<std::string>{"controls"};
  assert(seen==expected);
 }
 base[0].available=false;
 std::vector<DictionaryEntry> dictionaries(1);
 std::vector<std::string> seen;
 forEachSetting(nullptr,&dictionaries,[](const char*){return true;},[&](SettingInfo& s){seen.push_back(s.key);});
 assert((seen==std::vector<std::string>{"dictionaryName","controls"}));
}
"""
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "controls.cpp"
    binary = Path(directory) / "controls"
    source.write_text(code)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Production control groups, INX flat-menu gate, shared bindings and cached dictionary/history visibility pass")

# Compare the actual row-style expressions with the pinned upstream, using real
# touch capability rather than replacing the production style with fixed metrics.
upstream = subprocess.check_output(["git", "-C", str(ROOT), "show",
    "622e5de9558399c92720e8f76deaed8a0cb1b6d3:src/activities/settings/SettingsActivity.cpp"], text=True)
def row_style(source):
    return re.findall(r"props\.labelText\s*=\s*([^;]+);", method(source, "void SettingsActivity::buildScreen("))[-1]
font_code = r"""
#include <cassert>
enum class SettingsSection { End, General, Controls };
struct Theme { int smallText=10,bodyText=12; };
struct Screen { Theme value; const Theme& theme() const { return value; } };
struct Input { bool touch; bool hasTouch() const { return touch; } };
int candidate(bool touch, SettingsSection section) {
 Screen screen; Input mappedInput{touch}; (void)section; (void)mappedInput;
 return """ + row_style(activity) + r""";
}
int reference(bool touch, SettingsSection section) {
 Screen screen; Input mappedInput{touch}; (void)section; (void)mappedInput;
 return """ + row_style(upstream) + r""";
}
int main() {
 for (bool touch : {false,true})
  for (auto section : {SettingsSection::General,SettingsSection::Controls})
   assert(candidate(touch,section)==reference(touch,section));
}
"""
font_code = '#include <initializer_list>\n' + font_code
with tempfile.TemporaryDirectory() as directory:
    source=Path(directory)/"styles.cpp";binary=Path(directory)/"styles"
    source.write_text(font_code)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror",str(source),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("Actual fixed-section row-font expressions match upstream for touch and buttons")
