#include "NetworkStartup.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <WiFi.h>

#include "SdCardFontSystem.h"
#include "activities/RenderLock.h"

namespace NetworkStartup {

void prepare(GfxRenderer& renderer) {
  RenderLock lock;
  sdFontSystem.releaseLoadedFont(renderer);
  if (auto* fontCache = renderer.getFontCacheManager()) fontCache->clearCache();
}

bool setMode(GfxRenderer& renderer, const wifi_mode_t mode) {
  prepare(renderer);
  return WiFi.mode(mode);
}

}  // namespace NetworkStartup
