#pragma once

#include <atomic>
#include <cstdint>

#include "../../Activity.h"
#include "WeReadClient.h"
#include "WeReadStore.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

struct Rect;

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
    DetailLoading,
    DetailCoverLoading,
    Detail,
    Introduction,
    Downloading,
    Cancelling,
    OpenBook,
    Error,
    LogoutError
  };
  enum class Job : uint8_t { Sync, Detail, Download };
  enum class DetailAction : uint8_t { Introduction, Read, Cache, Images };
  static constexpr int kDetailActionCount = 4;
  static constexpr int kDetailListActionCount = kDetailActionCount - 1;
  static constexpr int kMaxIntroPages = 128;

  ButtonNavigator buttonNavigator_;
  OptionPopup cacheScopePopup_;
  WeReadClient::Operation operation_;
  mutable HalFile shelfFile_;
  std::atomic<State> state_{State::Menu};
  std::atomic<WeReadClient::Operation::ProgressStage> progressStage_{WeReadClient::Operation::ProgressStage::Chapters};
  std::atomic<uint32_t> progressCompleted_{0};
  std::atomic<uint32_t> progressTotal_{0};
  WeReadClient::Error error_ = WeReadClient::Error::Ok;
  WeReadStore::ShelfRecord pendingBook_;
  char qrUrl_[256] = {};
  uint32_t shelfCount_ = 0;
  int menuSelected_ = 0;
  int shelfSelected_ = 0;
  int shelfCoverPageStart_ = -1;
  int shelfCoverCursor_ = 0;
  int detailSelected_ = 0;
  int introPage_ = 0;
  int introPageCount_ = 1;
  Job retryJob_ = Job::Sync;
  WeReadStore::BookDetailHeader detail_;
  uint32_t introPageOffsets_[kMaxIntroPages + 1] = {};
  WeReadStore::ImagePolicy detailImagePolicy_ = WeReadStore::ImagePolicy::Embed;
  WeReadStore::ImagePolicy detailSavedImagePolicy_ = WeReadStore::ImagePolicy::Embed;
  WeReadClient::DownloadOptions::ChapterScope downloadChapterScope_ =
      WeReadClient::DownloadOptions::ChapterScope::WholeBook;
  bool detailLoaded_ = false;
  bool detailLoadFailed_ = false;
  bool detailOptionsKnown_ = false;
  bool detailIntroTruncated_ = false;
  bool introPagesTruncated_ = false;
  bool shelfCoverStopped_ = false;
  bool cacheScopePopupClosing_ = false;
  std::atomic<bool> downloadRenderPending_{false};
  std::atomic<bool> stageRenderPending_{false};

  bool refreshShelf();
  bool readShelf(int index, WeReadStore::ShelfRecord& record) const;
  Rect contentBounds() const;
  int shelfItemsPerPage() const;
  void resetShelfCoverLoading();
  void advanceShelfCovers();
  WeReadClient::Operation::Event stepOperation();
  void requestDownloadUpdate();
  void requestJobUpdate();
  void startJob(Job job, const WeReadStore::ShelfRecord* book = nullptr);
  void connectThen(Job job, const WeReadStore::ShelfRecord* book = nullptr);
  void activateSelected();
  void openSelectedDetail(const WeReadStore::ShelfRecord& book);
  void loadSelectedDetail(bool preserveUi = false);
  bool detailActionEnabled(DetailAction action) const;
  void moveDetailSelection(int direction);
  void activateDetailSelection();
  void showCacheScopePopup();
  void startBookDownload();
  void selectChapterRange();
  void cancelChapterRangeSelection();
  void failChapterRangeSelection(WeReadClient::Error error);
  void handleDetailInput();
  void handleIntroductionInput();
  void buildIntroductionPages();
  bool drawDetailIntroduction(const Rect& bounds, bool selected);
  void drawShelfGrid(const Rect& content);
  void drawBookDetail(const Rect& content, bool coverLoading = false);
  void drawIntroduction(const Rect& content);
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
  static State stateForJob(Job job);
  static bool isBusy(State state);
};
