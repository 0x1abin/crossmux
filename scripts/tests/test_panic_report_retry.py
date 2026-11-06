"""Run the production boot/report methods across host-modelled retained-RTC resets."""
import unittest

from test_reading_ui_regressions import ROOT, method, run_cpp


class PanicReportRetryTest(unittest.TestCase):
    def test_report_fault_does_not_repeat_or_change_boot_routing(self):
        source = (ROOT / 'lib/hal/HalSystem.cpp').read_text()
        retained = source[source.index('#define MAX_PANIC_STACK_DEPTH'):source.index('extern "C"')]
        namespace = source.index('namespace HalSystem {') + len('namespace HalSystem {')
        state = source[namespace:source.index('void begin()', namespace)]
        methods = '\n'.join(method(source, name) for name in (
            'void begin(', 'void checkPanic(', 'void clearPanic(', 'bool isRebootFromPanic('))
        program = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#define RTC_NOINIT_ATTR
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
constexpr int O_WRITE=1,O_CREAT=2,O_TRUNC=4;
enum esp_reset_reason_t {
 ESP_RST_PANIC,ESP_RST_CPU_LOCKUP,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,
 ESP_RST_POWERON,ESP_RST_SW,ESP_RST_DEEPSLEEP,ESP_RST_BROWNOUT,ESP_RST_UNKNOWN
};
enum class Fault {None,OpenFailure,ShortWrite,FormatReset,OpenReset,WriteReset,CloseReset,DestructReset};
Fault fault=Fault::None;
esp_reset_reason_t reason=ESP_RST_PANIC,reportReset=ESP_RST_PANIC;
esp_reset_reason_t esp_reset_reason() {return reason;}
int attempts=0,failures=0,checks=0;
bool captureOnReset=false,corruptLogHead=false;
bool sanitizeLogHead() {return corruptLogHead;}
void clearLastLogs() {corruptLogHead=false;}
namespace HalSystem {
struct StackFrame {uint32_t sp,spp[8];};
void begin();void checkPanic();void clearPanic();bool isRebootFromPanic();
std::string getPanicInfo(bool);
}
''' + retained + r'''
struct Reset {};
void resetDuringReport() {
 reason=reportReset;
 if(captureOnReset) panicCaptureMarker=PANIC_CAPTURE_MAGIC;
 throw Reset{};
}
struct File {
 bool valid;
 ~File() noexcept(false) {if(fault==Fault::DestructReset) resetDuringReport();}
 explicit operator bool() const {return valid;}
 size_t write(const char*,size_t count) {
  if(fault==Fault::WriteReset) resetDuringReport();
  return fault==Fault::ShortWrite?count-1:count;
 }
 void close() {if(fault==Fault::CloseReset) resetDuringReport();}
};
struct {
 File open(const char*,int) {
  if(fault==Fault::OpenReset) resetDuringReport();
  return {fault!=Fault::OpenFailure};
 }
} Storage;
namespace HalSystem {
std::string getPanicInfo(bool) {
 ++attempts;
 if(fault==Fault::FormatReset) resetDuringReport();
 return panicMessage;
}
''' + state + methods + r'''
}
void expect(bool value,const char* message) {
 ++checks;
 if(!value) {++failures;std::printf("FAIL: %s\n",message);}
}
void seed(esp_reset_reason_t reset,bool capture=true) {
 HalSystem::clearPanic();
 reason=reset;fault=Fault::None;attempts=0;corruptLogHead=true;
 panicCaptureMarker=capture?PANIC_CAPTURE_MAGIC:0;
 std::strcpy(panicMessage,"original diagnostic");
 panicStack[0].sp=0x1234;
 HalSystem::begin();
}
int main() {
 constexpr esp_reset_reason_t panicResets[]={ESP_RST_PANIC,ESP_RST_CPU_LOCKUP,ESP_RST_INT_WDT,
                                            ESP_RST_TASK_WDT,ESP_RST_WDT};
 for(const auto reset:panicResets) for(const auto io:{Fault::None,Fault::OpenFailure,Fault::ShortWrite}) {
  seed(reset);fault=io;
  expect(HalSystem::isRebootFromPanic(),"captured panic routes to crash screen before reporting");
  HalSystem::checkPanic();
  expect(HalSystem::isRebootFromPanic(),"report outcome preserves current boot classification");
  expect(!std::strcmp(panicMessage,"original diagnostic") && panicStack[0].sp==0x1234,
         "reporting retains crash-screen diagnostics");
  expect(!corruptLogHead,"panic boot sanitizes retained logs");
  HalSystem::checkPanic();
  expect(attempts==1,"one report attempt per boot, including open and short-write failures");
  fault=Fault::None;reason=ESP_RST_WDT;
  HalSystem::begin();HalSystem::checkPanic();
  expect(!HalSystem::isRebootFromPanic() && attempts==1,"old capture cannot classify a later bare watchdog");
 }
 for(const auto stage:{Fault::FormatReset,Fault::OpenReset,Fault::WriteReset,Fault::CloseReset,Fault::DestructReset})
 for(const auto reset:panicResets) for(const bool recapture:{false,true}) {
  seed(ESP_RST_PANIC);fault=stage;reportReset=reset;captureOnReset=recapture;
  bool interrupted=false;
  try {HalSystem::checkPanic();} catch(const Reset&) {interrupted=true;}
  expect(interrupted && attempts==1,"fault injection interrupted the report attempt");
  fault=Fault::None;
  HalSystem::begin();
  const bool expectedPanic=reset==ESP_RST_PANIC || reset==ESP_RST_CPU_LOCKUP || recapture;
  expect(HalSystem::isRebootFromPanic()==expectedPanic,"next boot preserves actual reset classification");
  HalSystem::checkPanic();HalSystem::checkPanic();
  expect(attempts==1,"interrupted report is skipped on the next boot even if hooks recapture");
  reason=ESP_RST_WDT;
  HalSystem::begin();HalSystem::checkPanic();
  expect(!HalSystem::isRebootFromPanic() && attempts==1,"skipped report consumes stale capture");
  reason=ESP_RST_PANIC;panicCaptureMarker=PANIC_CAPTURE_MAGIC;
  HalSystem::begin();HalSystem::checkPanic();
  expect(HalSystem::isRebootFromPanic() && attempts==2,"later independent panic can create a fresh report");
 }
 for(const auto reset:{ESP_RST_POWERON,ESP_RST_SW,ESP_RST_DEEPSLEEP,ESP_RST_BROWNOUT,ESP_RST_UNKNOWN}) {
  seed(ESP_RST_PANIC);fault=Fault::OpenReset;reportReset=reset;captureOnReset=false;
  try {HalSystem::checkPanic();} catch(const Reset&) {}
  fault=Fault::None;
  HalSystem::begin();HalSystem::checkPanic();
  expect(!HalSystem::isRebootFromPanic() && attempts==1,"ordinary reset does not report old diagnostics");
  expect(panicMessage[0]=='\0' && panicStack[0].sp==0,"ordinary reset clears retained diagnostics");
  reason=ESP_RST_PANIC;panicCaptureMarker=PANIC_CAPTURE_MAGIC;
  HalSystem::begin();HalSystem::checkPanic();
  expect(attempts==2,"ordinary reset clears an interrupted report guard");
 }
 for(const auto reset:{ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT}) {
  seed(reset,false);HalSystem::checkPanic();
  expect(!HalSystem::isRebootFromPanic() && attempts==0,"uncaptured watchdog is an ordinary boot");
 }
 std::printf("panic report reset contracts: %d checks, %d failures\n",checks,failures);
 return failures?1:0;
}
'''
        run_cpp(program)


if __name__ == '__main__':
    unittest.main()
