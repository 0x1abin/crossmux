#pragma once

#include <BleKeyboardHost.h>
#include <BlePageTurner.h>
#include <BoardConfig.h>

#include <cstddef>
#include <cstdint>

class GfxRenderer;

namespace bleinput {

enum class StartContext : uint8_t { Reader, Explicit };
enum class StartResult : uint8_t { Started, AlreadyRunning, LowMemory, Unavailable, Failed };

#if FREEINK_DEVICE_READPICO
// Read Pico holds a large block of CONTIGUOUS INTERNAL DMA for its panel (epdiy's
// LUTs, line queues and bounce buffers, ~45 KB; see platformio.ini's A3 note) on
// top of CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768, and its BLE host allocates
// from PSRAM anyway (CROSSPOINT_BLE_HOST_PSRAM, via [s3_ble_psram]). The generic
// reader gate below (80 KB free / 32 KB largest internal) was not sized for a board
// that deliberately spends internal RAM that way, so it rejects a start this board
// can afford and BLE never comes up inside the reader.
//
// These floors still leave room for NimBLE's task stacks (which must be internal)
// and its remaining internal bookkeeping.
inline constexpr size_t kReaderMinFreeInternal = 56 * 1024;
inline constexpr size_t kReaderMinLargestInternal = 20 * 1024;
#else
inline constexpr size_t kReaderMinFreeInternal = 80 * 1024;
inline constexpr size_t kReaderMinLargestInternal = 32 * 1024;
#endif
inline constexpr size_t kExplicitMinFreeInternal = 70 * 1024;
inline constexpr size_t kExplicitMinLargestInternal = 24 * 1024;
inline constexpr size_t kMinFreePsram = 256 * 1024;
inline constexpr size_t kMinLargestPsram = 32 * 1024;

// The module owns lifecycle and cache-release locking; caller must not hold RenderLock.
StartResult ensureStarted(GfxRenderer& renderer, StartContext context);
bool stop();
bleturner::Why validateStart(bool explicitStart, bool afterStart);
bleturner::Action legacyKeyAction(uint8_t usage, uint8_t mods);
void logDiagnostics(const char* phase);
bool encodeKey(const freeink::KeyEvent& event, uint8_t& kind, uint8_t& value);
void describeKey(uint8_t kind, uint8_t value, char* out, size_t outLen);

inline bool isRunning() { return bleturner::status().running; }
inline bool isConnected() { return bleturner::status().connected; }

}  // namespace bleinput
