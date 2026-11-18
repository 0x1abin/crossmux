#include "MemoryDiagnostics.h"

#include <Arduino.h>
#include <HalMemory.h>
#include <Logging.h>

#include "BleInput.h"
#include "activities/RenderLock.h"

void logMemoryDiagnosticsIfDue() {
  static uint32_t lastMemPrint = 0;
  if (millis() - lastMemPrint < 10000) return;

  // Largest-block queries walk the allocator under a critical-section lock.
  // Keep them outside panel streaming, without blocking input on a busy render.
  RenderLock lock(RenderLock::Mode::Try);
  if (!lock.ownsLock()) return;

  const auto heap = HalMemory::getInternalHeap();
  LOG_INF("MEM", "Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes", heap.freeBytes,
          heap.totalBytes, heap.minFreeBytes, heap.largestBlockBytes);
#ifdef BOARD_HAS_PSRAM
  const auto psram = HalMemory::getPsramHeap();
  LOG_INF("MEM", "PSRAM: Free: %zu bytes, Total: %zu bytes, Min Free: %zu bytes, MaxAlloc: %zu bytes", psram.freeBytes,
          psram.totalBytes, psram.minFreeBytes, psram.largestBlockBytes);
#endif
  if (bleinput::isRunning()) bleinput::logDiagnostics("running");
  lastMemPrint = millis();
}
