#pragma once

// MEMFIX-PORT: heap block map (on-demand via CMD:MEMMAP + reader one-shot); portable, no BLE dependency
namespace heapmap {
// Capture-and-log the DRAM heap block map (address/size/free per block,
// sub-256B used blocks rolled up as "dust"). Safe to call from the main loop;
// ~60-100 LOG_DBG lines. See HeapMap.cpp for why capture-then-log is the only
// shape that works (heap_caps_dump runs with interrupts masked).
void dump();
}  // namespace heapmap
