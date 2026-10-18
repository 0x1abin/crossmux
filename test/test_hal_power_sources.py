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
                    "void HalPowerManager::startDeepSleep(HalGPIO& gpio) const")
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
};
}
struct HalPowerManager {void startDeepSleep(HalGPIO&) const;};
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


if __name__ == "__main__":
    run()
    sleep_order()
