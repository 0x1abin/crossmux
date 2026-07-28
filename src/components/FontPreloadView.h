#pragma once

#include <cstddef>
#include <cstdint>

class GfxRenderer;

namespace fontpreload {

enum class State : uint8_t {
  Progress,
  Ready,
};

void draw(const GfxRenderer& renderer, const char* familyName, uint8_t pointSize, size_t completed, size_t total,
          State state);

}  // namespace fontpreload
