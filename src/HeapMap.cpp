#include "HeapMap.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <cstdio>

#include "rom/ets_sys.h"

// heap_caps_dump walks each heap inside a critical section (interrupts
// masked), so its output can neither go through the UART-0 ROM path we can't
// see nor be flow-controlled toward the CDC (the CDC ring drains in an
// interrupt handler — waiting deadlocks into the interrupt WDT,
// field-verified). Instead the ROM putc parses each line into a compact
// record; the captured table is logged after the dump with interrupts live.
namespace heapmap {
namespace {
struct BlockRec {
  uint32_t addr;
  uint32_t size;
  bool free;
};
constexpr uint16_t kMaxRecs = 1400;
BlockRec* g_recs = nullptr;  // borrowed buffer, valid only during capture
uint16_t g_recCount = 0;
bool g_overflowed = false;
char g_line[96];
uint8_t g_lineLen = 0;

void captInterpolatePutc(char c) {
  if (c != '\n') {
    if (g_lineLen < sizeof(g_line) - 1) g_line[g_lineLen++] = c;
    return;
  }
  g_line[g_lineLen] = '\0';
  g_lineLen = 0;
  // e.g. "Block 0x3fcc69bc data, size: 89424 bytes, Free: Yes"
  unsigned addr = 0, size = 0;
  char freeWord[4] = {0};
  if (sscanf(g_line, "Block 0x%x data, size: %u bytes, Free: %3s", &addr, &size, freeWord) == 3) {
    if (g_recs && g_recCount < kMaxRecs) {
      g_recs[g_recCount++] = {addr, size, freeWord[0] == 'Y'};
    } else {
      g_overflowed = true;
    }
  }
}
}  // namespace

void dump() {
  auto recBuf = makeUniqueNoThrow<BlockRec[]>(kMaxRecs);
  if (!recBuf) {
    LOG_ERR("MEM", "heap map skipped: no room for capture buffer");
    return;
  }
  g_recs = recBuf.get();
  g_recCount = 0;
  g_overflowed = false;
  g_lineLen = 0;
  // Capture (interrupts masked inside the dump): parse into records, never
  // wait. Log the table afterward with the system live. NOTE: the capture
  // buffer itself appears in the map as a used block of ~17.4 KB — it frees
  // on return (observer effect, do not chase it as a leak/splitter).
  ets_install_putc1(&captInterpolatePutc);
  heap_caps_dump(MALLOC_CAP_8BIT);
  ets_install_uart_printf();
  g_recs = nullptr;

  LOG_DBG("MEM", "---- heap block map: %u blocks%s ----", g_recCount, g_overflowed ? " (TRUNCATED)" : "");
  uint32_t dustCount = 0, dustBytes = 0;
  for (uint16_t i = 0; i < g_recCount; ++i) {
    const auto& r = recBuf[i];
    if (r.free || r.size >= 256) {
      LOG_DBG("MEM", "%s 0x%08x %u", r.free ? "FREE" : "used", r.addr, r.size);
    } else {
      dustCount++;
      dustBytes += r.size;
    }
  }
  LOG_DBG("MEM", "dust: %u used blocks < 256B totaling %u bytes", dustCount, dustBytes);
  LOG_DBG("MEM", "---- end heap block map ----");
}
}  // namespace heapmap
