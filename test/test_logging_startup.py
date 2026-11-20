"""Run the real logger before serial construction, then after begin, on both transports."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='logging-startup-') as directory:
    tmp = Path(directory)
    (tmp / 'Arduino.h').write_text(r'''#pragma once
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define RTC_NOINIT_ATTR
inline unsigned long millis(){return 42;}
class Print {
 public:
 virtual size_t write(uint8_t)=0;
 virtual size_t write(const uint8_t*,size_t)=0;
 virtual void flush(){}
 size_t print(const char* p){return write((const uint8_t*)p,std::strlen(p));}
};
''')
    (tmp / 'HardwareSerial.h').write_text(r'''#pragma once
#include <Arduino.h>
class HardwareSerial:public Print {
 public:
 bool begun=false;unsigned writes=0;
 void begin(unsigned long){begun=true;}
 operator bool()const{return false;} // A disconnected host must not suppress later logs.
 size_t write(uint8_t)override{assert(begun);++writes;return 1;}
 size_t write(const uint8_t*,size_t n)override{assert(begun);++writes;return n;}
};
extern HardwareSerial testSerial;
#define Serial testSerial
''')
    (tmp / 'HWCDC.h').write_text('#pragma once\nusing HWCDC=HardwareSerial;\n')
    (tmp / 'BoardConfig.h').write_text('#define FREEINK_LOG_TRANSPORT_ROM_PRINTF 2\n')
    (tmp / 'esp_rom_sys.h').write_text(r'''#pragma once
inline unsigned romWrites=0;
inline int esp_rom_printf(const char*,...){++romWrites;return 1;}
''')
    (tmp / 'test.cpp').write_text(r'''#include <Logging.h>
#include <esp_rom_sys.h>
struct EarlyLog {EarlyLog(){logPrintf("DBG","BOOT","before-begin\n");}} earlyLog;
HardwareSerial testSerial;
MySerialImpl MySerialImpl::instance;
size_t MySerialImpl::write(uint8_t){return 0;}
size_t MySerialImpl::write(const uint8_t*,size_t){return 0;}
void MySerialImpl::flush(){}
int main(){
 assert(!loggingSerialStarted && testSerial.writes==0);
 assert(getLastLogs().find("before-begin")!=std::string::npos);
 assert(romWrites==(FREEINK_LOG_TRANSPORT==2 ? 1u:0u));
 Serial.begin(115200);
 assert(loggingSerialStarted && testSerial.begun);
 logPrintf("INF","BOOT","after-begin\n");
 assert(getLastLogs().find("after-begin")!=std::string::npos);
 assert(testSerial.writes==(FREEINK_LOG_TRANSPORT==2 ? 0u:1u));
 assert(romWrites==(FREEINK_LOG_TRANSPORT==2 ? 2u:0u));
}
''')
    for transport, cdc in ((0, 0), (0, 1), (2, 0)):
        binary = tmp / f'test-{transport}-{cdc}'
        subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        f'-DFREEINK_LOG_TRANSPORT={transport}', f'-DARDUINO_USB_CDC_ON_BOOT={cdc}',
                        '-I' + str(tmp), '-I' + str(ROOT / 'lib/Logging'), str(tmp / 'test.cpp'),
                        str(ROOT / 'lib/Logging/Logging.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Constructor-time ring logs, serial begin gating, disconnected USB and ROM transport passed')
