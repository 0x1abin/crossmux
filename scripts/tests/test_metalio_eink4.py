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
constexpr int INPUT_PULLUP=2, OUTPUT=1, LOW=0;
inline uint32_t clockMs=0;
inline std::vector<unsigned> waits;
inline std::vector<std::pair<int,int>> modes;
inline unsigned long millis() { return clockMs; }
inline void delay(unsigned n) { clockMs+=n; waits.push_back(n); }
inline void pinMode(int pin,int mode) { modes.emplace_back(pin,mode); }
inline void digitalWrite(int,int) {}
''')
            (tmp / "esp_rom_sys.h").write_text("#pragma once\ninline void esp_rom_printf(const char*, ...) {}\n")
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
  uint8_t status=3, address=0;
  unsigned cursor=0;
  bool fail=false;
  bool begin(int sda,int scl,unsigned hz) { assert(sda==41 && scl==42 && hz==400000); return true; }
  void setTimeOut(unsigned) {}
  void beginTransmission(uint8_t addr) { address=addr; data.clear(); }
  void write(uint8_t value) { data.push_back(value); }
  int endTransmission(bool=true) {
    transactions.push_back({address,data});
    return fail ? 1 : 0;
  }
  uint8_t requestFrom(uint8_t,uint8_t count,uint8_t) { cursor=0; return count; }
  int available() { return 0; }
  uint8_t read() { return address==0x20 ? input.at(cursor++) : status; }
};
inline MockWire Wire;
''')
            # Compile the actual renderer transform, not a second implementation of it.
            renderer = (ROOT / "lib/GfxRenderer/GfxRenderer.cpp").read_text()
            start = renderer.index("void GfxRenderer::tapToLogical(")
            transform = renderer[start:renderer.index("\n}", start) + 2]
            source = r'''
#include <cassert>
#include <Cst816sInput.h>
#include <MetalioEink4Board.h>
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
 assert(begin());
 const uint16_t values[]={BOOT_OUTPUT,static_cast<uint16_t>(~OUTPUTS),
                          BOOT_OUTPUT|SCREEN_POWER,BOOT_OUTPUT|SCREEN_POWER|TOUCH_RESET};
 assert(Wire.transactions.size()==4);
 for (unsigned i=0;i<4;++i) {
   auto& tx=Wire.transactions[i];
   assert(tx.address==0x20 && tx.data.size()==3);
   assert(tx.data[0]==(i==1 ? 6 : 2));
   assert((tx.data[1] | tx.data[2]<<8)==values[i]);
 }
 assert((output & ((1<<4)|(1<<1)))==0); // PA and routing remain off.
 bool dat3=false;
 for (auto [pin,mode] : modes) if (pin==46) { assert(mode==INPUT_PULLUP); dat3=true; }
 assert(dat3);
 assert((waits==std::vector<unsigned>{10,120}));
 assert(!powerButtonPressed(true)); assert(!powerButtonPressed(true));
 assert(!powerButtonPressed(false)); assert(powerButtonPressed(true));
 Wire.input={0x7f,0xfe}; assert(buttons()==((1<<4)|(1<<5)));
 clockMs+=21; Wire.fail=true; assert(buttons()==0);
 clockMs+=2001; Wire.fail=false; Wire.input={0xff,0xff}; assert(buttons()==0);
 bool connected=false; assert(externalPowerConnected(connected) && connected);
 clockMs+=1001; Wire.status=7; assert(externalPowerConnected(connected) && !connected);
 clockMs+=1001; Wire.fail=true; assert(!externalPowerConnected(connected));
 Wire.fail=false; Wire.transactions.clear(); waits.clear();
 assert(shutdown());
 assert((waits==std::vector<unsigned>{280,100,100,100,100,100,100}));
 assert(Wire.transactions.size()==7);
 for (unsigned i=0;i<7;++i) {
   auto& tx=Wire.transactions[i];
   assert(tx.address==0x20 && tx.data[0]==2); // No charger writes, ever.
   unsigned value=tx.data[1]|tx.data[2]<<8;
   assert((value & (MAIN_POWER|SCREEN_POWER))==(MAIN_POWER|SCREEN_POWER));
   assert(bool(value & POWER_PULSE)==(i%2==0));
 }
 Wire.fail=true; assert(!shutdown());
}
'''.replace("TRANSFORM", transform)
            (tmp / "test.cpp").write_text(source)
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(tmp), "-I" + str(SDK / "BoardConfig/include"),
                            "-I" + str(SDK / "InputManager/include"), str(tmp / "test.cpp"),
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
