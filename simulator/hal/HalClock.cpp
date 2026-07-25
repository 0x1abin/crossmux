#include <HalClock.h>

HalClock halClock;

namespace {
time_t hostClockOffset = 0;
}

void HalClock::begin() {}

void HalClock::update() {}

time_t HalClock::nowUtc() const {
  const time_t now = time(nullptr) + hostClockOffset;
  return now >= 1704016800 ? now : 0;
}

bool HalClock::hasValidTime() const { return nowUtc() != 0; }

bool HalClock::setUtcTime(const time_t epoch) {
  if (epoch < 1704016800) return false;
  hostClockOffset = epoch - time(nullptr);
  _syncState = ClockSyncState::Idle;
  return true;
}

void HalClock::setAutoSyncEnabled(const bool enabled) { _autoSyncEnabled = enabled; }

bool HalClock::requestSync() {
  hostClockOffset = 0;
  _syncState = hasValidTime() ? ClockSyncState::Succeeded : ClockSyncState::Failed;
  return _syncState == ClockSyncState::Succeeded;
}

bool HalClock::syncNow(const uint32_t /*timeoutMs*/) { return requestSync(); }
