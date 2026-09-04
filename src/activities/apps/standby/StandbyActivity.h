#pragma once

#include <cstdint>
#include <memory>

#include "StandbyFace.h"
#include "activities/Activity.h"

struct ActivityResult;

class StandbyActivity final : public Activity {
 public:
  explicit StandbyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Standby", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;

  // Standby manages its own timer/GPIO light-sleep cycles, so the framework's
  // generic deep-sleep timer stays paused for the activity's lifetime.
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override {
    return syncState_ == SyncState::WifiConnecting || syncState_ == SyncState::ClockSyncing;
  }

 private:
  enum class SyncState : uint8_t {
    Idle,
    Delayed,
    WifiConnecting,
    ClockSyncing,
  };

  enum class DisplayMode : uint8_t {
    Normal,     // Header + battery + face dots + face content
    Immersive,  // Face content only; supported boards light-sleep after 35s idle.
  };

  std::unique_ptr<StandbyFace> currentFace_;
  uint8_t faceIndex_ = 0;
  SyncState syncState_ = SyncState::Idle;
  uint32_t syncStartMs_ = 0;
  DisplayMode mode_ = DisplayMode::Normal;
  uint32_t lastInputMs_ = 0;
  bool inverseMode_ = false;  // Confirm toggles black-bg/white-content. Not persisted.

  void switchFace(int8_t delta);
  void startTimeSync();
  bool trySilentWifiConnect();
  void promptForWifi();
  void onWifiResult(const ActivityResult& result);
  void beginClockSync();
  void pumpTimeSync();
  void stopTimeSyncWifi();
  void completeTimeSync();
  void processFaceTick(bool waitForUpdate);
  bool tryLightSleep(uint32_t idleMs);

  // Layer a 4-level grayscale refresh on top of the BW image just committed by
  // displayBuffer(): re-render the LSB then MSB planes and composite via the
  // gray LUT waveform. Used by passive faces in Immersive mode (gated on
  // wantsGrayscale, e.g. the 老黄历 calendar). The face's render() must be
  // idempotent across the three passes.
  void applyGrayscalePass(const Rect& viewport);
};
