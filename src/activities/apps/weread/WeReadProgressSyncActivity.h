#pragma once

#include <cstdint>
#include <string>

#include "WeReadClient.h"
#include "activities/Activity.h"

class WeReadProgressSyncActivity final : public Activity {
 public:
  WeReadProgressSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string epubPath,
                             const char* bookId, WeReadClient::ProgressSyncInput input);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class State : uint8_t {
    WifiSelection,
    Starting,
    Syncing,
    ChoosingDirection,
    Success,
    Failed,
    LoginRequired,
  };

  enum class DirectionOption : uint8_t {
    ApplyRemote,
    UploadLocal,
  };

  State state_ = State::WifiSelection;
  WeReadClient::Operation operation_;
  WeReadClient::Error error_ = WeReadClient::Error::Ok;
  WeReadClient::ProgressSyncMode syncMode_ = WeReadClient::ProgressSyncMode::Compare;
  WeReadClient::ProgressSyncOutcome outcome_ = WeReadClient::ProgressSyncOutcome::Pending;
  DirectionOption selectedDirection_ = DirectionOption::ApplyRemote;
  std::string epubPath_;
  char bookId_[64] = {};
  WeReadClient::ProgressSyncInput input_;
  float remoteFraction_ = 0.0f;
  bool uploadConflict_ = false;
  bool wifiActivated_ = false;

  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void startSync();
  void advanceSync();
  void beginSelectedDirection();
  void applyRemoteProgress(const WeReadProtocol::RemoteProgress& remote);
  void returnToReader();
  const char* resultMessage() const;
  const char* errorMessage() const;
};
