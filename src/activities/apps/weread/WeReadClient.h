#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "WeReadHttpClient.h"
#include "WeReadProtocol.h"
#include "WeReadStore.h"

namespace WeReadClient {

struct OperationTestPeer;

enum class Error {
  Ok,
  Cancelled,
  Network,
  SessionExpired,
  LoginFailed,
  Protocol,
  SdCard,
  Integrity,
  Unavailable,
  Clock,
  OutOfMemory,
  WholeBookOnly,
};

struct DownloadOptions {
  enum class ChapterScope : uint8_t {
    WholeBook,
    SelectRange,
  };

  WeReadStore::ImagePolicy imagePolicy = WeReadStore::ImagePolicy::Embed;
  ChapterScope chapterScope = ChapterScope::WholeBook;
};

struct ProgressSyncInput {
  float localFraction = 0.0f;
  uint32_t localTocIndex = 0;
  uint16_t localSpineIndex = 0;
  uint16_t localPageNumber = 0;
  uint16_t localPageCount = 0;
  bool hasLocalTocIndex = false;
};
static_assert(sizeof(ProgressSyncInput) == 16);

enum class ProgressSyncMode : uint8_t {
  Compare,
  ApplyRemote,
  UploadLocal,
};

enum class ProgressSyncOutcome : uint8_t {
  Pending,
  AlreadySynced,
  SelectionRequired,
  ApplyRemote,
  LocalUploaded,
};

struct ProgressSyncResult {
  ProgressSyncOutcome outcome = ProgressSyncOutcome::Pending;
  WeReadProtocol::RemoteProgress remote;
};

class Operation {
 public:
  enum class Kind : uint8_t { Sync, Detail, Download, ProgressSync };
  enum class Event : uint8_t {
    None,
    QrReady,
    Authenticated,
    DetailReady,
    ChapterRangeReady,
    ChapterComplete,
    Complete,
    Cancelled,
    Failed
  };
  enum class ProgressStage : uint8_t { Chapters, Images, Packaging };

  bool begin(Kind kind, const WeReadStore::ShelfRecord* book = nullptr, DownloadOptions options = {});
  bool beginProgressSync(const char* bookId, ProgressSyncInput input, ProgressSyncMode mode);
  Event step();
  void cancel();
  void reset();
  bool readChapter(uint32_t index, WeReadStore::TocRecord& record);
  bool setChapterRange(uint32_t first, uint32_t last);

  Error error() const { return error_; }
  uint32_t chapterCount() const { return chapterCount_; }
  ProgressStage progressStage() const { return progressStage_; }
  uint32_t progressCompleted() const { return progressCompleted_; }
  uint32_t progressTotal() const { return progressTotal_; }
  static constexpr uint8_t progressDecile(const uint32_t completed, const uint32_t total) {
    return total == 0 ? 0 : static_cast<uint8_t>((static_cast<uint64_t>(completed) * 10) / total);
  }
  const char* qrUrl() const { return url_; }
  const char* finalPath() const { return outputPath_.c_str(); }
  const ProgressSyncResult& progressSyncResult() const { return progressSyncResult_; }
  bool active() const;

 private:
  friend struct OperationTestPeer;

  enum class ProgressAction : uint8_t {
    AlreadySynced,
    SelectDirection,
    ApplyRemote,
    UploadLocal,
  };

  enum class Phase : uint8_t {
    Idle,
    LoginUid,
    LoginPollWait,
    LoginPoll,
    SyncShelf,
    Renew,
    PrepareDetail,
    FetchDetail,
    FetchCover,
    ConvertCover,
    PrepareDownload,
    FetchToc,
    PrepareProgressSync,
    FetchProgress,
    DecideProgress,
    FetchProgressReader,
    SendProgressEnter,
    SendProgressReport,
    VerifyProgress,
    OpenToc,
    AwaitChapterRange,
    LoadChapter,
    SyncClock,
    FetchReader,
    FetchPrimary,
    FetchText0,
    FetchText1,
    FetchEpub1,
    FetchEpub3,
    DecodeText,
    DecodeEpub,
    AdvanceChapter,
    PrepareImages,
    DownloadImages,
    PackageBook,
    Complete,
    Cancelled,
    Failed,
  };

