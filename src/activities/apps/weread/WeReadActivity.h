#pragma once

#include <atomic>
#include <cstdint>

#include "../../Activity.h"
#include "WeReadClient.h"
#include "WeReadStore.h"
#include "util/ButtonNavigator.h"

class WeReadActivity final : public Activity {
 public:
  WeReadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput) : Activity("WeRead", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class State : uint8_t {
    Menu,
    Shelf,
    Connecting,
    Qr,
    LoginConfirmed,
    Syncing,
    Downloading,
    Cancelling,
    OpenBook,
    Error,
    LogoutError
  };
  enum class Job : uint8_t { Sync, Download };

  ButtonNavigator buttonNavigator_;
  WeReadClient::Operation operation_;
  mutable HalFile shelfFile_;
  std::atomic<State> state_{State::Menu};
  std::atomic<uint32_t> completedChapters_{0};
  std::atomic<uint32_t> chapterCount_{0};
  WeReadClient::Error error_ = WeReadClient::Error::Ok;
  WeReadStore::ShelfRecord pendingBook_;
  char qrUrl_[256] = {};
  uint32_t shelfCount_ = 0;
  int menuSelected_ = 0;
  int shelfSelected_ = 0;
  Job retryJob_ = Job::Sync;
  std::atomic<bool> downloadRenderPending_{false};

  bool refreshShelf();
  bool readShelf(int index, WeReadStore::ShelfRecord& record) const;
  void requestDownloadUpdate();
  void requestJobUpdate();
  void startJob(Job job, const WeReadStore::ShelfRecord* book = nullptr);
  void connectThen(Job job, const WeReadStore::ShelfRecord* book = nullptr);
  void activateSelected();
  void advanceJob();
  void openBook(const char* path);
  void openShelf();
  void syncShelf();
  void promptLogout();
  void performLogout();
  void handleMenuInput();
  void handleShelfInput();
  void handleErrorInput();
  void handleLogoutErrorInput();
  const char* errorMessage() const;
  static bool isBusy(State state);
};
