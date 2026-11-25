#pragma once

#include <cstdint>

struct ReaderRotationContext {
  bool enabled;
  bool pageReady;
  bool renderBusy;
  bool overlay;
  bool gesture;
  uint8_t current;
  uint8_t desired;
};

inline bool readerRotationInputAvailable(const ReaderRotationContext& state) {
  return state.enabled && state.pageReady && !state.renderBusy && !state.overlay && !state.gesture;
}

inline bool readerRotationAllowed(const ReaderRotationContext& state) {
  constexpr uint8_t orientationCount = 4;
  return readerRotationInputAvailable(state) && state.desired < orientationCount && state.desired != state.current;
}
