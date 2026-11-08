"""Execute the actual HAL USB method and main wake switch with recording boundaries."""
from pathlib import Path
import subprocess, tempfile, os, re
ROOT=Path(__file__).resolve().parents[2]
def block(text, marker):
 start=text.index(marker); brace=text.index('{',start); depth=1; end=brace+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}'); end+=1
 return text[start:end]
hal=(ROOT/'lib/hal/HalGPIO.cpp').read_text()
method=block(hal,'bool HalGPIO::isUsbConnected() const')
usb_header=re.search(r'#if[^\n]+\n#include <soc/usb_serial_jtag_reg.h>\n#endif',hal).group(0)
helper_start=hal.rfind('#if',0,hal.index('static bool usbHostSofActive()'))
helper_end=hal.index('#endif',hal.index('static bool usbHostSofActive()'))+len('#endif')
helper=hal[helper_start:helper_end]
wake=block((ROOT/'src/main.cpp').read_text(),'switch (wakeupReason)')
source=r"""#include <cassert>
#include <cstdint>
#define LOG_DBG(...) ((void)0)
#define FREEINK_DEVICE_WAVESHARE_EPAPER_397 0
#define FREEINK_DEVICE_METALIO_EINK4 0
#define FREEINK_DEVICE_PICCO 0
constexpr int HIGH=1;
bool sof=false; unsigned chargeReads=0, sleeps=0;
unsigned long now=100;
unsigned long millis(){return now;}
void delay(unsigned long ms){now+=ms;}
bool deviceIsX3(){return false;}
int digitalRead(int){assert(false);return 0;}
namespace BoardConfig {struct Profile{int usbDetect=-1;};inline Profile ACTIVE;}
struct BatteryMonitor {
 bool isExternalPowerPresent(bool* known)const{*known=false;return false;}
 bool isCharging()const{++chargeReads;return true;}
};
struct HalGPIO {
 enum class WakeupReason{PowerButton,AfterUSBPower,AfterFlash,Other};
 bool isUsbConnected()const;
};
"""+usb_header+"\n"+helper+"\n"+method+r"""
struct CrossPointSettings{enum SHORT_PWRBTN{SLEEP};};
struct Settings{int shortPwrBtn=CrossPointSettings::SLEEP;} SETTINGS;
struct StorageType{void prepareForDeepSleep(){}} Storage;
struct PowerType{void startDeepSleep(HalGPIO&){++sleeps;}} powerManager;
int main(){
 HalGPIO gpio;
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
 assert(!gpio.isUsbConnected()&&chargeReads==0);
 sof=true; assert(gpio.isUsbConnected()&&chargeReads==0);
#endif
 bool wakeHoldVerified=true, wakePowerReleasePending=false;
 auto wakeupReason=HalGPIO::WakeupReason::AfterUSBPower;
"""+wake+r"""
#if FREEINK_DEVICE_METALIO_EINK4_PLUS
 assert(sleeps==0);
#else
 assert(sleeps==1);
#endif
 (void)wakePowerReleasePending;
}
"""
with tempfile.TemporaryDirectory(prefix='plus-battery-boot-')as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(source)
 (p/'soc').mkdir()
 (p/'soc/usb_serial_jtag_reg.h').write_text('#define USB_SERIAL_JTAG_FRAM_NUM_REG 0x24\ninline uint32_t frame=0;\n#define REG_READ(reg) (sof ? ++::frame : ::frame)\n')
 for plus in (1,0):
  subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
   '-I'+str(p),'-DFREEINK_DEVICE_METALIO_EINK4_PLUS='+str(plus),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
 print('Plus battery USB observation and awake cold-boot policy passed; legacy sleep preserved')
