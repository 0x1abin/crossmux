#pragma once
#include <CrossPointSettings.h>
#include <GfxRenderer.h>

namespace ReaderUtils {
inline HalDisplay::RefreshMode consumeRefreshMode(int& pagesUntilFullRefresh) {
  const auto mode = (pagesUntilFullRefresh <= 1) ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
  if (pagesUntilFullRefresh <= 1) {
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    pagesUntilFullRefresh--;
  }
  return mode;
}

// One helper, blocking or deferred: the async form starts the refresh and
// returns so the caller can overlap CPU work with the panel's refresh time.
// Async callers must not touch the framebuffer until
// renderer.waitRefreshComplete() and must rebuild the differential baseline
// before the next page turn (the tiled grayscale cleanup does).
inline void displayWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh, bool async = false) {
  const auto mode = consumeRefreshMode(pagesUntilFullRefresh);
  if (async) {
    renderer.displayBufferAsync(mode, DisplayRefreshContext::ContinuousReading);
  } else {
    renderer.displayBuffer(mode, DisplayRefreshContext::ContinuousReading);
  }
}

// Display the B/W base of a page whose grayscale pass follows. Panels that
// combine the base (Paper Mono) defer the activation so base + gray planes go
// out as one waveform — displaying the base separately makes the gray pass
// re-drive the whole text body (a visible flash). Other panels display
// normally. Same refresh-cadence bookkeeping as displayWithRefreshCycle.
inline void displayBaseWithRefreshCycle(const GfxRenderer& renderer, int& pagesUntilFullRefresh) {
  if (!renderer.combinesGrayscaleBase()) {
    displayWithRefreshCycle(renderer, pagesUntilFullRefresh);
    return;
  }
  renderer.displayGrayscaleBase(consumeRefreshMode(pagesUntilFullRefresh), DisplayRefreshContext::ContinuousReading);
}
}  // namespace ReaderUtils
