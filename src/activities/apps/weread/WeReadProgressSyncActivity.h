#pragma once

#include <cstdint>
#include <string>

#include "WeReadClient.h"
#include "activities/Activity.h"

class WeReadProgressSyncActivity final : public Activity {
 public:
  WeReadProgressSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string epubPath,
                             const char* bookId, float localFraction, int currentSpineIndex, int currentPageCount);

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
    Success,
    Failed,
    LoginRequired,
  };

  State state_ = State::WifiSelection;
  WeReadClient::Operation operation_;
  WeReadClient::Error error_ = WeReadClient::Error::Ok;
  WeReadClient::ProgressSyncOutcome outcome_ = WeReadClient::ProgressSyncOutcome::None;
  std::string epubPath_;
  char bookId_[64] = {};
  float localFraction_ = 0.0f;
  int currentSpineIndex_ = 0;
  int currentPageCount_ = 0;
  bool wifiActivated_ = false;

  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void startSync();
  void advanceSync();
  void applyRemoteProgress(float fraction);
  void returnToReader();
  const char* resultMessage() const;
  const char* errorMessage() const;
};
