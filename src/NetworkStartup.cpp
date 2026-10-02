#include "NetworkStartup.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalMemory.h>
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

void logMemory(const char* stage) {
  const auto internal = HalMemory::getInternalHeap();
  const auto dma = HalMemory::getInternalDmaHeap();
  const auto psram = HalMemory::getPsramHeap();
  LOG_INF("NET", "%s: internal free=%u largest=%u, DMA free=%u largest=%u, PSRAM free=%u", stage,
          static_cast<unsigned>(internal.freeBytes), static_cast<unsigned>(internal.largestBlockBytes),
          static_cast<unsigned>(dma.freeBytes), static_cast<unsigned>(dma.largestBlockBytes),
          static_cast<unsigned>(psram.freeBytes));
}

void prepare(GfxRenderer& renderer) {
  logMemory("before font reclaim");
  const MemorySnapshot before = readMemorySnapshot();
  if (!shouldReleaseRenderMemory(before)) return;

  {
    RenderLock lock;
    sdFontSystem.releaseLoadedFont(renderer);
    if (auto* fontCache = renderer.getFontCacheManager()) fontCache->clearCache();
  }
  logMemory("after font reclaim");
}

bool setMode(GfxRenderer& renderer, const wifi_mode_t mode) {
  bleinput::stop();
  logMemory("after BLE stop");
  prepare(renderer);
  const bool started = WiFi.mode(mode);
  logMemory(started ? "WiFi mode ready" : "WiFi mode failed");
  return started;
}

}  // namespace NetworkStartup
