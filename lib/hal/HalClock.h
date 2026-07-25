#pragma once

#include <Arduino.h>
#include <Rtc.h>

#include <cstdint>
#include <ctime>

enum class ClockSyncState : uint8_t {
  Idle,
  Syncing,
  Succeeded,
  Failed,
};

class HalClock;
extern HalClock halClock;

class HalClock {
  Rtc _sdkRtc;
  bool _rtcAvailable = false;
  bool _autoSyncEnabled = true;
  bool _wifiWasConnected = false;
  bool _sntpInitialized = false;
  ClockSyncState _syncState = ClockSyncState::Idle;
  unsigned long _lastSyncMs = 0;

  bool restoreSystemTimeFromRtc();
  bool updateRtcFromSystemTime();
  bool startSntp();
  void stopSntp();
  void completeSync();

 public:
  // The POSIX UTC system clock is the runtime source on every device. An
  // external RTC, when present, is used only to restore time after power loss.
  void begin();
  void update();

  time_t nowUtc() const;
  bool hasValidTime() const;
  bool setUtcTime(time_t epoch);

  void setAutoSyncEnabled(bool enabled);
  bool requestSync();
  bool syncNow(uint32_t timeoutMs = 10000);
  ClockSyncState syncState() const { return _syncState; }
};