  static constexpr size_t kCookieSize = 896;
  // Reader pages currently include response headers larger than 2 KB.
  static constexpr size_t kIoBufferSize = 4096;
  static constexpr size_t kUrlSize = 512;
  static constexpr uint8_t kMaxRequestAttempts = 3;
  static constexpr uint8_t kMaxImageRedirects = 5;
  static constexpr Event chapterResponseRetryEvent(const uint8_t attempts) {
    return attempts >= kMaxRequestAttempts ? Event::Failed : Event::None;
  }
  static constexpr Event detailCompletionEvent(const bool coverPending) {
    return coverPending ? Event::DetailReady : Event::Complete;
  }
  static constexpr bool detailCoverPending(const bool hasBmp, const bool hasSource, const bool hasUrl) {
    return hasUrl && (!hasBmp || !hasSource);
  }
  static constexpr Phase chapterResponseRetryPhase() { return Phase::FetchReader; }
  static constexpr bool shouldRetryPaidPreview(const bool paid, const bool plainText, const bool hasXhtmlTag) {
    return paid && !plainText && !hasXhtmlTag;
  }
  static constexpr bool imageAttemptPending(const uint8_t attempts) { return attempts < 2; }
  static constexpr bool imageRedirectAllowed(const uint8_t redirects) { return redirects < kMaxImageRedirects; }
  static constexpr bool validChapterRange(const uint32_t first, const uint32_t last, const uint32_t count) {
    return count > 0 && first <= last && last < count;
  }
  static constexpr uint32_t chapterRangeCount(const uint32_t first, const uint32_t last, const uint32_t count) {
    return validChapterRange(first, last, count) ? last - first + 1 : 0;
  }
  static constexpr bool wholeChapterRange(const uint32_t first, const uint32_t last, const uint32_t count) {
    return validChapterRange(first, last, count) && first == 0 && last == count - 1;
  }
  static constexpr ProgressAction progressAction(const ProgressSyncMode mode, const bool samePosition) {
    if (samePosition) return ProgressAction::AlreadySynced;
    switch (mode) {
      case ProgressSyncMode::Compare:
        return ProgressAction::SelectDirection;
      case ProgressSyncMode::ApplyRemote:
        return ProgressAction::ApplyRemote;
      case ProgressSyncMode::UploadLocal:
        return ProgressAction::UploadLocal;
    }
    return ProgressAction::SelectDirection;
  }
  static constexpr ProgressSyncOutcome progressVerification(const bool samePosition, const bool remoteHasAppId,
                                                            const bool sameAppId, const bool remoteHasUpdateTime,
                                                            const uint32_t remoteUpdateTime,
                                                            const uint32_t uploadStartedAt) {
    const bool fresh = remoteHasUpdateTime && remoteUpdateTime >= uploadStartedAt;
    if (samePosition && fresh && remoteHasAppId) {
      return sameAppId ? ProgressSyncOutcome::LocalUploaded : ProgressSyncOutcome::AlreadySynced;
    }
    if (!samePosition && fresh && remoteHasAppId && !sameAppId) {
      return ProgressSyncOutcome::SelectionRequired;
    }
    return ProgressSyncOutcome::Pending;
  }

