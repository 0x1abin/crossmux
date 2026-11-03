#!/usr/bin/env python3
"""Sync review check: production menus/tabs with fixed host metrics, not full pages.

Requires the reviewed Reader ba39565c and historical INX Git objects fetched by
the sync workflow; CI fetches full history for these fixed references.
FREEINK_SDK_ROOT selects the reviewed candidate; defaults to the pinned SDK.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

from test_upstream_theme_compat import compare, source

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
REF = os.environ.get('FREEINK_UPSTREAM_READER_REF', '98da17829cb3c8afbc50af491c81d440bbfd15b9')
SDK = Path(os.environ.get('FREEINK_SDK_ROOT', ROOT / 'freeink-sdk'))


def read(path, ref=None):
    return source(ref, path) if ref else (ROOT / path).read_text()


def method(text, name):
    return re.search(r'^[^\n]*' + name + r'\(.*?\n}', text, re.M | re.S).group()


def run(code, directory, sdk=False):
    cpp, binary = directory / 'check.cpp', directory / 'check'
    cpp.write_text(code)
    command = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers', '-I'+str(ROOT/'src'), str(cpp), '-o', str(binary)]
    if sdk:
        ui = SDK / 'libs/ui/FreeInkUI'
        command += ['-I' + str(ui / 'include'), str(ui / 'src/FreeInkUI.cpp')]
    subprocess.run(command, check=True)
    return subprocess.check_output([str(binary)], text=True)


def settings_metadata(ref=None):
    # Both fixed upstream and the fork store descriptors in the real visitor.
    # Extract descriptor metadata, then run each production grouping method below.
    declaration = read('src/SettingsList.h', ref)
    entries = method(read('src/SettingsList.cpp', ref), 'forEachBaseSetting')
    for helper in ('buildLongPressMenuSetting', 'buildShortPowerButtonSetting'):
        if f'emit({helper}())' in entries:
            entries = entries.replace(f'emit({helper}())', method(declaration, helper))
    result = []
    for entry in re.split(r'SettingInfo::\w+\(', entries)[1:]:
        ids = re.findall(r'StrId::(STR_\w+)', entry)
        if not ids or ids[0].startswith('STR_CAT_'):
            continue  # Dynamic Home-key labels are filtered by the real grouping method.
        fields = re.findall(r'&CrossPointSettings::(\w+)', entry)
        category = next((i for i in ids if i.startswith('STR_CAT_')), 'STR_NONE_OPT')
        action = re.search(r'SettingAction::(\w+)', entry)
        result.append((ids[0], category, fields[0] if fields else None,
                       '.withTextSettings()' in entry, '.withReadingStatsSettings()' in entry,
                       '.withWebHidden()' in entry, action.group(1) if action else 'None'))
    return result


def menus(directory):
    path = 'src/activities/settings/SettingsActivity.cpp'
    current = method(read(path), 'SettingsActivity::rebuildSettingsLists')
    incoming = read(path, REF)
    reference = method(incoming, 'SettingsActivity::rebuildSettings')
    helpers = []
    for result_type, name in [('SettingsSection', 'sectionFor'), ('bool', 'showOnDevice')]:
        body = re.search(r'^' + result_type + r' ' + name + r'\([^;\n]*\) \{.*?^}', incoming, re.M | re.S).group()
        helpers.append(body)
    upstream_code = '\n'.join(helpers) + '\n' + reference
    upstream_code = upstream_code.replace('sectionFor(', 'referenceSectionFor(').replace('showOnDevice(', 'referenceShowOnDevice(')
    upstream_code = upstream_code.replace('SettingsActivity::rebuildSettings', 'ReferenceSettingsActivity::rebuildSettings')
    # The section enum and display order are the fixed incoming six groups.
    section_path = 'src/activities/settings/SettingsSection.h'
    sections = re.search(r'enum class SettingsSection.*?};', read(section_path), re.S).group()
    assert sections == re.search(r'enum class SettingsSection.*?};', read(section_path, REF), re.S).group()
    titles = re.search(r'SETTINGS_SECTION_TITLES.*?};', read(section_path), re.S).group()
    assert titles == re.search(r'SETTINGS_SECTION_TITLES.*?};', read(section_path, REF), re.S).group()
    data = [settings_metadata(ref) for ref in (None, REF)]
    theme_values = method(read('src/SettingsList.h'), 'homeThemeValues')
    all_code = current + upstream_code + theme_values
    ids = sorted(set(re.findall(r'StrId::(\w+)', all_code)) |
                 {x for rows in data for row in rows for x in row[:2]} |
                 {'STR_NONE_OPT', 'STR_DICTIONARY', 'STR_CLOCK', 'STR_DATE_AND_TIME'})
    actions = sorted(set(re.findall(r'SettingAction::(\w+)', all_code)) |
                     {row[6] for rows in data for row in rows} | {'None'})
    fields = sorted(set(re.findall(r'&CrossPointSettings::(\w+)', all_code)) |
                    {row[2] for rows in data for row in rows if row[2]} | {'shortPwrBtn', 'uiTheme'})
    code = r'''#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstdint>
#define FREEINK_CAP_BLE_HID_HOST 1
#define FREEINK_DEVICE_MURPHY_M4 0
#define FREEINK_DEVICE_X4CLASSIC classic
bool inx=false, touch=false, pro=false, classic=false, dictionary=false, rtc=false, home=false;
bool grid=true, translated=false;
struct RenderLock {template<class T> explicit RenderLock(T&) {}};
struct UITheme { static bool supportsCoverGrid(){return grid;} };
'''
    code += sections + '\nconstexpr size_t SETTINGS_SECTION_COUNT=static_cast<size_t>(SettingsSection::End);\n'
    for enum, values in [('StrId', ids), ('SettingAction', actions)]:
        code += f'enum class {enum} {{' + ','.join(values) + '};\n'
        code += f'const char* name({enum} v) {{ switch(v) {{' + ''.join(
            f'case {enum}::{v}: return "{v}";' for v in values) + '} return "?"; }\n'
    code += 'struct CrossPointSettings { enum SHORT_PWRBTN { FOOTNOTES=1 };\n'
    code += ''.join(f'int {f}=7;\n' for f in fields) + 'bool operator==(const CrossPointSettings&) const = default; } SETTINGS;\n'
    code += r'''namespace BoardConfig {
 bool hasTouch(){return touch;} bool isX4Pro(){return pro;}
 bool isX4Classic(){return classic;} bool hasHomeKey(){return home;}
 bool isPicco(){return false;}
}
namespace home_button { bool isSetting(int CrossPointSettings::*) {return false;} }
namespace bleturner { struct Status {bool compiledIn=true;}; Status status(){return {};} }
struct { bool isAvailable(){return rtc;} } halClock;
struct SettingInfo {
 StrId nameId=StrId::STR_NONE_OPT, category=StrId::STR_NONE_OPT;
 int CrossPointSettings::*valuePtr=nullptr;
 bool inTextSettings=false, inReadingStatsSettings=false, webHidden=false;
 SettingAction action=SettingAction::None;
 std::vector<const char*> enumStringValues;
 static SettingInfo Action(StrId id, SettingAction action) {
   SettingInfo s; s.nameId=id; s.action=action; return s;
 }
};
const char* translate(StrId){return translated?"Translated None":"None";}
#define tr(id) translate(StrId::id)
struct DictionaryEntry {};
int discoveries=0;
namespace DictionaryRegistry { void discover(std::vector<DictionaryEntry>& entries) {++discoveries;if(dictionary) entries.emplace_back();} }
bool upstream=false;
std::vector<SettingInfo> getSettingsList(int*, std::vector<DictionaryEntry>* entries) {
 std::vector<SettingInfo> result;
'''
    for index, rows in enumerate(data):
        code += ('if (!upstream)' if index == 0 else 'else') + ' result = {\n'
        code += ',\n'.join('{StrId::%s, StrId::%s, %s, %s, %s, %s, SettingAction::%s}' % (
            a,b,'&CrossPointSettings::'+c if c else 'nullptr',str(d).lower(),str(e).lower(),str(f).lower(),g)
            for a,b,c,d,e,f,g in rows)
        code += '};\n'
    code += r'''if (dictionary && !entries->empty()) {
   SettingInfo row;row.nameId=StrId::STR_DICTIONARY;row.category=StrId::STR_CAT_READER;
   row.enumStringValues={tr(STR_NONE_OPT), "Dictionary"};
   auto pos=std::find_if(result.begin(),result.end(),[](const auto& setting){return setting.category==StrId::STR_CAT_CONTROLS;});
   result.insert(pos,std::move(row));
 }
 return result;
}
struct FontSystem {int value=0;void refreshIfDirty(){} int& registry(){return value;}};
struct SettingsActivity {
 std::array<std::vector<SettingInfo>,SETTINGS_SECTION_COUNT> categories;
 std::vector<SettingInfo>*currentSettings=nullptr;
 SettingsSection section=SettingsSection::End;
 int selectedCategoryIndex=0, settingsCount=0; bool resourcesDirty=true;
 FontSystem sdFontSystem;
 bool usesAccordion() {return inx;}
 void closeRouting(){} void rebuildRowItems(){}
 void rebuildSettingsLists();
};
struct ReferenceSettingsActivity {
 SettingsSection section=SettingsSection::General;std::vector<SettingInfo> settings;
 FontSystem sdFontSystem;void rebuildRowItems(){}void rebuildSettings();
};
'''
    code += re.search(r'constexpr size_t EXTRA_ROWS_PER_SECTION = \d+;', incoming).group() + '\n'
    code += current + '\n' + upstream_code + '\n' + theme_values
    code += r'''
int main() {
 grid=true;const auto themes=homeThemeValues();assert(themes.size()==7);
 grid=false;const auto compact=homeThemeValues();assert(compact.size()==6);
 assert(std::equal(compact.begin(),compact.end(),themes.begin()));grid=true;
 for (int flags=0; flags<128; ++flags) {
  touch=flags&1; pro=flags&2; classic=flags&4; dictionary=flags&16;
  rtc=flags&32;home=flags&64;
  SETTINGS.shortPwrBtn=(flags&8)?CrossPointSettings::FOOTNOTES:0;
  for (int theme=0; theme<8; ++theme) {
   inx=theme==5; upstream=theme==7;SETTINGS.uiTheme=theme;
   const auto before=SETTINGS;
   if (upstream) {
    for (size_t i=0;i<SETTINGS_SECTION_COUNT;++i) {
     ReferenceSettingsActivity s;s.section=static_cast<SettingsSection>(i);s.rebuildSettings();
     assert(SETTINGS==before);
     std::printf("MENU %d %d %zu\n",flags,theme,i);
     for(const auto& row:s.settings)std::printf("%s %s\n",name(row.nameId),name(row.action));
    }
    continue;
   }
   SettingsActivity s;s.rebuildSettingsLists();assert(SETTINGS==before);
   const int scans=discoveries;const auto original=s.categories;
   translated=true;s.rebuildSettingsLists();translated=false;
   assert(discoveries==scans && SETTINGS==before);
   for(size_t i=0;i<SETTINGS_SECTION_COUNT;++i)assert(s.categories[i].size()==original[i].size());
   int dateTimeEntries=0,dictionaryEntries=0;
   for (const auto& row:s.categories[static_cast<size_t>(SettingsSection::General)]) {
    assert(row.nameId != StrId::STR_CLOCK);
    if(row.nameId==StrId::STR_DATE_AND_TIME){++dateTimeEntries;assert(row.action==SettingAction::ClockSettings);}
   }
   assert(dateTimeEntries==1);
   for(const auto& row:s.categories[static_cast<size_t>(SettingsSection::Reader)])if(row.nameId==StrId::STR_DICTIONARY){
    ++dictionaryEntries;assert(std::string(row.enumStringValues[0])=="Translated None");
   }
   assert(dictionaryEntries==(dictionary?1:0));
   for(size_t i=0;i<SETTINGS_SECTION_COUNT;++i){
    SettingsActivity fixed;fixed.section=static_cast<SettingsSection>(i);fixed.selectedCategoryIndex=i;
    fixed.rebuildSettingsLists();assert(SETTINGS==before);assert(fixed.currentSettings==&fixed.categories[i]);
    assert(fixed.settingsCount==int(s.categories[i].size()));
    for(size_t j=0;j<SETTINGS_SECTION_COUNT;++j){
     if(j!=i){assert(fixed.categories[j].empty());continue;}
     assert(fixed.categories[j].size()==s.categories[j].size());
     for(size_t k=0;k<s.categories[j].size();++k){assert(fixed.categories[j][k].nameId==s.categories[j][k].nameId);assert(fixed.categories[j][k].action==s.categories[j][k].action);}
    }
    std::printf("MENU %d %d %zu\n",flags,theme,i);
    for(const auto& row:s.categories[i])std::printf("%s %s\n",name(row.nameId),name(row.action));
   }
  }
 }
}
'''
    code = code.replace('#include <vector>', '#include <vector>\n#include <string>')
    output = run(code, directory)
    parsed = {}
    for block in re.split(r'^MENU ', output, flags=re.M)[1:]:
        lines = block.splitlines()
        parsed[tuple(map(int, lines[0].split()))] = lines[1:]
    for flags in range(128):
        for category in range(6):
            baseline = parsed[flags, 0, category]
            reference = parsed[flags, 7, category]
            common = set(baseline) & set(reference)
            assert [x for x in baseline if x in common] == [x for x in reference if x in common], (flags, category, baseline, reference)
            for theme in (1,2,3,4,6):
                assert parsed[flags,theme,category] == baseline, (flags,theme,category)
            inx_rows = parsed[flags,5,category]
            assert set(baseline) <= set(inx_rows), (flags,category,'shared setting hidden in INX')
            for row in baseline:
                assert not row.startswith(('STR_INX_RECENT_LAYOUT ', 'STR_INX_LIBRARY_LAYOUT ', 'STR_INX_APPS_LAYOUT ', 'STR_INX_TAB_POSITION '))
            extras = set(baseline) - set(reference)
            assert [x for x in baseline if x in extras] == [x for x in inx_rows if x in extras]
    print('Settings: 128 device/condition combinations × 7 themes × 6 sections; fixed upstream grouping/order, fixed-section parity, dictionary reuse, theme values and Date & Time pass')


def plugin_toggle(directory):
    production = read('src/activities/settings/SettingsActivity.cpp')
    start = production.index('case SettingAction::Plugins:')
    start = production.index('{', start)
    depth, end = 1, start + 1
    while depth:
        depth += (production[end] == '{') - (production[end] == '}')
        end += 1
    branch = production[start:end]
    value = re.search(r'if \(setting.action == SettingAction::Plugins\) return ([^;]+);', production).group(1)
    code = r'''#include <cassert>
#include <functional>
#include <string>
#include <vector>
enum class StrId {STR_PLUGINS,STR_ENABLED,STR_DISABLED,STR_CANCEL};
constexpr auto STR_ENABLED=StrId::STR_ENABLED, STR_DISABLED=StrId::STR_DISABLED;
const char* tr(StrId id){return id==STR_ENABLED?"Enabled":"Disabled";}
int saves=0,refreshes=0;
struct Settings {int pluginsEnabled=0,pluginHubPromptHidden=0;void saveToFile(){++saves;}} SETTINGS;
namespace pluginevents {void refreshSubscriptions(){++refreshes;}}
struct Popup {
 std::vector<StrId> options;int selected=-1;std::function<void(int)> choose;
 void show(StrId title,const StrId* values,int count,int initial,std::function<void(int)> callback){
  assert(title==StrId::STR_PLUGINS);options.assign(values,values+count);selected=initial;choose=std::move(callback);
 }
};
struct SettingsActivity {
 Popup optionPopup;int rebuilds=0,updates=0;
 void rebuildSettingsLists(){++rebuilds;}void requestUpdate(){++updates;}
 void showPluginToggle();
 const char* valueText(){return @VALUE@;}
};
// Catalog/navigation/downloader stubs are deliberately absent: the extracted
// production branch must operate locally without launching any of them.
void SettingsActivity::showPluginToggle() @BRANCH@
int main(){
 for(int enabled:{0,1})for(int hidden:{0,1})for(int choice:{0,1,2}){
  saves=refreshes=0;SETTINGS={enabled,hidden};SettingsActivity activity;
  assert(std::string(activity.valueText())==(enabled?"Enabled":"Disabled"));
  activity.showPluginToggle();
  assert(activity.optionPopup.options==std::vector<StrId>({StrId::STR_ENABLED,StrId::STR_DISABLED,StrId::STR_CANCEL}));
  assert(activity.optionPopup.selected==(enabled?0:1));assert(activity.updates==1);
  assert(saves==0&&refreshes==0&&activity.rebuilds==0);
  activity.optionPopup.choose(choice);
  if(choice==2){
   assert(SETTINGS.pluginsEnabled==enabled&&SETTINGS.pluginHubPromptHidden==hidden);
   assert(saves==0&&refreshes==0&&activity.rebuilds==0&&activity.updates==1);
  }else{
   assert(SETTINGS.pluginsEnabled==(choice==0));
   assert(SETTINGS.pluginHubPromptHidden==(choice==0?0:hidden));
   assert(saves==1&&refreshes==1&&activity.rebuilds==1&&activity.updates==2);
  }
  assert(std::string(activity.valueText())==(SETTINGS.pluginsEnabled?"Enabled":"Disabled"));
 }
}
'''
    run(code.replace('@BRANCH@', branch).replace('@VALUE@', value), directory)
    print('Plugin System: actual production popup/callback and value text pass 12 enable/disable/cancel states; Hub prompt reset and persistence/subscription refresh stay local')


def short_power_toggle(directory):
    production = read('src/activities/settings/SettingsActivity.cpp')
    declaration = read('src/activities/settings/SettingsActivity.h')
    factories = read('src/SettingsList.h')
    persistence = read('src/CrossPointSettings.cpp')

    def block(text, marker):
        start = text.index('{', text.index(marker))
        depth, end = 1, start + 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        return text[start:end]

    dynamic = block(production, 'else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter)')
    visible = re.search(r'if \((setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&\s*'
                        r'SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES)\)\s*return false;', production).group(1)
    helpers = '\n'.join(method(factories, name) for name in (
        'buildShortPowerButtonValues', 'shortPowerButtonDisplayValue',
        'setShortPowerButtonFromDisplayValue', 'buildShortPowerButtonSetting'))
    info = re.search(r'^struct SettingInfo \{.*?^};', declaration, re.M | re.S).group()
    enum = re.search(r'  enum SHORT_PWRBTN \{.*?\n  };', read('src/CrossPointSettings.h'), re.S).group()
    popup_buttons = block(read('src/components/OptionPopup.h'), 'bool handleButtons(')
    save = re.search(r'  doc\["shortPwrBtn"\] = shortPwrBtn;', persistence).group()
    load = re.search(r'  shortPwrBtn = clamp\(doc\["shortPwrBtn"\].*?;', persistence, re.S).group()
    clamp = re.search(r'  auto clamp = \[\].*?;', persistence).group() + ' };'
    ids = sorted(set(re.findall(r'StrId::(\w+)', info + helpers)))
    actions = sorted(set(re.findall(r'SettingAction::(\w+)', info)))
    code = r'''#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>
bool touch=false;
namespace BoardConfig {bool hasTouch(){return touch;}}
struct Doc {
 uint8_t value=0;
 uint8_t& operator[](const char* key){assert(std::string(key)=="shortPwrBtn");return value;}
 uint8_t operator[](const char* key)const{assert(std::string(key)=="shortPwrBtn");return value;}
};
struct CrossPointSettings {
 @ENUM@
 uint8_t shortPwrBtn=IGNORE,pwrBtnFootnoteBack=0,uiTheme=0,sleepScreen=0,quickResumeSleepScreen=0;
 int saves=0;
 void saveToFile(){++saves;}
 void save(Doc& doc)const{@SAVE@}
 void load(const Doc& doc){@CLAMP@ @LOAD@}
} SETTINGS;
'''.replace('@ENUM@', enum).replace('@SAVE@', save).replace('@CLAMP@', clamp).replace('@LOAD@', load)
    code += 'enum class StrId {' + ','.join(ids) + '};\n'
    code += re.search(r'enum class SettingType \{.*?};', declaration).group() + '\n'
    code += 'enum class SettingAction {' + ','.join(actions) + '};\n' + info + '\n' + helpers
    code += r'''
enum class SettingsSection {Controls,End};
struct Popup {
 bool active=false;int selectedIndex=0;
 std::vector<std::string> ownedStrings;
 std::function<void(int)> onSelectCallback;
 void show(StrId,const StrId*,int count,int initial,std::function<void(int)> callback){
  active=true;ownedStrings.resize(count);selectedIndex=initial;onSelectCallback=std::move(callback);
 }
 void show(StrId,const std::vector<std::string>& options,int initial,std::function<void(int)> callback){
  active=true;ownedStrings=options;selectedIndex=initial;onSelectCallback=std::move(callback);
 }
 bool handleButtons(bool previous,bool next,bool confirm,bool back,const std::function<void()>& requestUpdate) @POPUP@
};
struct Nav {std::atomic<int> selected=20;void follow(int count){assert(selected>=0&&selected<=count);}};
struct SettingsActivity {
 SettingsSection section=SettingsSection::Controls;bool accordion=false,footnoteVisible=false;
 int rebuilds=0,updates=0;Popup optionPopup;Nav nav;
 Nav& activeNav(){return nav;}bool usesAccordion(){return accordion;}
 int listCount(){return 7+int(footnoteVisible);}
 void requestUpdate(){++updates;}
 void applyUiSettingChange(uint8_t CrossPointSettings::*){assert(false);}
 void syncQuickResumeTimeoutForSleepScreen(bool sleep,bool quick){assert(!sleep&&!quick);}
 void rebuildSettingsLists(){
  ++rebuilds;SettingInfo setting;setting.valuePtr=&CrossPointSettings::pwrBtnFootnoteBack;
  footnoteVisible=!(@VISIBLE@);
 }
 void refreshAfterSettingChange(uint8_t CrossPointSettings::*);
 void toggleSetting(const SettingInfo& setting){
  const auto changedValuePtr=setting.valuePtr;
  const bool sleepScreenChanged=setting.valuePtr==&CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged=setting.valuePtr==&CrossPointSettings::quickResumeSleepScreen;
  @DYNAMIC@
 }
};
'''.replace('@POPUP@', popup_buttons).replace('@VISIBLE@', visible).replace('@DYNAMIC@', dynamic)
    code += method(production, 'SettingsActivity::refreshAfterSettingChange')
    code += r'''
int main(){
 for(bool hasTouch:{false,true})for(int mode=0;mode<3;++mode){
  touch=hasTouch;
  for(uint8_t initial:{uint8_t(CrossPointSettings::IGNORE),uint8_t(CrossPointSettings::FOOTNOTES)}){
   for(int operation=0;operation<4;++operation){
    SETTINGS.shortPwrBtn=initial;SETTINGS.saves=0;
    auto setting=buildShortPowerButtonSetting();
    assert(setting.valuePtr==&CrossPointSettings::shortPwrBtn);
    assert(setting.enumValueCount==CrossPointSettings::SHORT_PWRBTN_COUNT);
    assert(std::string(setting.key)=="shortPwrBtn");
    SettingsActivity page;page.section=mode==0?SettingsSection::Controls:SettingsSection::End;page.accordion=mode==2;
    page.rebuildSettingsLists();page.rebuilds=0;
    assert(page.footnoteVisible==(initial==CrossPointSettings::FOOTNOTES));
    page.toggleSetting(setting);assert(page.optionPopup.active&&page.updates==1);
    assert(page.optionPopup.selectedIndex==initial&&page.rebuilds==0&&SETTINGS.saves==0);
    const int selected=operation==0?int(CrossPointSettings::FOOTNOTES):operation==1?int(CrossPointSettings::IGNORE):initial;
    page.optionPopup.selectedIndex=selected;
    page.optionPopup.handleButtons(false,false,operation!=3,operation==3,[&]{page.requestUpdate();});
    const bool changed=operation!=3&&selected!=initial;
    assert(!page.optionPopup.active);
    assert(SETTINGS.shortPwrBtn==(changed?selected:initial));
    assert(page.rebuilds==int(changed)&&SETTINGS.saves==int(changed));
    assert(page.footnoteVisible==(SETTINGS.shortPwrBtn==CrossPointSettings::FOOTNOTES));
   }
  }
  // C3 omits Confirm in the picker but keeps persisted clipping at raw value 6.
  auto setting=buildShortPowerButtonSetting();const uint8_t clippingRow=hasTouch?6:5;
  assert(setting.enumLabels()[clippingRow]==StrId::STR_SAVE_CLIPPING);
  setting.valueSetter(clippingRow);assert(SETTINGS.shortPwrBtn==6&&setting.valueGetter()==clippingRow);
  Doc doc;SETTINGS.save(doc);assert(doc.value==6);SETTINGS.shortPwrBtn=0;SETTINGS.load(doc);assert(SETTINGS.shortPwrBtn==6);
  for(uint8_t raw=0;raw<CrossPointSettings::SHORT_PWRBTN_COUNT;++raw){
   SETTINGS.shortPwrBtn=raw;SETTINGS.save(doc);SETTINGS.shortPwrBtn=0;SETTINGS.load(doc);assert(SETTINGS.shortPwrBtn==raw);
  }
 }
}
'''
    run(code, directory)
    print('Short power: actual factory, dynamic popup, refresh and dependent visibility pass enter/leave Footnotes, same option and Cancel in fixed/shared/accordion settings; C3/touch clipping raw 6 and persisted values remain stable')


def actions(directory):
    reader = [method(read('src/activities/reader/EpubReaderMenuActivity.cpp', ref),
                     'EpubReaderMenuActivity::buildMenuItems') for ref in (None, REF, '9d02f498')]
    browser = read('src/activities/home/FileBrowserActivity.cpp')
    names = ('showEditMenu', 'executeEditAction', 'onRowLongPress', 'activateSelected')
    methods = [method(browser, 'FileBrowserActivity::'+name) for name in names]
    # The actual input guards precede storage/navigation; those services are out of scope.
    methods[-1] = methods[-1].split('  RenderLock lock(*this);')[0] + '  result=0;\n}'
    methods.append(method(browser, 'FileBrowserActivity::handleButtons').split('  if (mappedInput.wasReleased(MappedInputManager::Button::Back))')[0]+'  return false;\n}')
    ids = sorted(set(re.findall(r'(STR_\w+)', '\n'.join(reader+methods))))
    menu_actions = sorted(set(re.findall(r'MenuAction::(\w+)', '\n'.join(reader))))
    code = '''#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>
#include <variant>
#define LOG_ERR(...) ((void)0)
bool inx=false;
struct UITheme { static UITheme& getInstance(){static UITheme t;return t;} bool hasMainTabs(){return inx;} };
struct { bool light=false; bool present(){return light;} } Frontlight;
bool switchOwnsLight=false;bool slideSwitchOwnsLight(){return switchOwnsLight;}
namespace bleturner {struct Status {bool compiledIn=false;};Status state;Status status(){return state;}}
struct {char dictionaryName[2]={};} SETTINGS;
'''
    code += 'enum class StrId {'+','.join(ids)+'};\n'
    code += ''.join(f'constexpr StrId {i}=StrId::{i};\n' for i in ids)
    code += 'const char* tr(StrId id){ switch(id){'+''.join(f'case StrId::{i}: return "{i}";' for i in ids)+'} return "?";}\n'
    code += 'enum class MenuAction {'+','.join(menu_actions)+'};\n'
    code += '''struct MenuItem {MenuAction action; StrId labelId; bool operator==(const MenuItem&) const = default;};
struct EpubReaderMenuActivity {
 static constexpr int MAX_MENU_ITEMS=32;
 void buildMenuItems(std::vector<MenuItem>&,bool,bool,bool=false);
 void reference(std::vector<MenuItem>&,bool,bool,bool=false);
 void historical(std::vector<MenuItem>&,bool,bool);
};
'''
    code += reader[0]+reader[1].replace('::buildMenuItems','::reference')+reader[2].replace('::buildMenuItems','::historical')
    code += '''
namespace FsHelpers { bool isProtectedPathComponent(const std::string& s){return s=="protected";} }
struct MappedInputManager { enum class Button {Confirm,Back}; };
struct FilePathResult {std::string path;}; struct ActivityResult {FilePathResult data; bool isCancelled=false;};
struct FileBrowserActivity {
 enum class Mode {Books,PickFirmware,PickPng}; enum class BrowserState {Browsing,ChoosingMoveDestination};
 enum class EditAction:uint8_t {Rename,Move,Delete,Open};
 Mode mode=Mode::Books; BrowserState browserState=BrowserState::Browsing;
 std::vector<std::string> files={"book.epub"}; struct {int selected=0;} nav;
 std::string basepath="/", MOVE_HERE_ENTRY="move"; int result=-1;
 static constexpr int GO_HOME_MS=1000;
 struct {
 int time=2000; bool held=false, released=false, fired=false;
 int getHeldTime(){return time;}
 bool wasLongPressed(MappedInputManager::Button,int threshold){
  if(!held || fired || time<threshold) return false;
  fired=true;return true;
 }
 bool wasReleased(MappedInputManager::Button){return released && !fired;}
} mappedInput;
 struct {void clearTapFlash(){}} app;
 struct Popup {
  int count=0; std::vector<std::string> labels; std::function<void(int)> callback;
  void show(const char*,const char*const* options,int n,int,std::function<void(int)> f){
   ++count; labels.assign(options,options+n);callback=std::move(f);
  }
  void show(StrId,const StrId* options,int n,int,std::function<void(int)> f){
   ++count;labels.clear();for(int i=0;i<n;++i)labels.emplace_back(tr(options[i]));callback=std::move(f);
  }
 } editPopup;
 int listCount(){return files.size();} std::string cleanEntryName(const std::string& s){return s;}
 std::string selectedPath(){return files[nav.selected];}
 void requestUpdate(){} void setResult(ActivityResult){} void finish(){result=0;}
 void completeMove(){result=4;} void promptRename(){result=2;} void beginMove(){result=3;}
 void promptDelete(const std::string&,const std::string&){result=1;}
 bool handleButtons(); void showEditMenu(); void executeEditAction(EditAction); void onRowLongPress(int); void activateSelected();
};
'''
    code += '\n'.join(methods)
    code += '''
int main(){
 for(int flags=0;flags<128;++flags) for(bool isInx:{false,true}) {
  inx=isInx; Frontlight.light=flags&1;bleturner::state.compiledIn=flags&16;SETTINGS.dictionaryName[0]=(flags&32)?'d':0;switchOwnsLight=flags&64; EpubReaderMenuActivity a;
  std::vector<MenuItem> actual,expected;
  a.buildMenuItems(actual,flags&2,flags&4,flags&8);
  a.reference(expected,flags&2,flags&4,flags&8);
  assert(actual==expected);
 }
 for(bool isInx:{false,true}) {
  inx=isInx;
  for(const char* entry:{"book.epub","folder/"}) {
   FileBrowserActivity a; a.files={entry};a.onRowLongPress(0);assert(a.editPopup.count==1);
   const std::vector<std::string> expected{"STR_OPEN","STR_DELETE","STR_RENAME","STR_MOVE"};
   assert(a.editPopup.labels==expected);
   for(int index=0;index<int(expected.size());++index){
    a.editPopup.callback(index);
    assert(a.result==index);
    assert(a.editPopup.count==1); // Open must not re-enter the long-press menu.
   }
  }
  FileBrowserActivity held; held.mappedInput.held=true;
  assert(held.handleButtons()); assert(held.editPopup.count==1);
  assert(!held.handleButtons()); // Continued hold fires once.
  held.mappedInput.held=false; held.mappedInput.released=true;
  assert(!held.handleButtons()); assert(held.editPopup.count==1);
  FileBrowserActivity a;a.files={"protected"};a.showEditMenu();assert(a.editPopup.count==0);
  a.files.clear();a.showEditMenu();assert(a.editPopup.count==0);
  a.files={"firmware.bin"};a.mode=FileBrowserActivity::Mode::PickFirmware;
  a.onRowLongPress(0);assert(a.editPopup.count==0);assert(a.result==0);
 }
}
'''
    run(code,directory)
    print('Reader menu: 128 visibility combinations with clippings, BLE, dictionary and slide-switch light; library menu: file/folder actions, protected paths and long-press Open pass')


def tabs(directory, ref, inx):
    text = read('src/activities/UiTabListActivity.cpp', ref)
    body = method(text, 'UiTabListActivity::buildTabBar')
    trace = (HERE / 'InxStyleParity.cpp').read_text().split('#ifdef UPSTREAM_THEME_PARITY')[0]
    code = trace + '''
#include <cassert>
namespace fui=freeink::ui;
using UiScreen=fui::Screen<64>;
struct Metrics { bool tabPillFullSlot=false; int contentSidePadding=20, tabSpacing=8, tabBarHeight=48, verticalSpacing=10, headerUnderlineSize=1; } metrics;
struct UITheme {
 static UITheme& getInstance(){static UITheme t; return t;}
 const Metrics& getMetrics(){return metrics;}
 bool hasMainTabs(){return @INX@;}
 bool usesClassicTabs(){return false;}
};
struct UiTabListActivity {
 int selected=0, ring=0; int16_t tabPillMaxPad=0;
 static constexpr int ACTION_TAB=3, TOUCH_TAB_BAR_HEIGHT=60;
 struct {bool touch=false; bool hasTouch(){return touch;}} mappedInput;
 fui::TabIndicator tabIndicator(int){return fui::TabIndicator::None;}
 int tabCount(){return 4;} int activeTab(){return selected;} int ringPos(){return ring;}
 const char* tabLabel(int i){static const char* labels[]={"Display","Reader","Controls","System"};return labels[i];}
 @DECL@;
};
@BODY@
int main() {
 int scene=0;
 for (bool landscape:{false,true}) for (bool touch:{false,true}) for (bool full:{false,true})
 for (int16_t lineH:{20,24,32}) for (int ring:{0,1}) for (int selected=0;selected<4;++selected) {
  std::printf("SCENE %d\\n",scene++);
  TraceTarget target; target.lineH=lineH; target.scaledFonts=true;
  DeviceContext device; device.width=landscape?800:480; device.height=landscape?480:800;
  device.hasTouch=touch; device.safeArea={11,7,13,9};
  InteractionBuffer<64> hits; InputSnapshot input; Frame<64> frame(target,device,input,hits);
  auto theme=themeTokensForLineHeight(lineH); theme.bodyText.font=1; theme.smallText.font=0; theme.titleText.font=2;
  theme.listRowRadius=full?8:0; UiScreen screen(frame,theme);
  metrics.tabPillFullSlot=full;
  UiTabListActivity activity; activity.selected=selected; activity.ring=ring; activity.mappedInput.touch=touch;
  activity.buildTabBar(screen);
  assert(hits.count()==4);
  for (size_t i=0;i<hits.count();++i) {
   const auto& h=hits.data()[i]; assert(h.value==int(i));
   std::printf("hit %d %d %d %d %d %d\\n",h.rect.x,h.rect.y,h.rect.width,h.rect.height,h.action,h.value);
   for(size_t j=0;j<i;++j) assert(hits.data()[j].rect.right()<=h.rect.x);
  }
 }
}
'''
    declaration = body.split('{',1)[0].replace('UiTabListActivity::','').strip().replace('const bool boldLabels','const bool boldLabels = false')
    return run(code.replace('@INX@', str(inx).lower()).replace('@DECL@', declaration).replace('@BODY@', body), directory, sdk=True)


def touch_long_press(directory):
    production = method(read('src/MappedInputManager.cpp'), 'MappedInputManager::wasScreenLongPress')
    code = r'''#include <cassert>
#define CROSSPOINT_EMULATED 1
struct { bool held=true, suppressed=false;
 bool wasTouchLongPress(float& x,float& y) {x=.25f;y=.5f;return held&&!suppressed;}
 void suppressTouchContact(){suppressed=true;}
} gpio;
struct {void tapToLogical(float x,float y,int& tx,int& ty){tx=int(x*480);ty=int(y*800);}} renderer;
struct MappedInputManager {bool wasScreenLongPress(int&,int&) const;};
''' + production + r'''
int main(){MappedInputManager input;int x=0,y=0;
 assert(input.wasScreenLongPress(x,y));assert(x==120&&y==400);
 assert(!input.wasScreenLongPress(x,y)); // held contact cannot fire twice
 gpio.held=false;assert(!input.wasScreenLongPress(x,y));
 gpio.suppressed=false;gpio.held=true;assert(input.wasScreenLongPress(x,y));
}
'''
    run(code,directory)
    print('Simulator long-press uses HAL coordinates and consumes one contact once')


def option_highlight(directory):
    draw = method(read('src/components/themes/inx/InxTheme.cpp'), 'InxTheme::drawOptionPopup')
    code = r'''
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
#include "InxItemLayout.h"
#include "fontIds.h"
struct EpdFontFamily { enum Style { REGULAR, BOLD }; };
struct GfxRenderer {
 mutable std::vector<Rect> fills;
 mutable int lightText=0;
 int getLineHeight(int) const { return 18; }
 std::string truncatedText(int,const char* text,int,EpdFontFamily::Style=EpdFontFamily::REGULAR) const { return text; }
 void fillRect(int x,int y,int w,int h,bool black) const { if(black) fills.emplace_back(x,y,w,h); }
 void drawText(int,int,int,const char*,bool black,EpdFontFamily::Style) const { if(!black) ++lightText; }
 void drawLine(int,int,int,int,bool) const {}
 void drawRect(int,int,int,int,bool) const {}
};
struct UITheme {
 static UITheme& getInstance() { static UITheme theme; return theme; }
 Rect getScreenSafeArea(const GfxRenderer&,bool,bool) const { return Rect{0,0,480,750}; }
};
void drawDottedSeparator(const GfxRenderer&,int,int,int) {}
struct InxTheme {
 void drawOptionPopup(const GfxRenderer&,const char*,const std::vector<std::string>&,int,int=-1) const;
};
''' + draw + r'''
int main() {
 for(int count:{5,40}) for(int selected:{1,3}) {
  const int index=count==5 ? selected : selected+25;
  const int first=count==5 ? 0 : 25;
  GfxRenderer renderer;
  std::vector<std::string> options(count,"option");
  InxTheme{}.drawOptionPopup(renderer,"title",options,index,first);
  const auto layout=InxOptionGeometry::layout(Rect{0,0,480,750},count,index);
  const Rect expected=layout.optionRect(index-first);
  int selectedFills=0;
  for(const auto& rect:renderer.fills) {
   if(rect.x==expected.x && rect.y==expected.y && rect.width==expected.width && rect.height==expected.height) ++selectedFills;
  }
  assert(selectedFills==1 && renderer.lightText==1);
 }
}
'''
    run(code,directory)
    print('INX option popup highlights nonzero current values and absolute indices beyond 16')


def paged_viewport(directory):
    sync = method(read('src/activities/UiListActivity.cpp'), 'UiListActivity::syncPagedViewport')
    sync_list = method(read('src/activities/UiListActivity.cpp'), 'UiListActivity::syncListViewport')
    code = r'''
#include <algorithm>
#include <cassert>
#include "FreeInkUI.h"
namespace fui=freeink::ui;
struct CrossPointSettings { static constexpr int INX=5; int uiTheme=INX; } SETTINGS;
struct UiScreen {
 void syncListViewport(fui::ListNav& nav,fui::ListProps& props,int count,int offset) {
  nav.syncToProps(fui::Rect{0,0,160,100},20,0,count,props,offset);
 }
};
struct UiListActivity {
 fui::ListNav nav;
 int count=40;
 bool contentFocus=true;
 bool usesUpstreamStyle()const{return false;}
 bool showMainTabContentSelection()const{return contentFocus;}
 bool defersContentSelection()const{return true;}
 fui::ListNav& activeNav(){return nav;}
 int listCount()const{return count;}
 void syncPagedViewport(int);
 void syncListViewport(UiScreen&,fui::ListProps&,int selectionOffset=0);
};
''' + sync + sync_list + r'''
int main() {
 for(int capacity:{1,4,5,9,12}) {
  UiListActivity page;
  page.syncPagedViewport(capacity);
  assert(page.nav.top==0 && page.nav.inputPageRows()==capacity);
  page.nav.requestScroll(capacity);
  page.syncPagedViewport(capacity);
  assert(page.nav.selected==0 && page.nav.top==capacity);
  page.nav.requestSelection(39);
  page.syncPagedViewport(capacity);
  assert(page.nav.selected==39 && page.nav.top<=39 && page.nav.top+page.nav.drawnRows==40);
  page.count=3;
  page.syncPagedViewport(capacity);
  assert(page.nav.selected==2 && page.nav.top<=2 && page.nav.top+page.nav.drawnRows<=3);
 }
 UiListActivity page;
 UiScreen screen;
 fui::ListProps props;
 page.contentFocus=false;
 page.syncListViewport(screen,props);
 assert(props.selectedIndex==-1); // no established row or content focus
 page.nav.onListRendered(props.topIndex,4,false);
 assert(!page.nav.consumeRebuildNeeded());
 page.contentFocus=true;
 page.nav.requestSelection(39);
 page.syncListViewport(screen,props);
 page.nav.onListRendered(props.topIndex,2,false); // wrapped rows clipped the selection
 assert(page.nav.consumeRebuildNeeded());
 assert(page.nav.top>props.topIndex && page.nav.top<=39);
}
'''
    run(code,directory,True)
    print('Shared list/grid viewport: swipe preserves selection; buttons follow; shrinking data clamps safely')


def recent_actions(directory):
    source = read('src/activities/home/RecentBooksActivity.cpp')
    handlers = '\n'.join(method(source, 'RecentBooksActivity::'+name)
                         for name in ('handleButtons', 'onRowLongPress', 'promptRemoveBook'))
    code = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
#include "FreeInkUI.h"
#define LOG_DBG(...) ((void)0)
#define tr(id) #id
constexpr unsigned long LONG_PRESS_MS=1000;
struct RecentBook {std::string path,title;};
struct Store {
 std::vector<RecentBook> books{{"first.epub","First"},{"last.epub","Last"}};
 int removals=0;
 bool removeByPath(const std::string& path) {
  auto row=std::find_if(books.begin(),books.end(),[&](const auto& book){return book.path==path;});
  if(row==books.end()) return false;
  books.erase(row);++removals;return true;
 }
} RECENT_BOOKS;
struct RenderLock {template<class T> explicit RenderLock(T&) {}};
struct ConfirmationActivity {};
struct ActivityResult {bool isCancelled=false;};
struct MappedInputManager {
 enum class Button {Confirm,Back};
 bool confirm=false,back=false;unsigned long held=0;
 bool wasReleased(Button b){return b==Button::Confirm ? confirm : back;}
 unsigned long getHeldTime(){return held;}
};
struct RecentBooksActivity {
 const std::vector<RecentBook>& recentBooks=RECENT_BOOKS.books;
 freeink::ui::ListNav nav;
 MappedInputManager mappedInput;
 std::function<void(const ActivityResult&)> resultHandler;
 struct {void clearTapFlash(){}} app;
 int opened=-1,home=0,reloads=0,updates=0;
 bool closed=false;
 int listCount()const{return int(recentBooks.size());}
 void activateIndex(int i){opened=i;}
 void onGoHome(){++home;}
 void loadRecentBooks(){++reloads;}
 void closeRouting(){closed=true;}
 void requestUpdate(bool immediate){assert(immediate);++updates;}
 template<class T,class F> void startActivityForResultWith(F handler,const char*,const std::string&) {resultHandler=handler;}
 bool handleButtons();
 void onRowLongPress(int);
 void promptRemoveBook(const std::string&,const std::string&);
};
''' + handlers + r'''
int main() {
 RecentBooksActivity page;page.nav.selected=1;
 page.mappedInput.confirm=true;
 assert(page.handleButtons() && page.opened==1 && !page.resultHandler);
 page.mappedInput.held=LONG_PRESS_MS;
 assert(page.handleButtons() && page.resultHandler);
 page.resultHandler(ActivityResult{true});
 assert(RECENT_BOOKS.removals==0 && page.reloads==0 && !page.closed);
 page.resultHandler(ActivityResult{});
 assert(RECENT_BOOKS.removals==1 && RECENT_BOOKS.books.front().path=="first.epub");
 assert(page.closed && page.reloads==1 && page.updates==1 && page.nav.selected==0);
 page.resultHandler={};
 page.onRowLongPress(-1);page.onRowLongPress(1);assert(!page.resultHandler);
 page.onRowLongPress(0);assert(page.resultHandler);
 page.resultHandler(ActivityResult{true});assert(RECENT_BOOKS.removals==1);
 page.mappedInput={};page.mappedInput.back=true;
 assert(page.handleButtons() && page.home==1);
}
'''
    run(code,directory,True)
    print('Shared recent-book actions: short open, long-press removal, cancellation, index recovery and Back pass')


def recent_swipes(directory):
    recent = read('src/activities/home/InxRecentActivity.cpp')
    base = read('src/activities/UiListActivity.cpp')
    code = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <string>
#include "FreeInkUI.h"
#include "InxRecentLayout.h"
namespace fui=freeink::ui;
template<class... Args>void logDebug(Args&&...){}
#define LOG_DBG(...) logDebug(__VA_ARGS__)
struct MappedInputManager {enum class SwipeDir {None,Up,Down,Left,Right};};
struct {int focus=0;void focusMainTabContent(){++focus;}} activityManager;
struct UiListActivity {
 fui::ListNav nav;
 std::string name="InxRecent";
 int count=20,updates=0;
 bool selection=false;
 fui::ListNav& activeNav(){return nav;}
 int listCount()const{return count;}
 bool defersContentSelection()const{return true;}
 bool hasContentSelection()const{return selection && count>0;}
 void requestUpdate(){++updates;}
 bool handleListSwipe(MappedInputManager::SwipeDir,int,int);
 void syncPagedViewport(int);
};
struct InxRecentActivity : UiListActivity {
 InxRecentLayout chosen=InxRecentLayout::Flow;
 Rect content{5,90,470,650};
 std::atomic<int> visibleStart{0},visibleCount{0};
 InxRecentLayout layout()const{return chosen;}
 Rect contentRect()const{return content;}
 bool handleListSwipe(MappedInputManager::SwipeDir,int,int);
 bool hasContentSelection()const;
 int initialConfirmIndex()const;
 void paint(){
  const int capacity=InxRecentGeometry::itemsPerPage(chosen);
  syncPagedViewport(capacity);visibleStart=nav.top;visibleCount=std::min(capacity,count-nav.top);
 }
};
''' + method(base, 'UiListActivity::syncPagedViewport') + method(base, 'UiListActivity::handleListSwipe') + '\n'.join(
        method(recent, 'InxRecentActivity::'+name) for name in ('hasContentSelection','initialConfirmIndex','handleListSwipe')) + r'''
int main(){
 using Swipe=MappedInputManager::SwipeDir;
 for(int count:{0,1,3,20}) {
  InxRecentActivity page;page.count=count;page.paint();
  const Rect band=InxRecentGeometry::flowCarouselRect(page.content);
  const int x=band.x+band.width/2,y=band.y+band.height/2;
  assert(!page.handleListSwipe(Swipe::Left,x,band.y-1));
  assert(!page.handleListSwipe(Swipe::Left,x,band.y+band.height));
  assert(!page.handleListSwipe(Swipe::Right,band.x-1,y));
  assert(!page.handleListSwipe(Swipe::Right,band.x+band.width,y));
  assert(page.updates==0 && !page.hasContentSelection());
  for(int i=0;i<count+3;++i){
   int before=page.updates;assert(page.handleListSwipe(Swipe::Left,x,y));page.paint();
   assert(page.nav.top==std::min(i+1,std::max(0,count-1)));
   assert(!page.hasContentSelection() && page.nav.selected==0);
   assert(page.updates==before+(count>1));
  }
  for(int i=0;i<count+3;++i){page.handleListSwipe(Swipe::Right,x,y);page.paint();}
  assert(page.nav.top==0);
  if(count>2){
   page.selection=true;assert(page.hasContentSelection());
   page.handleListSwipe(Swipe::Left,x,y);page.paint();
   assert(!page.hasContentSelection() && page.initialConfirmIndex()==1);
   page.nav.requestSelection(page.initialConfirmIndex());page.paint();assert(page.hasContentSelection());
   page.handleListSwipe(Swipe::Left,x,y);page.handleListSwipe(Swipe::Right,x,y);page.paint();
   assert(page.nav.top==1 && page.hasContentSelection());
   page.selection=false;
  }
 }
 for(auto layout:{InxRecentLayout::Flow,InxRecentLayout::Grid,InxRecentLayout::List,InxRecentLayout::Icons,InxRecentLayout::Cover}){
  InxRecentActivity page;page.chosen=layout;page.paint();
  const int capacity=InxRecentGeometry::itemsPerPage(layout);
  if(layout!=InxRecentLayout::Flow)assert(!page.handleListSwipe(Swipe::Left,100,100));
  assert(page.handleListSwipe(Swipe::Up,100,100));page.paint();
  assert(page.nav.top==capacity && page.initialConfirmIndex()==capacity && !page.hasContentSelection());
  page.nav.requestSelection(page.initialConfirmIndex());page.selection=true;page.paint();
  assert(page.hasContentSelection());
  page.handleListSwipe(Swipe::Down,100,100);page.paint();assert(page.nav.top==0 && !page.hasContentSelection());
 }
}
'''
    run(code,directory,True)
    print('Recent layouts: cover-band horizontal swipes clamp at ends; vertical paging and visible-entry confirmation pass')


def stats_actions(directory):
    source = read('src/activities/apps/reading-stats/ReadingStatsActivity.cpp')
    trace = read('test/inx_navigation/InxStyleParity.cpp').split('#ifdef UPSTREAM_THEME_PARITY')[0]
    code = trace + r'''
#include <array>
#include <atomic>
#include <cassert>
#include <vector>
#include "components/UiContentSelection.h"
template<class... Args>void logDebug(Args&&...){}
#define LOG_DBG(...) logDebug(__VA_ARGS__)
struct ButtonNavigator {
 static int nextIndex(int,int);
 static int previousIndex(int,int);
 static int nextPageIndex(int,int,int);
 static int previousPageIndex(int,int,int);
};
namespace fui=freeink::ui;
constexpr int BOOKS_PER_PAGE=3,BOOK_LONG_PRESS_MS=1000;
struct HalDisplay {enum {HALF_REFRESH};};
struct Activity {void onEnter(){}};
struct {std::vector<int> books{1,2,3};const auto& getBooks()const{return books;}} READING_STATS;
struct {int focus=0;void focusMainTabContent(){++focus;}} activityManager;
struct MappedInputManager {
 enum class Button {Back,Confirm,NavNext,NavPrevious,Count};
 enum class SwipeDir {None,Left,Right,Up,Down};
 fui::InputSnapshot snap{};
 Button held=Button::Count,pressed=Button::Count,release=Button::Count;
 SwipeDir swipe=SwipeDir::None;
 bool isPressed(Button b)const{return held==b;}
 bool wasPressed(Button b)const{return pressed==b;}
 bool wasReleased(Button b)const{return release==b;}
 int getHeldTime()const{return 0;}
 SwipeDir wasSwipe()const{return swipe;}
};
struct ReadingStatsActivity : Activity {
 using UiApp=fui::FreeInkApp<24,6>;
 using UiScreen=UiApp::ScreenType;
 static constexpr fui::ActionId ACTION_ENTRY=1;
 struct TouchTarget {fui::Rect bounds;int entry;};
 std::array<TouchTarget,4> touchTargets{};
 int touchTargetCount=0,opened=-1,removed=-1,updates=0,finishes=0,coverChecks=0;
 std::atomic<int> selectedIndex{0},viewIndex{0},renderedCoverView{-1};
 UiContentSelection contentSelection;
 bool inx=false,touch=true,mainTabEnabled=true,waitForConfirmRelease=false,waitForBackRelease=false,waitingForCoverRender=false,renderedCoverMissing=false;
 int attemptedCoverView=-1;
 struct {void requestNextRefresh(int){}} renderer;
 MappedInputManager mappedInput;
 struct Navigator {
  template<class F>void onNextRelease(F){}
  template<class F>void onPreviousRelease(F){}
  template<class F>void onNextContinuous(F){}
  template<class F>void onPreviousContinuous(F){}
 } buttonNavigator;
 TraceTarget target;
 fui::DeviceContext device;
 UiApp app{target,device};
 struct Route {fui::ActionEvent event;bool routed=true;explicit operator bool()const{return bool(event);}};
 Route routeTouch(const MappedInputManager& input,bool){
  if(!input.snap.touchPressed && !input.snap.touchReleased)return {{},false};
  return {app.route(input.snap)};
 }
 void resetUi(){}
 bool usesInxLayout()const{return inx;}
 bool defersContentSelection()const{return inx && touch && mainTabEnabled;}
 void requestUpdate(){++updates;}
 void finish(){++finishes;}
 void openSelectedEntry(){opened=selectedIndex;}
 void confirmRemoveSelectedBook(){removed=selectedIndex;}
 bool showMainTabContentSelection()const{return true;}
 void prepareVisibleCover(){++coverChecks;}
 int displayedIndex()const;
 bool hasContentSelection()const;
 void establishContentSelection(int);
 void moveSelectionTo(int);
 bool consumeSelectionEntryInput();
 enum class InputFlow { Stop, TouchOnly, All };
 InputFlow handleButtons();
 void onEnter();
 void loop();
};
''' + '\n'.join(line for line in read('src/components/UiContentSelection.cpp').splitlines() if not line.startswith('#include')) + '\n'.join(method(read('src/util/ButtonNavigator.cpp'), 'ButtonNavigator::'+name) for name in ('nextIndex','previousIndex','nextPageIndex','previousPageIndex')) + '\n'.join(method(source, 'ReadingStatsActivity::'+name) for name in ('displayedIndex','hasContentSelection','establishContentSelection','moveSelectionTo','consumeSelectionEntryInput','onEnter','handleButtons','loop')) + r'''
int main() {
 using Button=MappedInputManager::Button;
 using Swipe=MappedInputManager::SwipeDir;
 for(bool inx:{false,true}) {
  ReadingStatsActivity page;page.inx=inx;page.device.width=480;page.device.height=800;page.device.hasTouch=true;page.app.setDevice(page.device);page.onEnter();
  assert(page.selectedIndex==1);
  page.touchTargetCount=2;
  page.touchTargets[0]={fui::Rect{10,10,100,100},1};
  page.touchTargets[1]={fui::Rect{10,120,100,100},0};
  page.app.render();
  page.mappedInput.snap.touchReleased=true;
  page.mappedInput.snap.touchX=300;page.mappedInput.snap.touchY=10;
  page.loop();assert(page.opened==-1 && page.removed==-1);
  page.mappedInput.snap.touchX=30;page.mappedInput.snap.touchY=30;
  page.loop();assert(page.opened==1 && page.removed==-1);
  page.mappedInput.snap.longPress=true;
  page.loop();assert(page.removed==1);
  page.mappedInput.snap.longPress=false;page.mappedInput.snap.touchY=140;
  page.loop();assert(page.opened==0);
  page.opened=-1;page.mappedInput={};page.mappedInput.held=Button::Confirm;page.onEnter();page.app.render();
  page.loop();assert(page.opened==-1);
  page.mappedInput.held=Button::Count;page.mappedInput.release=Button::Confirm;
  page.loop();assert(page.opened==-1);
  page.mappedInput.release=Button::Count;page.mappedInput.snap={.touchReleased=true,.touchX=30,.touchY=140};
  page.loop();assert(page.opened==0);
  READING_STATS.books.clear();page.onEnter();assert(page.selectedIndex==0);
  READING_STATS.books={1,2,3};
 }
 for(bool inx:{false,true}) {
  ReadingStatsActivity page;page.inx=inx;page.onEnter();
  page.waitForConfirmRelease=true;page.mappedInput.held=Button::Confirm;page.mappedInput.release=Button::Back;
  page.loop();assert(page.finishes==1 && page.opened==-1); // Back precedes the Confirm barrier
  page.mappedInput={};page.onEnter();page.waitForBackRelease=true;page.waitForConfirmRelease=true;
  page.touchTargetCount=1;page.touchTargets[0]={fui::Rect{10,10,100,100},1};page.app.render();
  page.mappedInput.held=Button::Back;page.mappedInput.snap={.touchReleased=true,.touchX=30,.touchY=30};
  page.loop();assert(page.opened==(inx?1:-1)); // deferred entry keeps independent touch available
  page.mappedInput={};page.opened=-1;page.loop();assert(page.opened==-1);
 }
 for(bool touch:{false,true})for(bool mainTab:{false,true}) {
  ReadingStatsActivity page;page.inx=true;page.touch=touch;page.mainTabEnabled=mainTab;page.onEnter();
  assert(page.defersContentSelection()==(touch&&mainTab));
 }
 for(Button first:{Button::Confirm,Button::NavNext,Button::NavPrevious}) {
  ReadingStatsActivity page;page.inx=true;page.onEnter();page.renderedCoverView=1;
  assert(page.displayedIndex()==1 && !page.hasContentSelection());
  page.loop();assert(page.coverChecks==1); // visible covers load even without an item selection
  page.mappedInput.swipe=Swipe::Left;page.loop();page.mappedInput.swipe=Swipe::None;
  assert(page.displayedIndex()==2 && page.selectedIndex==1 && !page.hasContentSelection());
  page.renderedCoverView=2;int before=page.updates;
  page.mappedInput.pressed=page.mappedInput.held=first;page.loop();
  const int expected=first==Button::Confirm?2:first==Button::NavNext?0:3;
  assert(page.selectedIndex==expected && page.displayedIndex()==expected && page.opened==-1);
  assert(page.updates==before+1);
  page.renderedCoverView=expected;assert(page.hasContentSelection());
  page.mappedInput.pressed=Button::Count;
  for(int i=0;i<30;++i)page.loop();
  assert(page.updates==before+1 && page.opened==-1 && page.removed==-1);
  page.mappedInput.held=Button::Count;page.mappedInput.release=first;page.loop();
  assert(page.opened==-1 && page.removed==-1);
  page.mappedInput.release=Button::Confirm;page.loop();assert(page.opened==expected);
  page.opened=-1;page.mappedInput={};page.mappedInput.swipe=Swipe::Right;page.loop();
  page.renderedCoverView=page.displayedIndex();page.mappedInput={};
  assert(!page.hasContentSelection());
  page.mappedInput.release=Button::Confirm;page.loop();assert(page.opened==-1);
  assert(page.selectedIndex==page.displayedIndex() && page.hasContentSelection());
  page.loop();assert(page.opened==page.displayedIndex());
 }
 READING_STATS.books.clear();
 ReadingStatsActivity empty;empty.inx=true;empty.onEnter();empty.renderedCoverView=0;
 empty.mappedInput.release=Button::Confirm;empty.loop();assert(empty.hasContentSelection() && empty.opened==-1);
 empty.loop();assert(empty.opened==0);
 READING_STATS.books={1,2,3};
}
'''
    run(code,directory,True)
    print('Statistics in both skins: published hits open the right entry, blank taps do nothing, held-entry releases are consumed')


def stats_highlights(directory):
    source = read('src/activities/apps/reading-stats/ReadingStatsActivity.cpp')
    code = r'''
#include <array>
#include <cassert>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>
#include "InxItemLayout.h"
#include "components/themes/inx/InxTheme.h"
#define tr(key) #key
namespace EpdFontFamily {enum Style {BOLD,ITALIC};}
constexpr int INX_TEXT_GAP=4,INX_CELL_VERTICAL_PADDING=12;
struct GfxRenderer {
 mutable std::vector<Rect> covers,grids;
 int displays=0;
 int getScreenWidth()const{return 480;}
 int getLineHeight(int)const{return 24;}
 int getTextWidth(int,const char*,EpdFontFamily::Style)const{return 40;}
 void clearScreen(){covers.clear();grids.clear();}
 void displayBuffer(){++displays;}
};
struct UITheme {
 ThemeMetrics metrics=InxMetrics::values;
 std::vector<Rect> selections;
 static UITheme& getInstance(){static UITheme theme;return theme;}
 const ThemeMetrics& getMetrics()const{return metrics;}
 void drawSelectionFrame(const GfxRenderer&,Rect r){selections.push_back(r);}
 void drawButtonHints(const GfxRenderer&,const char*,const char*,const char*,const char*){}
};
#define GUI UITheme::getInstance()
struct ReadingBookStats {
 std::string title="book",author="author";
 unsigned sessions=3,totalReadingMs=1200,lastSessionMs=300;
 unsigned char lastProgressPercent=20,chapterProgressPercent=10;
 std::vector<int> readingDays{1,2};
};
struct {
 std::vector<ReadingBookStats> books{ReadingBookStats{},ReadingBookStats{}};
 const auto& getBooks()const{return books;}
 unsigned getTotalReadingMs()const{return 2400;}
 unsigned getCurrentStreakDays()const{return 2;}
 unsigned getBooksStartedCount()const{return books.size();}
 unsigned getBooksFinishedCount()const{return 0;}
} READING_STATS;
const char* titleOf(const ReadingBookStats& b){return b.title.c_str();}
void formatDuration(unsigned long,char* out,size_t size){snprintf(out,size,"20m");}
template<class... Args>void drawClippedText(Args&&...){}
template<class... Args>void drawCenteredClippedText(Args&&...){}
template<class... Args>void drawMiniProgressBar(Args&&...){}
template<class... Args>void drawDonut(Args&&...){}
bool drawCover(const GfxRenderer& r,const ReadingBookStats&,Rect cover){r.covers.push_back(cover);return true;}
void drawStatsGrid(const GfxRenderer& r,Rect grid,int,const char*const*,const char*const*){r.grids.push_back(grid);}
struct ReadingStatsActivity {
 struct TouchTarget {Rect bounds;int entry=0;};
 std::array<TouchTarget,4> touchTargets;
 int touchTargetCount=0;
 bool renderedCoverMissing=false,contentSelectionVisibleForFrame=false;
 GfxRenderer renderer;
 bool usesMainTabBar()const{return true;}
 Rect pageContentRect()const{return Rect{0,90,480,670};}
 void drawPageHeader(Rect,const char*){}
 struct Labels {const char *btn1="",*btn2="",*btn3="",*btn4="";};
 Labels mainTabButtonLabels(const char*,const char*,bool){return {};}
 void renderUi(){}
 void renderInx(int);
};
''' + method(source, 'statsCellHeight') + method(source, 'ReadingStatsActivity::renderInx') + r'''
bool same(Rect a,Rect b){return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height;}
int main(){
 auto& theme=UITheme::getInstance();
 for(int view:{0,1,2}){
  ReadingStatsActivity page;page.renderInx(view);
  assert(theme.selections.empty() && page.renderer.covers.size()==1 && page.renderer.displays==1);
  const auto cover=page.renderer.covers.front(),grid=page.renderer.grids.front();
  page.touchTargetCount=0;page.contentSelectionVisibleForFrame=true;page.renderInx(view);
  assert(page.renderer.covers.size()==1 && same(cover,page.renderer.covers.front()));
  assert(theme.selections.size()==1 && same(theme.selections.front(),view?cover:grid));
  for(int i=0;i<page.touchTargetCount;++i)assert(page.touchTargets[i].entry==(view?view:(i?0:1)));
  theme.selections.clear();
 }
 READING_STATS.books.clear();ReadingStatsActivity empty;empty.renderInx(0);
 assert(empty.renderer.covers.empty() && empty.touchTargetCount==3 && theme.selections.empty());
}
'''
    run(code,directory)
    print('Statistics drawing: selection emphasizes only the operated cover/overview grid; covers, borders and hit geometry remain available without selection')


def stats_returns(directory):
    source = read('src/activities/apps/reading-stats/ReadingStatsActivity.cpp')
    code = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
#include "InxItemLayout.h"
#include "components/UiContentSelection.h"
#define tr(key) #key
struct ReadingBookStats {std::string path;};
const char* getBookTitle(const ReadingBookStats& book){return book.path.c_str();}
struct ActivityResult {bool isCancelled=false;};
struct ConfirmationActivity {};
struct {
 std::vector<ReadingBookStats> books{{"a"},{"b"},{"c"}};
 const auto& getBooks()const{return books;}
 bool removeBook(const std::string& path){
  auto found=std::find_if(books.begin(),books.end(),[&](const auto& book){return book.path==path;});
  if(found==books.end())return false;
  books.erase(found);return true;
 }
} READING_STATS;
struct ReadingStatsActivity {
 std::atomic<int> selectedIndex{3},viewIndex{3},renderedCoverView{3};
 UiContentSelection contentSelection;
 bool deferred=true,closed=false,waitForBackRelease=false,waitingForCoverRender=false,renderedCoverMissing=true;
 int attemptedCoverView=-1,updates=0;
 std::function<void(const ActivityResult&)> result;
 bool defersContentSelection()const{return deferred;}
 void closeRouting(){closed=true;}
 int displayedIndex()const;
 void requestUpdate(bool=false){++updates;}
 void guardBackReturn();
 void confirmRemoveSelectedBook();
 void prepareVisibleCover();
 template<class T,class F>void startActivityForResultWith(F callback,const char*,const char*){result=callback;}
};
''' + '\n'.join(method(source, 'ReadingStatsActivity::'+name) for name in ('displayedIndex','guardBackReturn','confirmRemoveSelectedBook','prepareVisibleCover')) + r'''
int main(){
 ReadingStatsActivity page;page.contentSelection.establish();page.confirmRemoveSelectedBook();
 assert(page.result);page.result({true});
 assert(READING_STATS.books.size()==3 && page.contentSelection.hasSelection() && !page.closed);
 assert(page.selectedIndex==3 && page.viewIndex==3 && page.waitForBackRelease);
 page.result({false});
 assert(READING_STATS.books.size()==2 && !page.contentSelection.hasSelection() && page.closed);
 assert(page.selectedIndex==2 && page.viewIndex==2 && page.updates==2);
 page.waitingForCoverRender=false;page.renderedCoverView=1;page.viewIndex=1;
 page.prepareVisibleCover();assert(page.waitingForCoverRender && page.attemptedCoverView==1);
 int before=page.updates;page.prepareVisibleCover();assert(page.updates==before);
 page.waitingForCoverRender=false;page.viewIndex=0;page.renderedCoverView=0;
 page.prepareVisibleCover();assert(page.waitingForCoverRender && page.attemptedCoverView==0);
 page.waitingForCoverRender=false;page.renderedCoverView=1;page.viewIndex=2;
 before=page.updates;page.prepareVisibleCover();assert(page.updates==before);
 READING_STATS.books.clear();page.viewIndex=0;page.renderedCoverView=0;page.attemptedCoverView=-1;
 page.prepareVisibleCover();assert(page.updates==before);
}
'''
    run(code,directory)
    print('Statistics returns: cancellation retains the operated entry; removal clears selection; cover loading follows the visible page once')


def main():
    with tempfile.TemporaryDirectory(prefix='theme-menus-', dir=os.environ.get('TMPDIR')) as temp:
        temp=Path(temp)
        menus(temp)
        plugin_toggle(temp)
        short_power_toggle(temp)
        actions(temp)
        option_highlight(temp)
        paged_viewport(temp)
        recent_actions(temp)
        recent_swipes(temp)
        stats_actions(temp)
        stats_highlights(temp)
        stats_returns(temp)
        touch_long_press(temp)
        compare(tabs(temp,None,False), tabs(temp,REF,False), 'Non-INX shared tab component vs fixed upstream')
        compare(tabs(temp,None,True), tabs(temp,'9d02f498',True), 'INX tabs vs unchanged pre-sync source')


if __name__ == '__main__':
    main()
