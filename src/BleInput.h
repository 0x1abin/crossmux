#pragma once

#include <BleKeyboardHost.h>
#include <BoardConfig.h>

#include <cstddef>
#include <cstdint>

class GfxRenderer;

namespace bleinput {

enum class StartContext : uint8_t { Reader, Explicit };
enum class StartResult : uint8_t { Started, AlreadyRunning, LowMemory, Unavailable, Failed };

inline constexpr size_t kReaderMinFreeInternal = 80 * 1024;
inline constexpr size_t kReaderMinLargestInternal = 32 * 1024;
inline constexpr size_t kExplicitMinFreeInternal = 70 * 1024;
inline constexpr size_t kExplicitMinLargestInternal = 24 * 1024;
inline constexpr size_t kMinFreePsram = 256 * 1024;
inline constexpr size_t kMinLargestPsram = 32 * 1024;

// Caller holds RenderLock across lifecycle checks, memory recovery and startup.
StartResult ensureStarted(GfxRenderer& renderer, StartContext context);
void stop();
void logDiagnostics(const char* phase);
bool encodeKey(const freeink::KeyEvent& event, uint8_t& kind, uint8_t& value);
void describeKey(uint8_t kind, uint8_t value, char* out, size_t outLen);

#if FREEINK_CAP_BLE_HID_HOST
inline bool isRunning() { return BleHid.isRunning(); }
inline bool isConnected() { return BleHid.isConnected(); }
#else
inline bool isRunning() { return false; }
inline bool isConnected() { return false; }
#endif

}  // namespace bleinput