  void startLogin(Phase resume);
  void requestAuthentication(Phase resume);
  Event fail(Error error);
  Event handleRequestError(Error error, Phase retryPhase);
  Event retryChapterResponse();
  Event reauthenticateChapter();
  void requestSucceeded();
  void guardBookSession(const char* phase);
  bool preparePaths();
  bool waitForShardPace();
  Error fetchLoginUid();
  Error pollLogin();
  Error renewSession();
  Error syncShelfOnce();
  Error fetchDetailOnce();
  Event fetchCover();
  Event convertCover();
  Error fetchTocOnce();
  Error fetchProgressOnce(bool bypassCache);
  Error fetchProgressReaderOnce();
  Error sendProgressOnce(bool report);
  float normalizedRemoteProgress() const;
  bool sameRemotePosition() const;
  bool remoteAppIdMatchesLocal() const;
  void persistInitialProgress();
  Error decideProgress();
  Error fetchReaderOnce();
  Error fetchShardOnce(const char* endpoint, const std::string& destination);
  Event inspectPrimary();
  Event decodeChapter(bool plainText);
  Event finishWholeBook(const std::string& source);
  Event cancelNow();
  Error prepareImageWork();
  Event downloadNextImage();
  Error requestImage(WeReadStore::ImageRecord& image, WeReadStore::ImageWorkState& state, uint8_t& attempts,
                     uint8_t& redirects, bool trackProgress);

  Phase phase_ = Phase::Idle;
  Phase resumePhase_ = Phase::Idle;
  Kind kind_ = Kind::Sync;
  Error error_ = Error::Ok;
  ProgressStage progressStage_ = ProgressStage::Chapters;
  DownloadOptions options_;
  ProgressSyncInput progressSyncInput_;
  ProgressSyncMode progressSyncMode_ = ProgressSyncMode::Compare;
  ProgressSyncResult progressSyncResult_;
  WeReadStore::Session session_;
  WeReadStore::ShelfRecord book_;
  WeReadStore::TocRecord chapter_;
  WeReadHttpClient::Session bookSession_;
  HalFile tocFile_;
  uint32_t chapterCount_ = 0;
  uint32_t firstChapterIndex_ = 0;
  uint32_t lastChapterIndex_ = 0;
  uint32_t chapterIndex_ = 0;
  uint32_t progressCompleted_ = 0;
  uint32_t progressTotal_ = 0;
  uint32_t progressChapterOffset_ = 0;
  uint32_t imageWorkCount_ = 0;
  uint32_t imageWorkCursor_ = 0;
  uint32_t imageDownloaded_ = 0;
  uint32_t imageCached_ = 0;
  uint32_t imageSkipped_ = 0;
  uint32_t imageRedirects_ = 0;
  uint32_t imageFilesCreated_ = 0;
  uint64_t imageBytes_ = 0;
  uint8_t requestAttempt_ = 0;
  uint8_t progressVerifyAttempts_ = 0;
  uint8_t chapterResponseAttempts_ = 0;
  uint8_t coverAttempts_ = 0;
  uint8_t coverRedirects_ = 0;
  WeReadStore::ImageWorkState coverState_ = WeReadStore::ImageWorkState::Pending;
  bool cancelRequested_ = false;
  bool renewalAttempted_ = false;
  bool loginRecoveryAttempted_ = false;
  bool loginConfirmed_ = false;
  unsigned long loginStartedAt_ = 0;
  unsigned long nextActionAt_ = 0;
  unsigned long lastShardRequestAt_ = 0;
  unsigned long imagePhaseStartedAt_ = 0;
  int responseStatus_ = 0;
  uint32_t progressUploadStartedAt_ = 0;
  char previousVid_[64] = {};
  char loginUid_[128] = {};
  char psvts_[128] = {};
  float initialProgressFraction_ = 0.0f;
  bool initialProgressValid_ = false;
  char imageHost_[128] = {};
  WeReadProtocol::ImageType coverType_ = WeReadProtocol::ImageType::None;
  char cookie_[kCookieSize] = {};
  char url_[kUrlSize] = {};
  uint8_t ioBuffer_[kIoBufferSize] = {};
  std::string referer_;
  std::string bookDir_;
  std::string tocPath_;
  std::string outputPath_;
  std::string finalPartPath_;
};

}  // namespace WeReadClient
