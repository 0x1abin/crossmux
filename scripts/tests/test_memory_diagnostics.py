"""Execute real periodic diagnostics and their main-loop transport gate on the host."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MemoryDiagnosticsTest(unittest.TestCase):
    def test_render_deferral_cadence_and_transport_eligibility(self):
        source = (ROOT / "src/main.cpp").read_text()
        loop = source[source.index("void loop() {"):]
        start = loop.index("  // The ROM console")
        end = loop.index("  // Handle incoming serial commands,", start)
        # Execute the actual call-site gate, including the original block before
        # extraction, so RED demonstrates an unlocked heap walk rather than a
        # missing-file compiler error. The production helper is linked when present.
        timer = "\n".join(line for line in loop[:start].splitlines() if "lastMemPrint" in line)
        call_site = "void mainLoopDiagnostics() {\n" + timer + "\n" + loop[start:end] + "}\n"
        helper = ROOT / "src/util/MemoryDiagnostics.cpp"
        header = '#include "util/MemoryDiagnostics.h"\n' if helper.exists() else ""

        with tempfile.TemporaryDirectory(prefix="memory-diagnostics-") as directory:
            work = Path(directory)
            (work / "activities").mkdir()
            (work / "Arduino.h").write_text(r'''#pragma once
#include <cstdint>
extern uint32_t now;
inline uint32_t millis() { return now; }
extern bool serialConnected;
struct HostSerial { explicit operator bool() const { return serialConnected; } };
inline HostSerial Serial;
#define FREEINK_LOG_TRANSPORT_ROM_PRINTF 1
''')
            (work / "activities/RenderLock.h").write_text(r'''#pragma once
#include <cassert>
extern bool rendering, locked;
extern int lockAttempts;
struct RenderLock {
  enum class Mode { Blocking, Try };
  bool owned;
  explicit RenderLock(Mode mode = Mode::Blocking) : owned(!rendering) {
    ++lockAttempts;
    assert(mode == Mode::Try && "diagnostics must not block input");
    if (owned) { assert(!locked); locked = true; }
  }
  ~RenderLock() { if (owned) locked = false; }
  bool ownsLock() const { return owned; }
};
''')
            (work / "HalMemory.h").write_text(r'''#pragma once
#include <cassert>
#include <cstddef>
extern bool rendering, locked;
extern int internalSamples, psramSamples;
struct HalMemory {
  struct HeapStats { size_t freeBytes, totalBytes, minFreeBytes, largestBlockBytes; };
  static HeapStats getInternalHeap() {
    assert(!rendering && locked && "internal heap walk overlapped rendering");
    ++internalSamples;
    return {100, 200, 50, 80};
  }
  static HeapStats getPsramHeap() {
    assert(!rendering && locked && "PSRAM heap walk overlapped rendering");
    ++psramSamples;
    return {1000, 2000, 500, 800};
  }
};
''')
            (work / "Logging.h").write_text(r'''#pragma once
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
extern bool rendering, locked;
extern uint32_t now, loggingAdvance;
extern int messages;
inline void recordLog(const char* tag, const char* format, size_t freeBytes, size_t totalBytes,
                      size_t minFreeBytes, size_t largestBlockBytes) {
  assert(locked && !rendering);
  assert(std::strcmp(tag, "MEM") == 0);
  char line[192];
  std::snprintf(line, sizeof(line), format, freeBytes, totalBytes, minFreeBytes, largestBlockBytes);
  assert(std::strcmp(line, "Free: 100 bytes, Total: 200 bytes, Min Free: 50 bytes, MaxAlloc: 80 bytes") == 0 ||
         std::strcmp(line, "PSRAM: Free: 1000 bytes, Total: 2000 bytes, Min Free: 500 bytes, MaxAlloc: 800 bytes") == 0);
  ++messages;
  now += loggingAdvance;
}
#define LOG_INF(...) recordLog(__VA_ARGS__)
''')
            (work / "BleInput.h").write_text(r'''#pragma once
#include <cassert>
#include <cstring>
extern bool rendering, locked, bluetooth;
extern int bleQueries, bleMessages;
namespace bleinput {
inline bool isRunning() { assert(locked && !rendering); ++bleQueries; return bluetooth; }
inline void logDiagnostics(const char* phase) {
  assert(locked && !rendering && bluetooth && std::strcmp(phase, "running") == 0);
  ++bleMessages;
}
}
''')
            harness = r'''
#include <Arduino.h>
#include <HalMemory.h>
#include <Logging.h>
#include "BleInput.h"
#include "activities/RenderLock.h"
#include <cassert>
#include <cstring>
uint32_t now = 0, loggingAdvance = 0;
bool serialConnected = true, rendering = false, locked = false, bluetooth = false;
int internalSamples = 0, psramSamples = 0, messages = 0, bleQueries = 0, bleMessages = 0, lockAttempts = 0;
#ifdef BOARD_HAS_PSRAM
constexpr int perSample = 2;
#else
constexpr int perSample = 1;
#endif
void expectSamples(int expected) {
  assert(internalSamples == expected && messages == expected * perSample && bleQueries == expected);
  assert(psramSamples == expected * (perSample - 1) && !locked);
}
'''
            checks = r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  if (std::strcmp(argv[1], "gate") == 0) {
    now = 10000;
    serialConnected = false;
    mainLoopDiagnostics();
    const bool rom = FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF;
    expectSamples(rom ? 1 : 0);
    assert(lockAttempts == (rom ? 1 : 0));
    serialConnected = true;
    mainLoopDiagnostics();
    expectSamples(1);
    bluetooth = true;
    now = 20000;
    serialConnected = false;
    mainLoopDiagnostics();
    expectSamples(rom ? 2 : 1);
    serialConnected = true;
    mainLoopDiagnostics();
    expectSamples(2);
    assert(bleMessages == 1 && lockAttempts == 2);
    return 0;
  }

  now = 9999;
  mainLoopDiagnostics();
  expectSamples(0);
  assert(lockAttempts == 0);
  now = 10000;
  rendering = true;
  mainLoopDiagnostics();
  now = 10001;
  mainLoopDiagnostics();
  expectSamples(0);
  assert(lockAttempts == 2 && bleMessages == 0);
  // Busy refreshes preserve the due sample for the next idle loop.
  now = 10002;
  rendering = false;
  loggingAdvance = 7;
  mainLoopDiagnostics();
  expectSamples(1);
  const uint32_t completed = now;
  assert(completed == 10002 + 7 * perSample && bleMessages == 0);
  now = completed + 9999;
  mainLoopDiagnostics();
  expectSamples(1);
  assert(lockAttempts == 3);
  now = completed + 10000;
  bluetooth = true;
  mainLoopDiagnostics();
  expectSamples(2);
  assert(bleMessages == 1 && lockAttempts == 4);

  loggingAdvance = 0;
  now = UINT32_MAX - 4999;
  mainLoopDiagnostics();
  expectSamples(3);
  now = 4999;
  mainLoopDiagnostics();
  expectSamples(3);
  now = 5000;
  mainLoopDiagnostics();
  expectSamples(4);
  assert(bleMessages == 3 && lockAttempts == 6);
}
'''
            (work / "check.cpp").write_text(header + harness + call_site + checks)
            for psram in (False, True):
                for rom in (False, True):
                    with self.subTest(psram=psram, rom=rom):
                        binary = work / f"check-{int(psram)}-{int(rom)}"
                        flags = ["-DBOARD_HAS_PSRAM=1"] if psram else []
                        compiled = subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                            "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fno-exceptions",
                            f"-DFREEINK_LOG_TRANSPORT={int(rom)}", *flags,
                            "-I" + str(work), "-I" + str(ROOT / "src"), str(work / "check.cpp"),
                            *([str(helper)] if helper.exists() else []), "-o", str(binary)
                        ], capture_output=True, text=True)
                        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                        for scenario in ("timing", "gate"):
                            result = subprocess.run([str(binary), scenario], capture_output=True, text=True)
                            self.assertEqual(result.returncode, 0, f"{scenario}: " + result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
