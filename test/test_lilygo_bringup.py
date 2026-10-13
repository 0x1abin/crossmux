"""Run the SDK board and actual HAL ordering/gray allocation paths on the host."""
from pathlib import Path
import os
import configparser
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "freeink-sdk"


def function(source, signature):
    start = source.index(signature)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def run():
    config = configparser.ConfigParser(interpolation=None)
    config.read(ROOT / "platformio.ini")
    assert "-DFREEINK_DEVICE_LILYGO=1" in config["lilygo_t5s3_hardware"]["build_flags"]
    assert config["lilygo_t5s3_hardware"]["board_build.arduino.memory_type"] == "dio_opi"
    assert "M5GFX.git#0.2.20" in config["lilygo_t5s3_hardware"]["lib_deps"]
    with tempfile.TemporaryDirectory(prefix="lilygo-bringup-") as directory:
        tmp = Path(directory)
        headers = {
            "Arduino.h": r"""#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>
constexpr int INPUT=0, OUTPUT=1, INPUT_PULLUP=2, HIGH=1, LOW=0;
inline std::vector<int> sequence;
inline void pinMode(int,int) {}
inline void digitalWrite(int,int) {}
""",
            "SPI.h": "#pragma once\nstruct Spi {void begin(int,int,int,int) {sequence.push_back(1);}}; inline Spi SPI;\n",
            "InputManager.h": r"""#pragma once
struct InputManager {
 static constexpr int BTN_DOWN=5;
 static inline uint8_t (*hook)()=nullptr;
 static void setButtonHook(uint8_t (*value)()) {hook=value; sequence.push_back(2);}
 void begin() {sequence.push_back(3);}
};
""",
            "Wire.h": r"""#pragma once
#include <array>
struct WireType {
 bool present=true; int address=0, reg=0, cursor=0; std::vector<uint8_t> out;
 std::array<uint8_t,8> regs{255,255,255,255,0,0,255,255};
 void begin(int sda,int scl) {assert(sda==39 && scl==40); sequence.push_back(0);}
 void setClock(int hz) {assert(hz==400000);} void setTimeOut(int) {}
 void beginTransmission(int a) {address=a;out.clear();}
 void write(uint8_t b) {out.push_back(b);}
 void write(const uint8_t* data,size_t n) {out.insert(out.end(),data,data+n);}
 int endTransmission(bool=true) {if(!present)return 1; assert(address==0x20);if(!out.empty()) {reg=out[0]; for(size_t i=1;i<out.size();++i) regs.at(reg+i-1)=out[i];}return 0;}
 uint8_t requestFrom(uint8_t,uint8_t n) {cursor=reg;return n;}
 int available() {return 0;} uint8_t read() {return regs.at(cursor++);}
}; inline WireType Wire;
""",
            "freertos/FreeRTOS.h": "#pragma once\nusing SemaphoreHandle_t=void*; constexpr int portMAX_DELAY=0;\n",
            "freertos/semphr.h": "#pragma once\ninline void* xSemaphoreCreateRecursiveMutex(){return reinterpret_cast<void*>(1);}\ninline void xSemaphoreTakeRecursive(void*,int){}\ninline void xSemaphoreGiveRecursive(void*){}\n",
        }
        for name, content in headers.items():
            path=tmp/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(content)
        body=function((ROOT/"lib/hal/HalGPIO.cpp").read_text(), "void HalGPIO::begin()")
        source=r"""
#include <cassert>
#include <Arduino.h>
#include <BoardT5S3.h>
#include <InputManager.h>
#include <SPI.h>
#include <Wire.h>
#define LOG_ERR(...) ((void)0)
struct HalGPIO {
 enum class DeviceType {X4};DeviceType _deviceType=DeviceType::X4;
 InputManager inputMgr;bool inputBegun=false;void begin();
};
"""+body+r"""
int main() {
 HalGPIO gpio;gpio.begin();assert(gpio.inputBegun);
#if FREEINK_DEVICE_LILYGO
 assert((sequence==std::vector<int>{0,1,2,3}));assert(InputManager::hook);
 assert((Wire.regs[7] & (1<<2)) != 0); // PCA button pin stays input.
 Wire.regs[1]&=~(1<<2);assert(InputManager::hook()==(1<<InputManager::BTN_DOWN));
 Wire.regs[1]|=1<<2;assert(InputManager::hook()==0);
 Wire.present=false;assert(InputManager::hook()==0); // Failed read releases the key.
 sequence.clear();gpio.begin();assert(gpio.inputBegun && !BoardT5S3::pca9535Present());
#else
 assert((sequence==std::vector<int>{3}));assert(!InputManager::hook);
#endif
}
"""
        (tmp/"board.cpp").write_text(source)
        env=dict(os.environ,CCACHE_DISABLE="1")
        for device in (0,1):
            binary=tmp/f"board-{device}"
            subprocess.run(["c++","-std=c++17",f"-DFREEINK_DEVICE_LILYGO={device}","-I"+str(tmp),
                            "-I"+str(SDK/"libs/hardware/BoardT5S3/include"),"-include","cassert",
                            str(tmp/"board.cpp"),str(SDK/"libs/hardware/BoardT5S3/src/BoardT5S3.cpp"),"-o",str(binary)],check=True,env=env)
            subprocess.run([str(binary)],check=True)
        allocation=function((SDK/"libs/display/FreeInkDisplay/src/driver/LgfxEpdDriver.cpp").read_text(),"void allocCanvas(")
        (tmp/"allocation.cpp").write_text(r"""
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <new>
bool spriteOOM=false,objectOOM=false,planeOOM=false,spriteInPsram=true;int canvasAllocations=0,planeAllocations=0;
constexpr int MALLOC_CAP_SPIRAM=1;int g_dev;
bool esp_ptr_external_ram(const void* pointer) {return pointer && spriteInPsram;}
struct Log {void printf(const char*,...) {}} Serial;
namespace lgfx {
namespace color_depth_t {constexpr int grayscale_8bit=8;}
struct LGFX_Sprite {
 uint8_t* pixels=nullptr;bool psram=false;
 static void* operator new(size_t n,const std::nothrow_t&) noexcept {return objectOOM ? nullptr : std::malloc(n);}
 static void operator delete(void* p) noexcept {std::free(p);}
 explicit LGFX_Sprite(int*) {} ~LGFX_Sprite() {std::free(pixels);}
 void setPsram(bool value) {psram=value;}
 void setColorDepth(int bits) {assert(bits==8);}
 void createSprite(int w,int h) {assert(psram && w*h==518400);++canvasAllocations; if(!spriteOOM)pixels=static_cast<uint8_t*>(std::malloc(w*h));}
 void* getBuffer() {return pixels;}
};}
lgfx::LGFX_Sprite* g_canvas=nullptr;uint8_t* g_lsb=nullptr;uint8_t* g_msb=nullptr;uint16_t g_w=0,g_h=0,g_wb=0;
void* heap_caps_malloc(size_t n,int caps) {assert(n==64800 && caps==MALLOC_CAP_SPIRAM);++planeAllocations;return planeOOM ? nullptr : std::malloc(n);}
"""+allocation+r"""
int main() {
 objectOOM=true;allocCanvas(960,540);assert(!g_canvas && g_lsb && g_msb);
 objectOOM=false;spriteOOM=true;allocCanvas(960,540);assert(!g_canvas);
 spriteOOM=false;spriteInPsram=false;allocCanvas(960,540);assert(!g_canvas);
 spriteInPsram=true;allocCanvas(960,540);assert(g_canvas && g_canvas->getBuffer());
 const int canvases=canvasAllocations,planes=planeAllocations;
 allocCanvas(960,540);assert(canvasAllocations==canvases && planeAllocations==planes);
 delete g_canvas;g_canvas=nullptr;std::free(g_lsb);std::free(g_msb);g_lsb=g_msb=nullptr;
 planeOOM=true;allocCanvas(960,540);assert(g_canvas && !g_lsb && !g_msb);delete g_canvas;
}
""")
        binary=tmp/"allocation"
        subprocess.run(["c++","-std=c++17",str(tmp/"allocation.cpp"),"-o",str(binary)],check=True,env=env)
        subprocess.run([str(binary)],check=True)
    print("LilyGo board ordering, hook/failure/macro isolation and PSRAM reuse/OOM passed")


if __name__ == "__main__":
    run()
