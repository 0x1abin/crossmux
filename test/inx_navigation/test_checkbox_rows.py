#!/usr/bin/env python3
"""Run the production settings row builder and shared checkbox controls."""
from pathlib import Path
import tempfile
from test_theme_menus import method, run

ROOT = Path(__file__).resolve().parents[2]
base = (ROOT / 'src/components/themes/BaseTheme.cpp').read_text()
settings = (ROOT / 'src/activities/settings/SettingsActivity.cpp').read_text()
start = settings.index('  const auto& settings = *currentSettings;', settings.index('void SettingsActivity::buildScreen'))
end = settings.index('  fui::ListProps props;', start)
helper_start=settings.index('  const auto applyCheckbox',settings.index('void SettingsActivity::buildScreen'))
helper_end=settings.index('  const auto& metrics',helper_start)
program = r'''
#include <cassert>
#include <functional>
#include <string>
#include <vector>
#include "components/lists/list.h"
namespace fui=freeink::ui;
struct CrossPointSettings { static constexpr int INX=5; int uiTheme=0; uint8_t value=0; } SETTINGS;
struct BaseTheme { static void setCheckboxRow(fui::ListItem&,bool); } GUI;
@GUARD@
enum class StrId { STR_STATE_OFF, STR_STATE_ON, OTHER };
enum class SettingType { TOGGLE, ENUM, ACTION };
struct SettingInfo {
 SettingType type=SettingType::TOGGLE;
 uint8_t CrossPointSettings::*valuePtr=&CrossPointSettings::value;
 std::function<uint8_t()> valueGetter;
 std::vector<StrId> labels;
 std::vector<std::string> enumStringValues;
 const auto& enumLabels()const{return labels;}
};
std::vector<SettingInfo> entries(5);
auto* currentSettings=&entries;
std::string rowValues_[5];
fui::ListItem rowItems_[5];
std::string settingValueText(const SettingInfo&){return "historical value";}
void refresh(){ @ROWS@ }
int main(){
 entries[1].type=SettingType::ENUM;
 entries[1].labels={StrId::STR_STATE_OFF,StrId::STR_STATE_ON};
 entries[1].valuePtr=nullptr;entries[1].valueGetter=[](){return SETTINGS.value;};
 entries[2].type=SettingType::ENUM;entries[2].labels={StrId::STR_STATE_OFF,StrId::STR_STATE_ON,StrId::OTHER};
 entries[3].type=SettingType::ENUM;entries[3].labels=entries[1].labels;entries[3].enumStringValues={"Off","On"};
 entries[4].type=SettingType::ACTION;
 // Reuse the same row storage across theme/state changes to catch stale toggles.
 for(int theme:{0,1,2,3,4,6,5,0,5}) for(int checked:{0,1}) {
  SETTINGS.uiTheme=theme;SETTINGS.value=checked;refresh();
  assert(SETTINGS.value==checked && SETTINGS.uiTheme==theme);
  for(int i=0;i<5;++i){
   const bool checkbox=i<2;
   assert(rowItems_[i].toggle==checkbox);
   if(checkbox){assert(rowItems_[i].value==nullptr);assert(rowItems_[i].toggleChecked==bool(checked));}
   else assert(std::string(rowItems_[i].value)=="historical value");
  }
 }
}
'''.replace('@GUARD@',method(base,'BaseTheme::setCheckboxRow')).replace('@ROWS@',settings[helper_start:helper_end]+settings[start:end])
with tempfile.TemporaryDirectory(prefix='checkbox-rows-') as directory:
    run(program,Path(directory),sdk=True)
print('Production checkbox rows: all themes, both states, pointer/getter, multi-value and INX checkboxes pass')

# Exercise the real per-page typography guards as well as their checkbox data.
paths = ['reader/EpubReaderMenuActivity.cpp', 'settings/LanguageSelectActivity.cpp',
         'settings/KOReaderSettingsActivity.cpp', 'settings/OpdsSettingsActivity.cpp']
for path in paths:
    text = (ROOT / 'src/activities' / path).read_text()
    start = text.index('  if (SETTINGS.uiTheme != CrossPointSettings::INX)', text.index('  fui::ListProps props;', text.index('void ' + Path(path).stem + '::buildScreen')))
    guard = text[start:text.index('  syncListViewport(screen, props);', start)]
    code = r'''
#include <FreeInkApp.h>
#include <cassert>
namespace fui=freeink::ui;
struct CrossPointSettings {static constexpr int INX=5; int uiTheme;} SETTINGS;
struct Screen {fui::ThemeTokens tokens; const auto& theme()const{return tokens;}} screen;
int main(){screen.tokens.smallText.font=17;for(int theme:{0,1,2,3,4,5,6}){
 SETTINGS.uiTheme=theme;fui::ListProps props;
 @GUARD@
 assert(props.labelText.font==(theme==5?0:17));
 if(theme!=5)assert(props.labelText.maxLines==2);
}}
'''.replace('@GUARD@', guard)
    with tempfile.TemporaryDirectory(prefix='menu-font-') as directory:
        run(code, Path(directory), sdk=True)
print('Production reader/language/KOReader/OPDS label typography guards pass')
