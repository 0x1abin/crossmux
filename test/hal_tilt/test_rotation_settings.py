"""Probe failure must hide auto-rotate without removing its persisted descriptor."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/tests'))
from test_reading_ui_regressions import method
header=(ROOT/'src/SettingsList.h').read_text()
hidden=method(header,'inline bool isHiddenOnDevice(')
available=method(header,'inline bool isSettingAvailableOnBoard(const SettingInfo& setting) {')
ids=sorted(set(re.findall(r'StrId::(STR_\w+)',hidden+available))|{'STR_AUTO_ROTATE'})
default=re.search(r'uint8_t autoRotate\s*=\s*[^;]+;', (ROOT/'src/CrossPointSettings.h').read_text()).group()
fixture=r"""
#include <cassert>
#include <cstdint>
#define FREEINK_DEVICE_METALIO_EINK4_PLUS 1
#define FREEINK_CAP_WARMLIGHT 0
struct CrossPointSettings {
 enum {SLIDE_SWITCH_NIGHT_MODE,SLIDE_SWITCH_TOUCH_CONTROLS};
 uint8_t frontlightBrightness=0,frontlightOn=0,frontlightRestoreOnWake=0;
 AUTO_FIELD
};
enum class StrId { ENUM_IDS };
struct SettingInfo {StrId nameId;uint8_t CrossPointSettings::* valuePtr;};
struct Probe {bool available=false;bool isAvailable()const{return false;}bool supportsAutomaticRotation()const{return available;}} halTiltSensor,halClock;
struct Light {bool present()const{return true;}} Frontlight;
bool slideSwitchOwnsLight(){return false;}bool slideSwitchOwns(int){return false;}
namespace BoardConfig {bool isX4Pro(){return false;}bool hasTouch(){return false;}bool hasHomeKey(){return false;}}
""".replace('AUTO_FIELD',default).replace('ENUM_IDS',','.join(ids))
tests=r"""
int main(){
 CrossPointSettings settings;assert(settings.autoRotate==0);
 SettingInfo automatic{StrId::STR_AUTO_ROTATE,&CrossPointSettings::autoRotate};
 settings.autoRotate=1;
 for(bool detected:{false,true,false}){
  halTiltSensor.available=detected;
  assert(isHiddenOnDevice(automatic)==!detected);
  assert(isSettingAvailableOnBoard(automatic));
  assert(settings.autoRotate==1);
 }
}
"""
with tempfile.TemporaryDirectory(prefix='rotation-settings-') as temporary:
 p=Path(temporary);source=p/'check.cpp';binary=p/'check'
 source.write_text('#include <initializer_list>\n'+fixture+'\n'+hidden+'\n'+available+'\n'+tests)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('Auto-rotate default off; production UI hides on failed probe while persistence gate retains the setting')
