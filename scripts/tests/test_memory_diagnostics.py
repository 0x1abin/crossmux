"""Periodic heap walks must never overlap rendering or consume a deferred sample."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory(prefix='memory-diagnostics-') as temp:
    work = Path(temp)
    (work / 'activities').mkdir()
    (work / 'Arduino.h').write_text('''#pragma once
#include <cstdint>
extern uint32_t now;
inline uint32_t millis() { return now; }
''')
    (work / 'activities/RenderLock.h').write_text('''#pragma once
extern bool rendering, locked;
struct RenderLock {
  enum class Mode { Blocking, Try };
  bool owned;
  explicit RenderLock(Mode mode) : owned(!rendering) {
    assert(mode == Mode::Try);
    if (owned) { assert(!locked); locked = true; }
  }
  ~RenderLock() { if (owned) locked = false; }
  bool ownsLock() const { return owned; }
};
'''.replace('#pragma once', '#pragma once\n#include <cassert>'))
    (work / 'HalMemory.h').write_text('''#pragma once
#include <cassert>
#include <cstddef>
extern bool rendering, locked;
extern int samples;
struct HalMemory {
  struct HeapStats { size_t freeBytes, totalBytes, minFreeBytes, largestBlockBytes; };
  static HeapStats read() {
    assert(!rendering && locked && "heap walk overlapped rendering");
    ++samples;
    return {100, 200, 50, 80};
  }
  static HeapStats getInternalHeap() { return read(); }
  static HeapStats getPsramHeap() { return read(); }
};
''')
    (work / 'Logging.h').write_text('''#pragma once
#include <cassert>
extern bool locked;
extern int messages;
template <typename... Args> void recordLog(Args...) { assert(locked); ++messages; }
#define LOG_INF(...) recordLog(__VA_ARGS__)
''')
    (work / 'BleInput.h').write_text('''#pragma once
#include <cassert>
extern bool locked, bluetooth;
extern int bleMessages;
namespace bleinput {
inline bool isRunning() { return bluetooth; }
inline void logDiagnostics(const char*) { assert(locked); ++bleMessages; }
}
''')
    (work / 'check.cpp').write_text('''#include "util/MemoryDiagnostics.h"
#include <cassert>
#include <cstdint>
uint32_t now = 9999;
bool rendering = false, locked = false, bluetooth = false;
int samples = 0, messages = 0, bleMessages = 0;
#ifdef BOARD_HAS_PSRAM
constexpr int perSample = 2;
#else
constexpr int perSample = 1;
#endif
int main() {
  logMemoryDiagnosticsIfDue();
  assert(samples == 0);
  now = 10000;
  rendering = true;
  logMemoryDiagnosticsIfDue();
  assert(samples == 0 && messages == 0 && !locked);
  // A busy refresh defers, rather than drops, the already-due sample.
  now = 10001;
  rendering = false;
  logMemoryDiagnosticsIfDue();
  assert(samples == perSample && messages == perSample && !locked);
  now = 20000;
  logMemoryDiagnosticsIfDue();
  assert(samples == perSample);
  now = 20001;
  bluetooth = true;
  logMemoryDiagnosticsIfDue();
  assert(samples == 2 * perSample && bleMessages == 1 && !locked);
  // The periodic schedule also survives the 32-bit millisecond wrap.
  now = UINT32_MAX - 4999;
  logMemoryDiagnosticsIfDue();
  assert(samples == 3 * perSample);
  now = 4999;
  logMemoryDiagnosticsIfDue();
  assert(samples == 3 * perSample);
  now = 5000;
  logMemoryDiagnosticsIfDue();
  assert(samples == 4 * perSample && messages == 4 * perSample && bleMessages == 3);
}
''')
    for psram in (False, True):
        binary = work / ('psram' if psram else 'internal')
        flags = ['-DBOARD_HAS_PSRAM=1'] if psram else []
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        *flags, '-I' + str(work), '-I' + str(ROOT / 'src'),
                        str(work / 'check.cpp'), str(ROOT / 'src/util/MemoryDiagnostics.cpp'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Memory diagnostics: busy deferral, locked heap/BLE logging, interval and rollover passed')
