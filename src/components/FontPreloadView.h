#pragma once

#include <I18n.h>
#include <SdCardFontCache.h>

#include <cstddef>
#include <cstdint>

class GfxRenderer;

namespace fontpreload {

constexpr unsigned long NOTICE_DURATION_MS = 2000;
StrId failureMessage(SdCardFontCache::Result result);
void drawTooLargeNotice(const GfxRenderer& renderer);

enum class State : uint8_t {
  Progress,
  Ready,
};

void draw(const GfxRenderer& renderer, const char* familyName, uint8_t pointSize, size_t completed, size_t total,
          State state);

}  // namespace fontpreload
