#pragma once

#include <cstdint>

// Debounces save requests. After schedule(now), consumeIfDue(now) returns
// true exactly once after the debounce window elapses; the activity then
// performs its game-specific save.
struct GameSaveDebouncer {
  static constexpr uint32_t kWindowMs = 1500;

  uint32_t pendingAtMs = 0;

  void schedule(uint32_t now) { pendingAtMs = now + kWindowMs; }
  void clear() { pendingAtMs = 0; }
  bool consumeIfDue(uint32_t now) {
    if (pendingAtMs == 0 || now < pendingAtMs) return false;
    pendingAtMs = 0;
    return true;
  }
};
