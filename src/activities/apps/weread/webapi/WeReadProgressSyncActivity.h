#pragma once

#include <cstdint>
#include <string>

#include "../WeReadBackend.h"
#include "../WeReadProgressContext.h"
#include "activities/Activity.h"

class Epub;
struct CrossPointPosition;

class WeReadProgressSyncActivity final : public Activity {
 public:
  WeReadProgressSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string epubPath,
                             const char* bookId, const WeReadProgressContext& context);

  static WeReadProgressContext makeContext(const Epub& epub, const char* bookId, float localFraction,
                                           const CrossPointPosition& localPosition);

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
  uint16_t localSpineIndex_ = 0;
  uint16_t localPageNumber_ = 0;
  uint16_t localPageCount_ = 0;
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
