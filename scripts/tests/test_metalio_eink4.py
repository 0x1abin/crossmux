"""Exercise Metalio's real parsers and I2C sequence with small host-side MCU stubs."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "freeink-sdk/libs/hardware"


class MetalioTest(unittest.TestCase):
    def test_input_power_and_rotation(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            (tmp / "Arduino.h").write_text(r'''
#pragma once
#include <cstdint>
#include <vector>
#include <utility>
#include <climits>
#include <cmath>
#include <algorithm>
#define IRAM_ATTR
constexpr int INPUT_PULLUP=2, INPUT_PULLDOWN=4, ADC_11db=3, OUTPUT=1, LOW=0, HIGH=1, INPUT=0, FALLING=3;
inline int Serial=0;
inline int gpio0=HIGH;
inline int digitalRead(int pin) { return pin==0 ? gpio0 : HIGH; }
inline void analogSetAttenuation(int) {}
inline int analogRead(int) { return 4095; }
inline int analogReadMilliVolts(int) { return 3300; }
inline void (*touchIrq)(void*)=nullptr;
inline void* touchArg=nullptr;
inline void attachInterruptArg(int,void (*fn)(void*),void* arg,int) { touchIrq=fn; touchArg=arg; }
inline void detachInterrupt(int) {}
inline uint32_t clockMs=0;
inline std::vector<unsigned> waits;
inline std::vector<std::pair<int,int>> modes;
inline unsigned long millis() { return clockMs; }
inline void (*delayHook)(unsigned)=nullptr;
inline void delay(unsigned n) { clockMs+=n; waits.push_back(n); if(delayHook) delayHook(n); }
inline void pinMode(int pin,int mode) { modes.emplace_back(pin,mode); }
inline void digitalWrite(int,int) {}
''')
            (tmp / "esp_rom_sys.h").write_text("#pragma once\ninline unsigned romLogs=0; inline void esp_rom_printf(const char*, ...) { ++romLogs; }\n")
            (tmp / "Wire.h").write_text(r'''
#pragma once
#include <array>
#include <vector>
#include <cassert>
struct MockWire {
  struct Transaction { unsigned address; std::vector<uint8_t> data; };
  std::vector<Transaction> transactions;
  std::vector<uint8_t> data;
  std::array<uint8_t,2> input{0xff,0xff};
  std::array<uint8_t,5> touch{};
  uint8_t status=3, address=0;
  unsigned cursor=0;
  bool fail=false, beginOk=true;
  unsigned failWrites=0;
  bool begin(int sda,int scl,unsigned hz) { assert(sda==41 && scl==42 && hz==400000); return beginOk; }
  void setTimeOut(unsigned) {}
  void beginTransmission(uint8_t addr) { address=addr; data.clear(); }
  void write(uint8_t value) { data.push_back(value); }
  int endTransmission(bool=true) {
    transactions.push_back({address,data});
    if(failWrites) { --failWrites; return 1; }
    return fail ? 1 : 0;
  }
  uint8_t requestFrom(uint8_t,uint8_t count,uint8_t) { cursor=0; return count; }
  int available() { return 0; }
  uint8_t read() { return address==0x20 ? input.at(cursor++) : address==0x15 ? touch.at(cursor++) : status; }
};
inline MockWire Wire;
''')
            (tmp / "driver").mkdir()
            (tmp / "driver/gpio.h").write_text(r'''
#pragma once
#include <cassert>
using gpio_num_t=int;
constexpr int GPIO_NUM_44=44, GPIO_MODE_OUTPUT=1, ESP_OK=0;
inline int motorLevel=0;
inline bool failDirection=false;
inline int gpio_set_level(int pin,int level) { assert(pin==44); motorLevel=level; return ESP_OK; }
inline int gpio_set_direction(int,int) { return failDirection ? -1 : ESP_OK; }
inline bool motorHeld=false;
inline void gpio_hold_dis(int pin) { if(pin==44) motorHeld=false; }
inline void gpio_hold_en(int pin) { if(pin==44) { assert(motorLevel==0); motorHeld=true; } }
inline void gpio_sleep_sel_dis(int) {}
''')
            (tmp / "freertos").mkdir()
            (tmp / "freertos/FreeRTOS.h").write_text(r'''
#pragma once
#include <mutex>
using portMUX_TYPE=std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) (m)->lock()
#define portEXIT_CRITICAL(m) (m)->unlock()
using QueueHandle_t=void*; using TaskHandle_t=void*;
constexpr int pdTRUE=1, pdPASS=1;
#define pdMS_TO_TICKS(x) (x)
''')
            (tmp / "freertos/queue.h").write_text(r'''
#pragma once
inline void* xQueueCreate(int,int) { return nullptr; }
inline int xQueueReceive(void*,void*,int) { return 0; }
inline int xQueueSend(void*,const void*,int) { return 0; }
inline void xQueueReset(void*) {}
''')
            (tmp / "freertos/task.h").write_text(r'''
#pragma once
inline int xTaskCreate(void (*)(void*),const char*,unsigned,void*,unsigned,void**) { return 0; }
inline int xTaskCreatePinnedToCore(void (*)(void*),const char*,unsigned,void*,int,void**,int) { return 0; }
inline void vTaskDelay(unsigned) {}
inline void vTaskDelete(void*) {}
''')
            (tmp / "esp_timer.h").write_text(r'''
#pragma once
#include <cstdint>
using esp_timer_handle_t=void*;
constexpr int ESP_TIMER_TASK=0;
struct esp_timer_create_args_t {
 void (*callback)(void*)=nullptr; void* arg=nullptr;
 int dispatch_method=0; const char* name=nullptr;
};
inline int64_t timerNow=0;
inline unsigned timerCreates=0, timerStarts=0;
inline uint64_t timeout=0;
inline bool failCreate=false, failStart=false;
inline void (*timerCallback)(void*)=nullptr;
inline int64_t esp_timer_get_time() { return timerNow; }
inline int esp_timer_create(const esp_timer_create_args_t* args, void** handle) {
 ++timerCreates;
 if (failCreate) return -1;
 timerCallback=args->callback; *handle=reinterpret_cast<void*>(1); return 0;
}
inline int esp_timer_stop(void*) { return 0; }
inline int esp_timer_start_once(void*,uint64_t us) {
 ++timerStarts; timeout=us; return failStart ? -1 : 0;
}
''')
            (tmp / "ArduinoJson.h").write_text("#pragma once\nclass JsonDocument {}; class JsonVariantConst {};\n")
            (tmp / "PersistableStore.h").write_text(r'''
#pragma once
#include <mutex>
template<class T> class PersistableStore {
 protected: mutable std::mutex storeMutex;
 public: static T& getInstance() { static T value; return value; }
};
''')
            # Compile the actual renderer transform, not a second implementation of it.
            renderer = (ROOT / "lib/GfxRenderer/GfxRenderer.cpp").read_text()
            start = renderer.index("void GfxRenderer::tapToLogical(")
            transform = renderer[start:renderer.index("\n}", start) + 2]
            source = r'''
#include <cassert>
#include <Cst816sInput.h>
#include <MetalioEink4Board.h>
#include <InputManager.h>
#include <HapticFeedback.h>
#include "CrossPointSettings.h"
#include <thread>
#include <type_traits>
uint8_t CrossPointSettings::defaultLanguageIndex() { return 0; }
template<class T,class=void> struct HasHapticSetting : std::false_type {};
template<class T> struct HasHapticSetting<T,std::void_t<decltype(std::declval<T>().hapticFeedbackLevel)>>
 : std::true_type {};
struct GfxRenderer {
 enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
 Orientation orientation=Portrait;
 int panelWidth=800, panelHeight=480;
 void tapToLogical(float,float,int&,int&) const;
};
TRANSFORM
using Region=freeink::Cst816sRegion;
freeink::Cst816sFrame frame(unsigned x,unsigned y,unsigned count=1) {
 uint8_t b[]={static_cast<uint8_t>(count),static_cast<uint8_t>(x>>8),static_cast<uint8_t>(x),
              static_cast<uint8_t>(y>>8),static_cast<uint8_t>(y)};
 return freeink::decodeCst816s(b,5);
}
int main() {
 assert(frame(80,900).region==Region::Home);
 assert(frame(400,900).region==Region::Previous);
 assert(frame(240,900).region==Region::Next);
 assert(frame(79,900).region==Region::Invalid);
 assert(frame(480,0).region==Region::Invalid);
 assert(frame(0,800).region==Region::Invalid);
 assert(frame(0,0,2).region==Region::Invalid);
 assert(frame(0,0,0).region==Region::None);
 assert(freeink::decodeCst816s(nullptr,5).region==Region::Invalid);
 uint8_t shortFrame[4]={};
 assert(freeink::decodeCst816s(shortFrame,4).region==Region::Invalid);
 // Four corners through the actual firmware rotation method.
 GfxRenderer renderer;
 for (unsigned x : {0u,479u}) for (unsigned y : {0u,799u}) {
   auto p=frame(x,y); assert(p.region==Region::Screen);
   assert(p.x==y && p.y==479-x);
   const int expected[4][2]={{int(x),int(y)},{799-int(y),int(x)},
                            {479-int(x),799-int(y)},{int(y),479-int(x)}};
   for (int i=0;i<4;++i) {
     renderer.orientation=static_cast<GfxRenderer::Orientation>(i);
     int lx,ly;
     renderer.tapToLogical(float(p.x)/799,float(p.y)/479,lx,ly);
     assert(lx==expected[i][0] && ly==expected[i][1]);
   }
 }
 freeink::Cst816sContact contact;
 contact.update(Region::Home,100,700);
 contact.update(Region::None,200,700); assert(contact.homeTap && !contact.homeLong);
 contact.update(Region::None,210,700); assert(!contact.homeTap);
 contact.update(Region::Home,300,700);
 contact.update(Region::Home,1000,700); assert(contact.homeLong);
 contact.update(Region::Home,1100,700); assert(!contact.homeLong);
 contact.update(Region::None,1200,700); assert(!contact.homeTap);
 contact.update(Region::Home,1300,700);
 contact.update(Region::Invalid,1400,700);  // I2C error cancels without a click.
 contact.update(Region::None,1500,700); assert(!contact.homeTap);
 contact.update(Region::Screen,1600,700);
 contact.update(Region::Next,1700,700); assert(contact.region==Region::Invalid);
 contact.update(Region::Next,1800,700); assert(contact.region==Region::Invalid);
 contact.update(Region::None,1900,700);
 contact.update(Region::Home,UINT32_MAX-500,700);
 contact.update(Region::Home,200,700); assert(contact.homeLong);

 using namespace freeink::metalio;
 motorHeld=true;
 assert(begin()); assert(!motorHeld);
 const uint16_t values[]={BOOT_OUTPUT,static_cast<uint16_t>(~OUTPUTS),
                          BOOT_OUTPUT|SCREEN_POWER,BOOT_OUTPUT|SCREEN_POWER|TOUCH_RESET};
 assert(Wire.transactions.size()==4);
 for (unsigned i=0;i<4;++i) {
   auto& tx=Wire.transactions[i];
   assert(tx.address==0x20 && tx.data.size()==3);
   assert(tx.data[0]==(i==1 ? 6 : 2));
   assert((tx.data[1] | tx.data[2]<<8)==values[i]);
 }
 assert((BOOT_OUTPUT & USB_MUX_SEL)!=0 && (OUTPUTS & USB_MUX_SEL)!=0);
 assert((output & ((1<<4)|(1<<1)))==0); // PA and routing remain off.
 bool dat3=false;
 for (auto [pin,mode] : modes) if (pin==46) { assert(mode==INPUT_PULLUP); dat3=true; }
 assert(dat3);
 assert((waits==std::vector<unsigned>{10,120}));
 assert(!powerButtonPressed(true)); assert(!powerButtonPressed(true));
 assert(!powerButtonPressed(false)); assert(powerButtonPressed(true));
 Wire.input={0x7f,0xff}; assert(buttons()==(1<<5)); // P0.7 is Down.
 clockMs+=21; Wire.input={0xff,0xff}; assert(buttons()==0);
 clockMs+=21; Wire.input={0xff,0xfe}; assert(buttons()==(1<<4)); // P1.0 is Up.
 clockMs+=21; assert(buttons()==(1<<4)); // Held state survives successful reads.
 clockMs+=21; Wire.input={0x7f,0xfe}; assert(buttons()==((1<<4)|(1<<5)));
 clockMs+=21; Wire.fail=true; assert(buttons()==0);
 clockMs+=2001; Wire.fail=false; Wire.input={0xff,0xff}; assert(buttons()==0);
 bool connected=false; assert(externalPowerConnected(connected) && connected);
 clockMs+=1001; Wire.status=7; assert(externalPowerConnected(connected) && !connected);
 clockMs+=1001; Wire.fail=true; assert(!externalPowerConnected(connected));
 // A host-only delay exception simulates physical power removal; the real
 // noreturn driver must never finish or fall back to MCU deep sleep.
 struct PowerRemoved {};
 for (bool fail : {false,true}) {
   Wire.fail=fail; Wire.transactions.clear(); waits.clear(); romLogs=0;
   output=(output & ~(MAIN_POWER|SCREEN_POWER)) | PA_POWER;
   delayHook=+[](unsigned n) {
     assert(n==100);
     if(waits.size()==24) throw PowerRemoved{};
   };
   try { shutdown(); } catch(const PowerRemoved&) {}
   delayHook=nullptr;
   assert(waits.size()==24 && Wire.transactions.size()==24);
   assert(romLogs <= 4); // Initial message plus at most one I2C error per second.
   for (unsigned i=0;i<24;++i) {
     const auto& tx=Wire.transactions[i];
     assert(tx.address==0x20 && tx.data[0]==2); // Never write the charger.
     const unsigned value=tx.data[1]|tx.data[2]<<8;
     assert((value & (MAIN_POWER|SCREEN_POWER))==(MAIN_POWER|SCREEN_POWER));
     assert(!(value & PA_POWER));
     assert(bool(value & POWER_PULSE)==(i%2==0));
   }
 }
 // Transient pulse errors recover without stopping the cadence.
 Wire.fail=false; Wire.failWrites=3; Wire.transactions.clear(); waits.clear();
 delayHook=+[](unsigned n) {
   assert(n==100);
   if(waits.size()==10) throw PowerRemoved{};
 };
 try { shutdown(); } catch(const PowerRemoved&) {}
 delayHook=nullptr;
 assert(Wire.failWrites==0 && Wire.transactions.size()==10);
 assert(!(output & POWER_PULSE));
 // Initialization failure retries every second, then enters the same loop.
 ready=false; Wire.beginOk=false; waits.clear(); romLogs=0;
 delayHook=+[](unsigned n) {
   if(waits.size()<=2) {
     assert(n==1000);
     if(waits.size()==2) Wire.beginOk=true;
   }
   if(waits.size()==12) throw PowerRemoved{};
 };
 try { shutdown(); } catch(const PowerRemoved&) {}
 delayHook=nullptr;
 assert(ready && romLogs==3);
 assert(waits[2]==10 && waits[3]==120); // Board reset sequence on recovered init.
 for(unsigned i=4;i<waits.size();++i) assert(waits[i]==100);
 Wire.fail=false; Wire.input={0xff,0xff};
 InputManager input;
 input.begin();
 auto sample=[&]() { clockMs+=25; input.update(); clockMs+=25; input.update(); };
 sample();
 gpio0=LOW; sample();
 assert(input.isPressed(InputManager::BTN_CONFIRM));
 assert(!input.isPressed(InputManager::BTN_BACK));
 gpio0=HIGH; sample(); assert(!input.isPressed(InputManager::BTN_CONFIRM));
 auto touch=[&](unsigned x,unsigned y,unsigned count) {
   Wire.touch={static_cast<uint8_t>(count),static_cast<uint8_t>(x>>8),static_cast<uint8_t>(x),
               static_cast<uint8_t>(y>>8),static_cast<uint8_t>(y)};
   assert(touchIrq); touchIrq(touchArg); sample();
 };
 for (auto [x,key] : {std::pair{400u,InputManager::BTN_LEFT},std::pair{240u,InputManager::BTN_RIGHT}}) {
   touch(x,900,1); assert(input.isPressed(key));
   assert(!input.isPressed(InputManager::BTN_UP) && !input.isPressed(InputManager::BTN_DOWN));
   touch(x,900,0); assert(!input.isPressed(key));
 }
 touch(400,900,1); Wire.fail=true; sample();
 assert(!input.isPressed(InputManager::BTN_LEFT));
 Wire.fail=false; clockMs+=2001; touch(0,0,0);
 // Existing Home tap/hold events remain independent of navigation button bits.
 touch(80,900,1);
 Wire.touch[0]=0; touchIrq(touchArg); clockMs+=25; input.update();
 assert(input.wasHomeKeyTapped() && !input.wasHomeKeyLongPressed());
 sample(); assert(!input.wasHomeKeyTapped());
 touch(80,900,1); clockMs+=700; input.update();
 assert(input.wasHomeKeyLongPressed());
 Wire.touch[0]=0; touchIrq(touchArg); clockMs+=25; input.update();
 assert(!input.wasHomeKeyTapped() && !input.wasHomeKeyLongPressed());

 // One-frame feedback edges use real InputManager.cpp, before debounce/semantic actions.
 auto edge=[&](unsigned x,unsigned y,unsigned count) {
   Wire.touch={static_cast<uint8_t>(count),static_cast<uint8_t>(x>>8),static_cast<uint8_t>(x),
               static_cast<uint8_t>(y>>8),static_cast<uint8_t>(y)};
   touchIrq(touchArg); clockMs+=25; input.update();
 };
 for (auto [x,y] : {std::pair{80u,900u},std::pair{400u,900u},std::pair{240u,900u},
                    std::pair{0u,0u},std::pair{479u,799u}}) {
   edge(x,y,1); assert(input.wasTouchContactPressed());
   edge(x,y,1); assert(!input.wasTouchContactPressed());
   clockMs+=700; input.update(); assert(!input.wasTouchContactPressed());
   edge(x,y,0); assert(!input.wasTouchContactPressed());
 }
 edge(0,0,1); assert(input.wasTouchContactPressed());
 edge(100,200,1); assert(!input.wasTouchContactPressed()); // swipe starts only once
 edge(200,300,1); assert(!input.wasTouchContactPressed());
 edge(240,900,1); assert(!input.wasTouchContactPressed()); // screen -> bezel cancels
 edge(0,0,0);
 edge(400,900,1); assert(input.wasTouchContactPressed());
 input.suppressTouchContact(); assert(!input.wasTouchContactPressed());
 edge(400,900,1); assert(!input.wasTouchContactPressed());
 edge(240,900,1); assert(!input.wasTouchContactPressed()); // held across owners
 edge(0,0,0);
 edge(80,900,1); assert(input.wasTouchContactPressed());
 Wire.fail=true; edge(80,900,1); assert(!input.wasTouchContactPressed());
 Wire.fail=false; clockMs+=2001;
 edge(80,900,1); assert(!input.wasTouchContactPressed()); // must return idle first
 edge(0,0,0); edge(80,900,1); assert(input.wasTouchContactPressed());
 edge(0,0,0);
 // Screen contacts obey the same suppression, owner transition and fault barriers.
 edge(100,200,1); assert(input.wasTouchContactPressed());
 input.clearTouchTapEvent(); assert(!input.wasTouchContactPressed());
 edge(100,200,1); assert(!input.wasTouchContactPressed());
 edge(100,200,0); assert(!input.wasTouchContactPressed());
 edge(100,200,1); assert(input.wasTouchContactPressed());
 input.suppressTouchContact(); assert(!input.wasTouchContactPressed());
 edge(100,200,1); assert(!input.wasTouchContactPressed());
 edge(100,200,0); assert(!input.wasTouchContactPressed());
 edge(100,200,1); assert(input.wasTouchContactPressed());
 Wire.fail=true; edge(100,200,1); assert(!input.wasTouchContactPressed());
 Wire.fail=false; clockMs+=2001;
 edge(100,200,1); assert(!input.wasTouchContactPressed());
 edge(100,200,0); edge(100,200,1); assert(input.wasTouchContactPressed());
 edge(80,900,1); assert(!input.wasTouchContactPressed());
 edge(100,200,1); assert(!input.wasTouchContactPressed()); // cannot re-enter screen until release
 edge(0,0,0);
 gpio0=LOW; sample(); assert(!input.wasTouchContactPressed());
 gpio0=HIGH; sample();
 Wire.transactions.clear();
 input.prepareForDeepSleep(); assert(!input.wasTouchContactPressed());
 assert(Wire.transactions.size()==1);
 assert(Wire.transactions[0].address==0x15);
 assert((Wire.transactions[0].data==std::vector<uint8_t>{0xA5,0x03}));
 // Failed sleep command still completes shutdown; a new boot resets touch.
 InputManager failedSleep; failedSleep.begin(); Wire.fail=true;
 failedSleep.prepareForDeepSleep(); Wire.fail=false;

#if FREEINK_CAP_HAPTIC
 static_assert(HasHapticSetting<CrossPointSettings>::value);
 assert(CrossPointSettings::getInstance().hapticFeedbackLevel==CrossPointSettings::HAPTIC_FEEDBACK_MEDIUM);
 using namespace freeink::haptic;
 assert(pulseDuration(0)==0 && pulseDuration(1)==20 && pulseDuration(2)==35 && pulseDuration(3)==60);
 assert(pulseDuration(255)==35);
 failDirection=true; assert(!freeink::haptic::begin());
 assert(motorLevel==0 && timerCreates==0); failDirection=false;
 failCreate=true; assert(!freeink::haptic::begin());
 pulse(35); assert(motorLevel==0 && timerStarts==0);
 failCreate=false; assert(freeink::haptic::begin());
 assert(freeink::haptic::begin() && timerCreates==2); // only one successful allocation
 for (unsigned level : {0u,1u,2u,3u,255u}) {
   stop(); pulse(pulseDuration(level));
   if (level==0) { assert(motorLevel==0); continue; }
   assert(motorLevel==1 && timeout==static_cast<unsigned>(pulseDuration(level))*1000);
   const auto starts=timerStarts;
   pulse(60); assert(timerStarts==starts); // no queue or extension
   timerCallback(nullptr); assert(motorLevel==1); // stale/early callback
   timerNow+=timeout; timerCallback(nullptr); assert(motorLevel==0);
 }
 pulse(35); stop(); assert(motorLevel==0);
 pulse(60); timerCallback(nullptr); assert(motorLevel==1); // callback from stopped pulse
 timerNow+=60000; timerCallback(nullptr); assert(motorLevel==0);
 pulse(35); prepareForSleep(); assert(motorLevel==0 && motorHeld);
 timerCallback(nullptr); assert(motorLevel==0);
 gpio_hold_dis(44);
 failStart=true; pulse(35); assert(motorLevel==0 && deadline==0); failStart=false;
 // Both orders of stop vs expiry, including actual concurrent callback execution.
 for (int i=0;i<100;++i) {
   pulse(20); timerNow+=20000;
   std::thread callback([] { timerCallback(nullptr); });
   stop(); callback.join(); assert(motorLevel==0 && deadline==0);
 }
#else
 static_assert(FREEINK_CAP_HAPTIC==0);
 static_assert(!HasHapticSetting<CrossPointSettings>::value);
#endif
}
'''.replace("TRANSFORM", transform)
            (tmp / "test.cpp").write_text(source)
            for flags in ([], ["-DFREEINK_CAP_HAPTIC=0"]):
                subprocess.run(["c++", "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
                                "-I" + str(tmp), "-I" + str(SDK / "BoardConfig/include"),
                                "-I" + str(SDK / "InputManager/include"), "-DFREEINK_DEVICE_METALIO_EINK4=1",
                                "-I" + str(ROOT / "src"), "-I" + str(ROOT / "lib/Epub"),
                                *flags, str(SDK / "InputManager/src/InputManager.cpp"), str(tmp / "test.cpp"),
                                "-o", str(tmp / "test")], check=True)
                subprocess.run([str(tmp / "test")], check=True)


    def test_firmware_scanner_checks_metalio_across_chunk_boundaries(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            (tmp / "BoardConfig.h").write_text("#define FREEINK_DEVICE_METALIO_EINK4 1\n")
            (tmp / "test.cpp").write_text(r'''
#include <FirmwareBoardTag.h>
#include <cassert>
#include <cstring>
int main() {
  assert(board_tag::boardNameLen()==std::strlen("metalio_eink4"));
  const char* tags[]={"CROSSPOINT-BOARD-V1:metalio_eink4;",
                      "CROSSPOINT-BOARD-V1:waveshare_epaper_397;",
                      "CROSSPOINT-BOARD-V1:x4;"};
  for (unsigned i=0;i<3;++i) for (size_t split=0;split<=std::strlen(tags[i]);++split) {
    board_tag::Scanner scanner;
    auto data=reinterpret_cast<const uint8_t*>(tags[i]);
    scanner.feed(data,split);
    scanner.feed(data+split,std::strlen(tags[i])-split);
    assert(scanner.mismatch()==(i!=0));
  }
}
''')
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(tmp), "-I" + str(ROOT / "src/network"),
                            str(ROOT / "src/network/FirmwareBoardTag.cpp"), str(tmp / "test.cpp"),
                            "-o", str(tmp / "test")], check=True)
            subprocess.run([str(tmp / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
