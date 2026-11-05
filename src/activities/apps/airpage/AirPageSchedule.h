#pragma once
#include <cstdint>
namespace airpage {
constexpr uint16_t kSyncMinutes[] = {0, 5, 15, 30, 60, 120, 360};
constexpr bool syncDue(uint32_t now, uint32_t last, uint16_t minutes) {
  return minutes != 0 && now - last >= static_cast<uint32_t>(minutes) * 60000u;
}
}  // namespace airpage
