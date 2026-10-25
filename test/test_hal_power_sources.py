"""Compile actual power-source and USB boot branches with explicit telemetry faults."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def run():
    usb = function((ROOT / "lib/hal/HalGPIO.cpp").read_text(), "bool HalGPIO::isUsbConnected() const")
    source = (ROOT / "src/main.cpp").read_text().split("case HalGPIO::WakeupReason::AfterUSBPower:", 1)[1]
    start = source.index("#if FREEINK_DEVICE_X4PRO")
    boot = source[start:source.index("#endif", start) + len("#endif")]
    harness = r"""
#include <cassert>
#include <cstdint>
constexpr int HIGH=1;
void delay(int) {} int digitalRead(int) {return 0;}
namespace X3GPIO {bool readBQ27220CurrentMA(int16_t*) {return false;}}
namespace BoardConfig {struct Profile {int usbDetect=-1;};Profile ACTIVE;}
struct BatteryMonitor {
 static inline bool known=false,present=false,charging=false;
 bool isExternalPowerPresent(bool* out) const {*out=known;return present;}
 bool isCharging() const {return charging;}
};
bool frames=false;bool usbHostSofActive() {return frames;}
struct HalGPIO {bool deviceIsX3() const {return false;} bool isUsbConnected() const;};
struct {bool stopped=false;void prepareForDeepSleep() {stopped=true;}} Storage;
struct {void startDeepSleep(HalGPIO&) {Storage.stopped=true;}} powerManager;
""" + usb + "\nvoid bootOnUsb(HalGPIO& gpio) {do {\n" + boot + r"""
} while(false);}
int main() {
 HalGPIO gpio;
#if FREEINK_DEVICE_METALIO_EINK4
 BatteryMonitor::known=true;BatteryMonitor::present=true;assert(gpio.isUsbConnected());
 BatteryMonitor::present=false;BatteryMonitor::charging=true;assert(!gpio.isUsbConnected());
 BatteryMonitor::known=false;assert(gpio.isUsbConnected());
 BatteryMonitor::charging=false;frames=true;assert(gpio.isUsbConnected());
 frames=false;assert(!gpio.isUsbConnected());
#endif
 bootOnUsb(gpio);
#if FREEINK_DEVICE_LILYGO || FREEINK_DEVICE_METALIO_EINK4
 assert(!Storage.stopped);
#else
 assert(Storage.stopped);
#endif
}
"""
    with tempfile.TemporaryDirectory(prefix="hal-power-") as directory:
        tmp=Path(directory);(tmp/"check.cpp").write_text(harness)
        for metalio,lilygo in ((0,0),(0,1),(1,0)):
            binary=tmp/f"check-{metalio}-{lilygo}"
            subprocess.run(["c++","-std=c++17",f"-DFREEINK_DEVICE_METALIO_EINK4={metalio}",
                            f"-DFREEINK_DEVICE_LILYGO={lilygo}",str(tmp/"check.cpp"),"-o",str(binary)],check=True,
                           env=dict(os.environ,CCACHE_DISABLE="1"))
            subprocess.run([str(binary)],check=True)
    print("Metalio authoritative/unknown power and USB SOF fallback; LilyGo USB cold boot passed")


def sleep_order():
    body = function((ROOT / "lib/hal/HalPowerManager.cpp").read_text(),
                    "void HalPowerManager::startDeepSleep(HalGPIO& gpio, const bool wakeOnHeldButton) const")
    harness = r"""
#include <cassert>
#include <cstdint>
#include <string>
constexpr int HIGH=1,OUTPUT=3,XTEINK_C3_GPIO13=13;
using gpio_num_t=int;
std::string events; bool lightOff=false,held=false; int level=0,holds=0,errors=0;
#define LOG_ERR(...) (++errors)
void gpio_hold_dis(int) {held=false;}
void pinMode(int,int) {}
void digitalWrite(int pin,int value) {assert(pin==4);level=value;}
void gpio_hold_en(int pin) {assert(pin==4);held=true;++holds;events+='4';}
namespace BoardConfig {
struct Power {int8_t latch0=FREEINK_DEVICE_EEGO_A4?4:-1,latch1=-1;};
struct Profile {Power power;};Profile ACTIVE;
}
struct {void setOn(bool on) {assert(!on);lightOff=true;events+='L';}} Frontlight;
struct HalGPIO {void prepareForDeepSleep() {assert(lightOff);events+='T';}};
namespace freeink {
namespace metalio {bool powerOff() {events+='M';return false;}}
struct PowerManager {
 static void powerDownRailsForSleep() {assert(held && level==HIGH);events+='R';}
 static void deepSleepUntilPowerButton() {events+='S';}
 static void armPowerButtonWakeup() {events+='A';}
 static void deepSleep() {events+='S';}
};
}
struct HalPowerManager {void startDeepSleep(HalGPIO&, bool=false) const;};
""" + body + r"""
int main() {
 HalGPIO gpio;HalPowerManager{}.startDeepSleep(gpio);
#if FREEINK_DEVICE_EEGO_A4
 assert(events=="L4TRS" && holds==1 && held && level==HIGH && errors==0);
#else
 assert(events=="LTMS" && holds==0 && errors==1);
#endif
}
"""
    with tempfile.TemporaryDirectory(prefix="hal-sleep-") as directory:
        tmp=Path(directory);(tmp/"check.cpp").write_text(harness)
        for a4,metalio in ((1,0),(0,1)):
            binary=tmp/f"check-{a4}"
            subprocess.run(["c++","-std=c++17","-DSOC_PM_SUPPORT_EXT1_WAKEUP=1",
                            f"-DFREEINK_DEVICE_EEGO_A4={a4}",f"-DFREEINK_DEVICE_METALIO_EINK4={metalio}",
                            str(tmp/"check.cpp"),"-o",str(binary)],check=True,
                           env=dict(os.environ,CCACHE_DISABLE="1"))
            subprocess.run([str(binary)],check=True)
    print("Frontlight/touch/latch sleep ordering and Metalio failed-power-off fallback passed")


def sleep_modes():
    source = (ROOT / "src/activities/boot_sleep/SleepActivity.cpp").read_text()
    body = function(source, "void SleepActivity::onEnter()")
    settings = (ROOT / "src/CrossPointSettings.h").read_text()
    start = settings.index("  enum SLEEP_SCREEN_MODE {")
    values = settings[start:settings.index("};", start) + 2]
    harness = r"""
