#!/usr/bin/env python3
"""Sync review check: production menus/tabs with fixed host metrics, not full pages.

Requires the reviewed Reader 93e98bb and pre-sync 9d02f498 Git objects fetched by
the sync workflow; intentionally not registered in offline/shallow-clone CTest.
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
REF = '93e98bb78702e29868a16a13b80c40e6b36ccdff'
SDK = Path(os.environ.get('FREEINK_SDK_ROOT', ROOT / 'freeink-sdk'))


def read(path, ref=None):
    return source(ref, path) if ref else (ROOT / path).read_text()


def method(text, name):
    return re.search(r'^[^\n]*' + name + r'\(.*?\n}', text, re.M | re.S).group()


def run(code, directory, sdk=False):
    cpp, binary = directory / 'check.cpp', directory / 'check'
    cpp.write_text(code)
    command = ['c++', '-std=c++20', '-Wall', '-Wextra', '-Wno-unused-parameter', str(cpp), '-o', str(binary)]
    if sdk:
        ui = SDK / 'libs/ui/FreeInkUI'
        command += ['-I' + str(ui / 'include'), str(ui / 'src/FreeInkUI.cpp')]
    subprocess.run(command, check=True)
    return subprocess.check_output([str(binary)], text=True)


def settings_metadata(text):
    # Read labels, categories, field bindings and subpage flags from the shared
    # declaration. Hardware/font services are stubbed; no parallel menu model.
    start = text.index('std::vector<SettingInfo> v = {')
    entries = text[start:].split('\n    };', 1)[0]
    result = []
    for entry in re.split(r'SettingInfo::\w+\(', entries)[1:]:
        ids = re.findall(r'StrId::(STR_\w+)', entry)
        fields = re.findall(r'&CrossPointSettings::(\w+)', entry)
        category = next((i for i in ids if i.startswith('STR_CAT_')), 'STR_NONE_OPT')
        result.append((ids[0], category, fields[0] if fields else None,
                       '.withTextSettings()' in entry, '.withReadingStatsSettings()' in entry))
    result.append(('STR_DICTIONARY', 'STR_CAT_READER', None, False, False))
    return result


def menus(directory):
    path = 'src/activities/settings/SettingsActivity.cpp'
    methods = [method(read(path, ref), 'SettingsActivity::rebuildSettingsLists') for ref in (None, REF)]
    data = [settings_metadata(read('src/SettingsList.h', ref)) for ref in (None, REF)]
    ids = sorted(set(re.findall(r'StrId::(\w+)', '\n'.join(methods))) |
                 {x for rows in data for row in rows for x in row[:2]})
    actions = sorted(set(re.findall(r'SettingAction::(\w+)', '\n'.join(methods))) | {'None'})
    fields = sorted(set(re.findall(r'&CrossPointSettings::(\w+)', '\n'.join(methods))) |
                    {row[2] for rows in data for row in rows if row[2]})
    code = '''#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstdint>
#define FREEINK_CAP_BLE_HID_HOST 1
#define FREEINK_DEVICE_MURPHY_M4 0
#define FREEINK_DEVICE_X4CLASSIC classic
bool inx=false, touch=false, pro=false, classic=false, dictionary=false;
'''
    for enum, values in [('StrId', ids), ('SettingAction', actions)]:
        code += f'enum class {enum} {{' + ','.join(values) + '};\n'
        code += f'const char* name({enum} v) {{ switch(v) {{' + ''.join(
            f'case {enum}::{v}: return "{v}";' for v in values) + '} return "?"; }\n'
    code += 'struct CrossPointSettings { enum SHORT_PWRBTN { FOOTNOTES=1 };\n'
    code += ''.join(f'int {f}=7;\n' for f in fields) + 'bool operator==(const CrossPointSettings&) const = default; } SETTINGS;\n'
    code += '''namespace BoardConfig {
 bool hasTouch(){return touch;} bool isX4Pro(){return pro;}
 bool isX4Classic(){return classic;} bool hasHomeKey(){return false;}
}
namespace home_button { bool isSetting(int CrossPointSettings::*) {return false;} }
struct { bool isAvailable(){return false;} } halClock;
struct SettingInfo {
 StrId nameId, category=StrId::STR_NONE_OPT;
 int CrossPointSettings::*valuePtr=nullptr;
 bool inTextSettings=false, inReadingStatsSettings=false;
 SettingAction action=SettingAction::None;
 static SettingInfo Action(StrId id, SettingAction action) {
   SettingInfo s{id}; s.action=action; return s;
 }
};
struct DictionaryEntry {};
namespace DictionaryRegistry { void discover(std::vector<DictionaryEntry>& entries) {if(dictionary) entries.emplace_back();} }
struct Fonts { void refreshIfDirty() {} int registry() {return 0;} };
bool upstream=false;
std::vector<SettingInfo> getSettingsList(int*, std::vector<DictionaryEntry>*) {
 std::vector<SettingInfo> result;
'''
    for index, rows in enumerate(data):
        code += ('if (!upstream)' if index == 0 else 'else') + ' result = {\n'
        code += ',\n'.join('{StrId::%s, StrId::%s, %s, %s, %s}' % (
            a,b,'&CrossPointSettings::'+c if c else 'nullptr',str(d).lower(),str(e).lower()) for a,b,c,d,e in rows)
        code += '};\n'
    code += '''if (!dictionary) result.pop_back();
 return result;
}
struct SettingsActivity {
 std::vector<SettingInfo> displaySettings, readerSettings, controlsSettings, systemSettings;
 std::vector<SettingInfo>*currentSettings=nullptr;
 int selectedCategoryIndex=0, settingsCount=0; bool dictionariesLoaded=true;
 struct { int value=0; void refreshIfDirty(){} int& registry(){return value;} } sdFontSystem;
 bool usesAccordion() {return inx;}
 void rebuildRowItems(){}
 void rebuildSettingsLists();
 void reference();
};
'''
    code += methods[0] + '\n' + methods[1].replace('::rebuildSettingsLists', '::reference')
    code += '''
int main() {
 for (int flags=0; flags<32; ++flags) {
  touch=flags&1; pro=flags&2; classic=flags&4; dictionary=flags&16;
  SETTINGS.shortPwrBtn=(flags&8)?CrossPointSettings::FOOTNOTES:0;
  for (int theme=0; theme<8; ++theme) {
   inx=theme==5; upstream=theme==7; SettingsActivity s;
   const auto before=SETTINGS;
   if (upstream) s.reference(); else s.rebuildSettingsLists();
   assert(SETTINGS==before);
   int category=0;
   for (auto* rows : {&s.displaySettings,&s.readerSettings,&s.controlsSettings,&s.systemSettings}) {
    std::printf("MENU %d %d %d\\n",flags,theme,category++);
    for (const auto& row:*rows) std::printf("%s %s\\n",name(row.nameId),name(row.action));
   }
  }
 }
}
'''
    output = run(code, directory)
    parsed = {}
    for block in re.split(r'^MENU ', output, flags=re.M)[1:]:
        lines = block.splitlines()
        parsed[tuple(map(int, lines[0].split()))] = lines[1:]
    # Only entries present in both snapshots can be compared before the gated
    # Reader integration (new Home Button/RTC pages are tracked separately).
    for flags in range(32):
        for category in range(4):
            baseline = parsed[flags, 0, category]
            reference = parsed[flags, 7, category]
            common = set(baseline) & set(reference)
            assert [x for x in baseline if x in common] == [x for x in reference if x in common], (flags, category, [x for x in baseline if x in common], [x for x in reference if x in common])
            for theme in (1,2,3,4,6):
                assert parsed[flags,theme,category] == baseline, (flags,theme,category)
            inx_rows = parsed[flags,5,category]
            assert set(baseline) <= set(inx_rows), (flags,category,'shared setting hidden in INX')
            for row in baseline:
                assert not row.startswith(('STR_INX_RECENT_LAYOUT ', 'STR_INX_LIBRARY_LAYOUT ', 'STR_INX_APPS_LAYOUT '))
            # Added entries retain their category, relative order and action binding in INX too.
            extras = set(baseline) - set(reference)
            assert [x for x in baseline if x in extras] == [x for x in inx_rows if x in extras]
    print('Settings: 32 device/condition combinations × 7 themes; shared entries/actions and retained upstream order pass')


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
bool inx=false;
struct UITheme { static UITheme& getInstance(){static UITheme t;return t;} bool hasMainTabs(){return inx;} };
struct { bool light=false; bool present(){return light;} } Frontlight;
'''
    code += 'enum class StrId {'+','.join(ids)+'};\n'
    code += ''.join(f'constexpr StrId {i}=StrId::{i};\n' for i in ids)
    code += 'const char* tr(StrId id){ switch(id){'+''.join(f'case StrId::{i}: return "{i}";' for i in ids)+'} return "?";}\n'
    code += 'enum class MenuAction {'+','.join(menu_actions)+'};\n'
    code += '''struct MenuItem {MenuAction action; StrId labelId; bool operator==(const MenuItem&) const = default;};
struct EpubReaderMenuActivity {
 static constexpr int MAX_MENU_ITEMS=32;
 void buildMenuItems(std::vector<MenuItem>&,bool,bool);
 void reference(std::vector<MenuItem>&,bool,bool);
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
 for(int flags=0;flags<8;++flags) for(bool isInx:{false,true}) {
  inx=isInx; Frontlight.light=flags&1; EpubReaderMenuActivity a;
  std::vector<MenuItem> actual,expected;
  a.buildMenuItems(actual,flags&2,flags&4);
  if(inx) a.historical(expected,flags&2,flags&4); else a.reference(expected,flags&2,flags&4);
  assert(actual==expected);
 }
 for(bool isInx:{false,true}) {
  inx=isInx;
  for(const char* entry:{"book.epub","folder/"}) {
   FileBrowserActivity a; a.files={entry};a.onRowLongPress(0);assert(a.editPopup.count==1);
   const std::vector<std::string> expected=inx?std::vector<std::string>{"STR_RENAME","STR_MOVE","STR_DELETE"}:
      std::vector<std::string>{"STR_OPEN","STR_DELETE","STR_RENAME","STR_MOVE"};
   assert(a.editPopup.labels==expected);
   for(int index=0;index<int(expected.size());++index){
    a.editPopup.callback(index);
    assert(a.result==(inx?(index==0?2:index==1?3:1):index));
    assert(a.editPopup.count==1); // Open must not re-enter the long-press menu.
   }
  }
  FileBrowserActivity held; held.mappedInput.held=true;
  assert(held.handleButtons()==!inx); assert(held.editPopup.count==(inx?0:1));
  assert(!held.handleButtons()); // Continued hold fires once.
  held.mappedInput.held=false; held.mappedInput.released=true;
  assert(held.handleButtons()==inx); assert(held.editPopup.count==1);
  FileBrowserActivity a;a.files={"protected"};a.showEditMenu();assert(a.editPopup.count==0);
  a.files.clear();a.showEditMenu();assert(a.editPopup.count==0);
  a.files={"firmware.bin"};a.mode=FileBrowserActivity::Mode::PickFirmware;
  a.onRowLongPress(0);assert(a.editPopup.count==0);assert(a.result==(inx?-1:0));
 }
}
'''
    run(code,directory)
    print('Reader menu: 8 visibility combinations; library menu: file/folder actions, protected paths and long-press Open pass')


def tabs(directory, ref, inx):
    text = read('src/activities/UiTabListActivity.cpp', ref)
    body = method(text, 'UiTabListActivity::buildTabBar')
    trace = (HERE / 'InxStyleParity.cpp').read_text().split('#ifdef UPSTREAM_THEME_PARITY')[0]
    code = trace + '''
#include <cassert>
namespace fui=freeink::ui;
using UiScreen=fui::Screen<64>;
struct Metrics { bool tabPillFullSlot=false; int contentSidePadding=20, tabSpacing=8, tabBarHeight=48, verticalSpacing=10; } metrics;
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


def main():
    with tempfile.TemporaryDirectory(prefix='theme-menus-', dir=os.environ.get('TMPDIR')) as temp:
        temp=Path(temp)
        menus(temp)
        actions(temp)
        touch_long_press(temp)
        compare(tabs(temp,None,False), tabs(temp,REF,False), 'Non-INX shared tab component vs fixed upstream')
        compare(tabs(temp,None,True), tabs(temp,'9d02f498',True), 'INX tabs vs unchanged pre-sync source')


if __name__ == '__main__':
    main()
