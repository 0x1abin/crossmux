#pragma once

#include <cstdint>
#include <memory>

#include "../../Activity.h"
#include "StandbyFace.h"

struct ActivityResult;

class StandbyActivity final : public Activity {
 public:
  explicit StandbyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Standby", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;

  // Standby is a clock — keep the framework's deep-sleep timer (main.cpp:422,
  // default 10 min) paused for the entire lifetime of this activity. Exiting
  // back to Apps restores the default sleep behaviour. Tight-loop polling is
  // still only used during WiFi/NTP sync.
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return syncState_ != SyncState::Idle; }

 private:
  enum class SyncState : uint8_t {
    Idle,            // No sync running (either succeeded or skipped/failed)
    WifiConnecting,  // WiFi.begin() called, polling WiFi.status()
    NtpSyncing,      // SNTP started, polling sntp_get_sync_status()
  };

  enum class DisplayMode : uint8_t {
    Normal,     // Header + battery + face dots + face content
    Immersive,  // Face content only (after 5s idle). On battery the framework
                // engages CPU low-freq after 3s idle and full deep sleep after
                // SETTINGS.getSleepTimeoutMs() — Standby just needs to stay out
                // of the way.
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
  void beginNtpSync();
  void pumpTimeSync();
  void finishTimeSync();

  // Layer a 4-level grayscale refresh on top of the BW image just committed by
  // displayBuffer(): re-render the LSB then MSB planes and composite via the
  // gray LUT waveform. The caller decides when to invoke it — passive faces in
  // Immersive (gated on wantsGrayscale), interactive faces on demand
  // (wantsImmediateGrayscale). The face's render() must be idempotent across
  // the three passes.
  void applyGrayscalePass(int sw, int sh);
};
