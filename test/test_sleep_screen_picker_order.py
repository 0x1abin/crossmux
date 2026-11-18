"""Exercise actual sleep picker labels, highlighting and deferred raw-value writes."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
activity = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text()
settings = (ROOT / "src/CrossPointSettings.h").read_text()
base = (ROOT / "src/SettingsList.cpp").read_text()
mode = re.search(r"enum SLEEP_SCREEN_MODE \{.*?\n  };", settings, re.S).group()
start = base.index("  static constexpr auto SLEEP_SCREEN_VALUES")
labels = base[start:base.index("  }();", start) + len("  }();")]
start = activity.index("    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);", activity.index("void SettingsActivity::toggleSetting("))
branch = activity[start:activity.index("  } else if (setting.type == SettingType::VALUE", start)]
ids = sorted(set(re.findall(r"StrId::(\w+)", labels)))
program = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>
struct CrossPointSettings {
@MODE@
 uint8_t sleepScreen=0, quickResumeSleepScreen=0, other=0, persisted=0;
 int saves=0;
 bool saveToFile(){persisted=sleepScreen;++saves;return true;}
} SETTINGS;
enum class StrId { @IDS@ };
@LABELS@
struct SettingInfo {
 StrId nameId{};
 uint8_t CrossPointSettings::* valuePtr=&CrossPointSettings::sleepScreen;
 std::span<const StrId> enumLabels() const {return SLEEP_SCREEN_VALUES;}
};
struct UITheme {
 bool inx=false;bool hasMainTabs() const{return inx;}
 static UITheme& getInstance(){static UITheme theme;return theme;}
};
UITheme& GUI=UITheme::getInstance();
struct Popup {
 std::vector<StrId> labels;
 int current=-1;
 std::function<void(int)> onSelect;
 void show(StrId,const StrId* options,int count,int selected,std::function<void(int)> callback){
  labels.assign(options,options+count);current=selected;onSelect=std::move(callback);
 }
};
struct SettingsActivity {
 Popup optionPopup;
 int refreshes=0;
 void requestUpdate(){}
 void refreshAfterSettingChange(uint8_t CrossPointSettings::*){++refreshes;}
 void syncQuickResumeTimeoutForSleepScreen(bool,bool){}
 void toggle(const SettingInfo& setting){
  const bool sleepScreenChanged=setting.valuePtr==&CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged=setting.valuePtr==&CrossPointSettings::quickResumeSleepScreen;
@BRANCH@
 }
};
int main(){
 static_assert(CrossPointSettings::BLANK==4 && CrossPointSettings::COVER_CUSTOM==5);
 const std::array<StrId,9> upstream={StrId::STR_DARK,StrId::STR_LIGHT,StrId::STR_CUSTOM,StrId::STR_COVER,
  StrId::STR_COVER_CUSTOM,StrId::STR_NONE_OPT,StrId::STR_QUICK_RESUME,StrId::STR_TRANSPARENT,StrId::STR_COVER_TRANSPARENT};
 // The persistence/Web descriptor continues to describe original raw IDs.
 assert(SLEEP_SCREEN_VALUES[4]==StrId::STR_NONE_OPT && SLEEP_SCREEN_VALUES[5]==StrId::STR_COVER_CUSTOM);
 for(bool inx:{false,true}) for(int initial=0;initial<9;++initial) for(bool changeTheme:{false,true}){
  SETTINGS.sleepScreen=initial;SETTINGS.saves=0;GUI.inx=inx;
  SettingsActivity cancelled;cancelled.toggle({});
  assert(SETTINGS.saves==0 && SETTINGS.sleepScreen==initial);
  for(int row=0;row<9;++row) {
   SETTINGS.sleepScreen=initial;SETTINGS.saves=0;GUI.inx=inx;
   SettingsActivity host;host.toggle({});
   assert(host.optionPopup.labels.size()==9);
   for(int i=0;i<9;++i)assert(host.optionPopup.labels[i]==(inx?SLEEP_SCREEN_VALUES[i]:upstream[i]));
   assert(host.optionPopup.labels[host.optionPopup.current]==SLEEP_SCREEN_VALUES[initial]);
   if(changeTheme)GUI.inx=!inx; // Callback must use the mapping chosen when opened.
   host.optionPopup.onSelect(row);
   int raw=inx?row:(row==4?5:row==5?4:row);
   assert(SETTINGS.sleepScreen==raw && SETTINGS.persisted==raw && SETTINGS.saves==1);
   SETTINGS.sleepScreen=0;SETTINGS.sleepScreen=SETTINGS.persisted;
   assert(SLEEP_SCREEN_VALUES[SETTINGS.sleepScreen]==host.optionPopup.labels[row]);
  }
 }
 // Another static enum must retain raw presentation even in non-INX themes.
 SETTINGS.other=5;GUI.inx=false;SettingsActivity generic;SettingInfo other;other.valuePtr=&CrossPointSettings::other;
 generic.toggle(other);assert(generic.optionPopup.current==5);
 assert(generic.optionPopup.labels[4]==StrId::STR_NONE_OPT);
 generic.optionPopup.onSelect(4);assert(SETTINGS.other==4);
}
""".replace("@MODE@", mode).replace("@IDS@", ",".join(ids)).replace("@LABELS@",labels).replace("@BRANCH@",branch)
with tempfile.TemporaryDirectory(prefix="sleep-picker-") as directory:
    source=Path(directory)/"picker.cpp";binary=Path(directory)/"picker"
    source.write_text(program)
    subprocess.run(["c++","-std=c++20","-Wall","-Wextra","-Werror",str(source),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
# The raw persistence descriptor must not become a theme-dependent DynamicEnum.
assert re.search(r'SettingInfo::StaticEnum\(StrId::STR_SLEEP_SCREEN,\s*&CrossPointSettings::sleepScreen,\s*SLEEP_SCREEN_VALUES',base)
print("Actual sleep picker: all nine initial values/options, INX/raw IDs, cancel, deferred theme change and save/reload mapping pass")
