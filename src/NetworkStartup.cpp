#include "NetworkStartup.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <WiFi.h>

#if !defined(SIMULATOR)
#include <esp_heap_caps.h>
#include <sdkconfig.h>
#endif

#include "BleInput.h"
#include "SdCardFontSystem.h"
#include "activities/RenderLock.h"

namespace {

constexpr size_t MIN_FREE_PSRAM = 256 * 1024;
constexpr size_t MIN_FREE_INTERNAL = 64 * 1024;
constexpr size_t MIN_LARGEST_INTERNAL_BLOCK = 32 * 1024;

struct MemorySnapshot {
  size_t freePsram;
  size_t freeInternal;
  size_t largestInternalBlock;
};

constexpr bool shouldReleaseRenderMemory(const MemorySnapshot& memory) {
  return memory.freePsram < MIN_FREE_PSRAM || memory.freeInternal < MIN_FREE_INTERNAL ||
         memory.largestInternalBlock < MIN_LARGEST_INTERNAL_BLOCK;
}

static_assert(shouldReleaseRenderMemory({0, MIN_FREE_INTERNAL, MIN_LARGEST_INTERNAL_BLOCK}));
static_assert(shouldReleaseRenderMemory({MIN_FREE_PSRAM - 1, MIN_FREE_INTERNAL, MIN_LARGEST_INTERNAL_BLOCK}));
static_assert(shouldReleaseRenderMemory({MIN_FREE_PSRAM, MIN_FREE_INTERNAL - 1, MIN_LARGEST_INTERNAL_BLOCK}));
static_assert(shouldReleaseRenderMemory({MIN_FREE_PSRAM, MIN_FREE_INTERNAL, MIN_LARGEST_INTERNAL_BLOCK - 1}));
static_assert(!shouldReleaseRenderMemory({MIN_FREE_PSRAM, MIN_FREE_INTERNAL, MIN_LARGEST_INTERNAL_BLOCK}));

MemorySnapshot readMemorySnapshot() {
#if defined(SIMULATOR)
  return {MIN_FREE_PSRAM, MIN_FREE_INTERNAL, MIN_LARGEST_INTERNAL_BLOCK};
#elif defined(BOARD_HAS_PSRAM) && defined(CONFIG_SPIRAM_USE_MALLOC) && CONFIG_SPIRAM_USE_MALLOC
  constexpr uint32_t INTERNAL_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  return {heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_free_size(INTERNAL_CAPS),
          heap_caps_get_largest_free_block(INTERNAL_CAPS)};
#else
  return {};
#endif
}

}  // namespace

namespace NetworkStartup {

void prepare(GfxRenderer& renderer) {
  const MemorySnapshot before = readMemorySnapshot();
  if (!shouldReleaseRenderMemory(before)) return;

  {
    RenderLock lock;
    sdFontSystem.releaseLoadedFont(renderer);
    if (auto* fontCache = renderer.getFontCacheManager()) fontCache->clearCache();
  }

  // Releasing only helps WiFi if it actually enlarges the largest allocatable
  // internal block -- free bytes alone are not what the radio needs. On Read Pico
  // the SD family's tables live in PSRAM, so dropping it changes freeInternal but
  // leaves largestInternalBlock at its 31,732-byte ceiling, meaning the release buys
  // nothing while costing the UI the glyph fallback it draws CJK with (WiFi and
  // AirPage then render in the built-in faces). Put the family back when the release
  // did not move the block the gate is actually about; targets where it does help
  // keep the old behaviour because the comparison fails.
  const MemorySnapshot after = readMemorySnapshot();
  LOG_DBG("NET", "Released render memory for the network: largest internal block %u -> %u",
          static_cast<unsigned>(before.largestInternalBlock), static_cast<unsigned>(after.largestInternalBlock));
  if (after.largestInternalBlock <= before.largestInternalBlock) {
    sdFontSystem.ensureLoaded(renderer);
  }
}

bool setMode(GfxRenderer& renderer, const wifi_mode_t mode) {
  bleinput::stop();
  prepare(renderer);
  return WiFi.mode(mode);
}

}  // namespace NetworkStartup
