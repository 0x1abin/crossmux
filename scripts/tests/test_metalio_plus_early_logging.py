"""Compile production logging with the real profile before Arduino Serial is ready."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EarlyLoggingTest(unittest.TestCase):
    def test_plus_early_log_uses_rom_and_keeps_crash_ring_without_serial_access(self):
        with tempfile.TemporaryDirectory(prefix='metalio-plus-early-log-') as tmp:
            work = Path(tmp)
            (work / 'Arduino.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <cstring>
#include <cstdio>
#include <cstdarg>
constexpr int HIGH=1, LOW=0, INPUT=1, OUTPUT=3, INPUT_PULLUP=5;
void pinMode(int,int); void digitalWrite(int,int); int digitalRead(int);
void delay(unsigned long); void delayMicroseconds(unsigned int);
inline unsigned long millis() { return 1; }
#define RTC_NOINIT_ATTR
#include <HWCDC.h>
#define Serial HWCDCSerial
''')
            (work / 'HWCDC.h').write_text('''#pragma once
#include <cstddef>
#include <cstdint>
extern unsigned unsafeSerialCalls;
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual void flush() {}
};
class HWCDC : public Print {
 public:
  void begin(unsigned long) {}
  operator bool() const { return false; }
  void print(const char*) { ++unsafeSerialCalls; }
  size_t write(uint8_t) override { ++unsafeSerialCalls; return 0; }
  size_t write(const uint8_t*, size_t) override { ++unsafeSerialCalls; return 0; }
};
inline HWCDC HWCDCSerial;
''')
            (work / 'HardwareSerial.h').write_text('#pragma once\n#include <HWCDC.h>\nusing HardwareSerial = HWCDC;\n')
            (work / 'esp_rom_sys.h').write_text('#pragma once\nint esp_rom_printf(const char*, ...);\n')
            harness = work / 'harness.cpp'
            harness.write_text('''#include <Logging.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
unsigned unsafeSerialCalls = 0;
std::string romOutput;
int esp_rom_printf(const char* format, ...) {
  char data[512]; va_list args; va_start(args, format);
  const int count=vsnprintf(data,sizeof(data),format,args); va_end(args);
  romOutput += data; return count;
}
int main() {
  // UITheme emits this before HWCDCSerial's dynamic constructor on native S31.
  logPrintf("DBG", "UI", "Using INX theme\\n");
  assert(unsafeSerialCalls==0);
  assert(romOutput=="[1] [DBG] [UI] Using INX theme\\n");
  assert(getLastLogs().find("Using INX theme")!=std::string::npos);
}
''')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-DFREEINK_DEVICE_METALIO_EINK4_PLUS=1', '-DARDUINO_USB_CDC_ON_BOOT=1',
                            '-I' + str(work), '-I' + str(ROOT / 'lib/Logging'),
                            '-I' + str(ROOT / 'freeink-sdk/libs/hardware/BoardConfig/include'),
                            '-I' + str(ROOT / 'freeink-sdk/libs/hardware/XteinkDetect/test/host/stubs'),
                            str(ROOT / 'lib/Logging/Logging.cpp'), str(harness), '-o', str(work / 'test')], check=True)
            subprocess.run([str(work / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
