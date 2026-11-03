"""Exercise Metalio's real parsers and I2C sequence with small host-side MCU stubs."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = ROOT / "freeink-sdk/libs/hardware"


class MetalioTest(unittest.TestCase):
    def test_input_power_and_rotation(self):
        for library, entry in (("InputManager", "run_metalio.sh"), ("HapticManager", "run.sh")):
            subprocess.run(["sh", str(SDK / library / "test/host" / entry)], check=True)
        with tempfile.TemporaryDirectory() as directory:
            tmp = pathlib.Path(directory)
            renderer = (ROOT / "lib/GfxRenderer/GfxRenderer.cpp").read_text()
            start = renderer.index("void GfxRenderer::tapToLogical(")
            transform = renderer[start:renderer.index("\n}", start) + 2]
            source = r'''
#include <cassert>
#include <climits>
#include <initializer_list>
#include <Cst816sInput.h>
''' + r'''
class GfxRenderer {
 public:
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

}
'''
            (tmp / "check.cpp").write_text(source.replace("TRANSFORM", transform))
            subprocess.run(["c++", "-std=c++20", "-I" + str(SDK / "InputManager/include"),
                            str(tmp / "check.cpp"), "-o", str(tmp / "check")], check=True)
            subprocess.run([str(tmp / "check")], check=True)

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