#include <cassert>
#include <cstdint>
constexpr int STR_ENTERING_SLEEP=0;
#define tr(x) "sleep"
struct CrossPointSettings {
""" + values + r"""
 enum QUICK_RESUME_SLEEP_SCREEN { OFF, QUICK_RESUME_AFTER_TIMEOUT };
 uint8_t sleepScreen=DARK,quickResumeSleepScreen=OFF,orientation=0;
} SETTINGS;
struct {bool lastSleepFromReader=false;} APP_STATE;
struct {bool inverted=false;bool isInverted() {return inverted;}void setInverted(bool value) {inverted=value;}} display;
struct HalDisplay {enum {FULL_REFRESH};};
struct GfxRenderer {
 enum class Orientation {Portrait};
 int washes=0,inversions=0;
 void invertScreen() {++inversions;}
 void setOrientation(Orientation) {}
 void clearScreen() {++washes;}
 void displayBuffer(int) {}
};
namespace sleepCover {bool ok=true;bool recover() {return ok;}}
namespace ReaderUtils {void applyOrientation(GfxRenderer&,uint8_t) {}}
struct {void drawPopup(GfxRenderer&,const char*) {}} GUI;
void drawSleepPopupPreservingFrame(GfxRenderer&) {}
void releaseSdFontCachesForDecode(GfxRenderer&) {}
struct Activity {void onEnter() {}};
struct SleepActivity:Activity {
 bool fromTimeout=false;
 GfxRenderer renderer;
 char chosen='?';
 void renderDefaultSleepScreen() {chosen='D';}
 void renderCustomSleepScreen() {chosen='I';}
 void renderCoverSleepScreen() {chosen='C';}
 void renderLastScreenSleepScreen() {chosen='L';}
 void renderTransparentCustomSleepScreen() {chosen='T';}
 void renderBlankSleepScreen() {chosen='B';}
 void onEnter();
};
""" + body + r"""
int main() {
 static_assert(CrossPointSettings::TRANSPARENT==7 && CrossPointSettings::TRANSPARENT_CUSTOM==7);
 static_assert(CrossPointSettings::COVER_TRANSPARENT==8);
 const char expected[]={'D','D','I','C','B','I','L','T','C'};
 for(bool reader : {false,true}) for(bool inverted : {false,true}) for(int mode=0;mode<9;++mode) {
  APP_STATE.lastSleepFromReader=reader;
  display.inverted=inverted;
  SETTINGS.sleepScreen=mode;
  SleepActivity activity;
  activity.onEnter();
  const char want=reader && mode==CrossPointSettings::COVER_CUSTOM ? 'C' : expected[mode];
  assert(activity.chosen==want);
  if(mode>=6) assert(activity.renderer.washes==0);
#if FREEINK_DEVICE_READPICO
  else assert(activity.renderer.washes==1);
#endif
 }
 SETTINGS.sleepScreen=CrossPointSettings::COVER_TRANSPARENT;
 SETTINGS.quickResumeSleepScreen=CrossPointSettings::QUICK_RESUME_AFTER_TIMEOUT;
 SleepActivity timeout;timeout.fromTimeout=true;timeout.onEnter();assert(timeout.chosen=='L');
 sleepCover::ok=false;
 SleepActivity failed;failed.onEnter();assert(failed.chosen=='D');
}
"""
    with tempfile.TemporaryDirectory(prefix="sleep-modes-") as directory:
        tmp=Path(directory);(tmp/"check.cpp").write_text("#include <initializer_list>\n"+harness)
        for pico in (0,1):
            binary=tmp/f"check-{pico}"
            subprocess.run(["c++","-std=c++17",f"-DFREEINK_DEVICE_READPICO={pico}",str(tmp/"check.cpp"),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
    print("Sleep modes 0..8, transparent alias7, cover-overlay8, reader/night and ReadPico wash routing passed")


if __name__ == "__main__":
    run()
    sleep_order()
    sleep_modes()
