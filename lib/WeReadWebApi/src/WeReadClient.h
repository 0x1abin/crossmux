#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "WeReadBrowse.h"
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

enum class LocalOffsetBasis : uint8_t {
  None,
  VisibleText,
  RawXhtmlUtf16,
};

struct ProgressSyncInput {
  float localFraction = 0.0f;
  uint32_t localTocIndex = 0;
  uint32_t localOffset = 0;
  LocalOffsetBasis localOffsetBasis = LocalOffsetBasis::None;
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
  enum class Kind : uint8_t { Sync, Detail, Download, ProgressSync, Browse };
  enum class ShelfCoverScope : uint8_t { None, All };
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
  enum class ProgressStage : uint8_t { Chapters, Preparing, Images, Packaging };

  bool begin(Kind kind, const WeReadStore::ShelfRecord* book = nullptr, DownloadOptions options = {},
             ShelfCoverScope shelfCoverScope = ShelfCoverScope::None);
  bool beginProgressSync(const char* bookId, ProgressSyncInput input, ProgressSyncMode mode);
  bool beginBrowseCache(const WeReadStore::BookRecord& book);
  Event step(WeReadStore::WorkCallback callback = nullptr, void* callbackContext = nullptr);
  void cancel();
  void reset();
  bool readChapter(uint32_t index, WeReadStore::TocRecord& record);
  bool setChapterRange(uint32_t first, uint32_t last);

  Error error() const { return error_; }
  uint32_t chapterCount() const { return chapterCount_; }
  ProgressStage progressStage() const { return progressStage_; }
  uint32_t progressCompleted() const { return progressCompleted_; }
  uint32_t progressTotal() const { return progressTotal_; }
  const char* progressTitle() const { return book_.title; }
  static constexpr uint8_t progressDecile(const uint32_t completed, const uint32_t total) {
    return total == 0 ? 0 : static_cast<uint8_t>((static_cast<uint64_t>(completed) * 10) / total);
  }
  const char* qrUrl() const { return url_; }
  const char* finalPath() const { return outputPath_.c_str(); }
  const ProgressSyncResult& progressSyncResult() const { return progressSyncResult_; }
  bool active() const;
  bool needsCoverConversionScratch() const { return coverConversionNeedsScratch(phase_, progressStage_); }

 private:
  friend struct OperationTestPeer;

  enum class ProgressAction : uint8_t {
    AlreadySynced,
    SelectDirection,
    ApplyRemote,
    UploadLocal,
  };

  enum class CoverCacheAction : uint8_t {
    Complete,
    ConvertSource,
    FetchSource,
  };

  enum class ShelfCoverAction : uint8_t {
    Complete,
    FetchDetail,
    FetchSource,
    ConvertSource,
  };

  enum class CoverWorkResult : uint8_t {
    Pending,
    Complete,
    Skipped,
  };

  enum class ShelfCoverPassAction : uint8_t {
    StartSources,
    StartThumbnails,
    Complete,
    Invalid,
  };

  enum class Phase : uint8_t {
    Idle,
    LoginUid,
    LoginPollWait,
    LoginPoll,
    SyncShelf,
    OrganizeShelf,
    ShelfCovers,
    Renew,
    PrepareDetail,
    FetchDetail,
    PrepareBrowseCache,
    FetchBrowse,
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

  static constexpr bool coverConversionNeedsScratch(const Phase phase, const ProgressStage stage) {
    return phase == Phase::ConvertCover || (phase == Phase::ShelfCovers && stage == ProgressStage::Packaging);
  }

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
  static constexpr CoverCacheAction coverCacheAction(const bool hasCurrentBmp, const bool hasSource,
                                                     const bool hasUrl) {
    if (hasCurrentBmp && hasSource) return CoverCacheAction::Complete;
    if (hasSource) return CoverCacheAction::ConvertSource;
    return hasUrl ? CoverCacheAction::FetchSource : CoverCacheAction::Complete;
  }
  static constexpr ShelfCoverAction shelfCoverAction(const bool hasCurrentBmp, const bool hasSource,
                                                     const bool hasUrl) {
    if (hasCurrentBmp) return ShelfCoverAction::Complete;
    if (hasSource) return ShelfCoverAction::ConvertSource;
    return hasUrl ? ShelfCoverAction::FetchSource : ShelfCoverAction::FetchDetail;
  }
  static constexpr ShelfCoverPassAction shelfCoverPassAction(const ProgressStage stage) {
    switch (stage) {
      case ProgressStage::Preparing:
        return ShelfCoverPassAction::StartSources;
      case ProgressStage::Images:
        return ShelfCoverPassAction::StartThumbnails;
      case ProgressStage::Packaging:
        return ShelfCoverPassAction::Complete;
      case ProgressStage::Chapters:
        return ShelfCoverPassAction::Invalid;
    }
    return ShelfCoverPassAction::Invalid;
  }
  static constexpr uint32_t remainingShelfCoverItems(const uint32_t cursor, const uint32_t total) {
    return cursor < total ? total - cursor : 0;
  }
  static constexpr uint32_t shelfCoverWorkCount(const ShelfCoverScope scope, const uint32_t total) {
    if (total > WeReadStore::kLargeShelfThreshold) return 0;
    switch (scope) {
      case ShelfCoverScope::None:
        return 0;
      case ShelfCoverScope::All:
        return total;
    }
    return 0;
  }
  static constexpr Phase shelfCoverResumePhase() { return Phase::ShelfCovers; }
  static constexpr Phase chapterResponseRetryPhase() { return Phase::FetchReader; }
  static constexpr bool shouldRetryPaidPreview(const bool paid, const bool plainText, const bool hasXhtmlTag) {
    return paid && !plainText && !hasXhtmlTag;
  }
  static constexpr bool reuseChapterFile(const bool refreshingBook, const bool chapterExists) {
    return !refreshingBook && chapterExists;
  }
  static constexpr bool imageAttemptPending(const uint8_t attempts) { return attempts < 2; }
  static constexpr bool imageRedirectAllowed(const uint8_t redirects) { return redirects < kMaxImageRedirects; }
  static WeReadProtocol::ImageType selectCoverUrl(const char* detailUrl, const char* shelfUrl, char* output,
                                                  const size_t outputSize) {
    if (!output || outputSize == 0) return WeReadProtocol::ImageType::None;
    output[0] = '\0';
    WeReadProtocol::ImageType type = WeReadProtocol::normalizeCoverImageUrl(detailUrl, output, outputSize);
    if (type != WeReadProtocol::ImageType::None) return type;
    output[0] = '\0';
    type = WeReadProtocol::normalizeCoverImageUrl(shelfUrl, output, outputSize);
    if (type != WeReadProtocol::ImageType::None) return type;
    output[0] = '\0';
    return type;
  }
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
  static constexpr uint32_t browseReviewRequestCount(const uint32_t cached) {
    const uint32_t remaining = cached < WeReadBrowse::kMaxCachedReviews ? WeReadBrowse::kMaxCachedReviews - cached : 0;
    return remaining < 20 ? remaining : 20;
  }
  static constexpr bool browseReviewCursorAdvances(const WeReadBrowse::Cursor& current,
                                                   const WeReadBrowse::Cursor& first, const WeReadBrowse::Cursor& next,
                                                   const uint32_t responseCount) {
    const bool stalled = next.maxIdx == current.maxIdx && next.syncKey == current.syncKey;
    const bool looped = current.page > 0 && next.maxIdx == first.maxIdx && next.syncKey == first.syncKey;
    return responseCount > 0 && !stalled && !looped;
  }

  void startLogin(Phase resume);
  void requestAuthentication(Phase resume);
  void abortBrowseCache();
  Event fail(Error error);
  Event handleRequestError(Error error, Phase retryPhase);
  Event retryChapterResponse();
  Event reauthenticateChapter();
  void requestSucceeded();
  void guardBookSession(const char* phase);
  bool preparePaths();
  Error fetchLoginUid();
  Error pollLogin();
  Error renewSession();
  Error syncShelfOnce();
  Error organizeShelfOnce();
  Event stepShelfCovers();
  bool loadShelfCoverBook(ShelfCoverAction& action);
  void beginShelfCoverPass(ProgressStage stage);
  void advanceShelfCoverItem();
  void skipRemainingShelfCoverItems();
  void logShelfCoverPass(const char* stage) const;
  Error fetchDetailOnce();
  Error fetchBrowseOnce();
  Error fetchCoverSource(CoverWorkResult& result);
  Error convertCoverSource(bool& converted);
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
  bool persistCoverOverride();
  Event cancelNow();
  Error prepareImageWork(WeReadStore::WorkCallback callback, void* callbackContext);
  Event downloadNextImage();
  Error requestImage(WeReadStore::ImageRecord& image, WeReadStore::ImageWorkState& state, uint8_t& attempts,
                     uint8_t& redirects, bool trackProgress, WeReadProtocol::ImageType* detectedType = nullptr);

  Phase phase_ = Phase::Idle;
  Phase resumePhase_ = Phase::Idle;
  Kind kind_ = Kind::Sync;
  ShelfCoverScope shelfCoverScope_ = ShelfCoverScope::None;
  Error error_ = Error::Ok;
  ProgressStage progressStage_ = ProgressStage::Chapters;
  DownloadOptions options_;
  ProgressSyncInput progressSyncInput_;
  ProgressSyncMode progressSyncMode_ = ProgressSyncMode::Compare;
  ProgressSyncResult progressSyncResult_;
  WeReadStore::Session session_;
  WeReadStore::BookRecord book_;
  std::unique_ptr<char[]> shelfCoverUrl_;
  WeReadStore::TocRecord chapter_;
  WeReadHttpClient::Session bookSession_;
  HalFile indexFile_;
  uint32_t chapterCount_ = 0;
  uint32_t firstChapterIndex_ = 0;
  uint32_t lastChapterIndex_ = 0;
  uint32_t chapterIndex_ = 0;
  uint32_t progressCompleted_ = 0;
  uint32_t progressTotal_ = 0;
  uint32_t progressChapterOffset_ = 0;
  uint32_t workCount_ = 0;
  uint32_t workCursor_ = 0;
  uint32_t workCompleted_ = 0;
  uint32_t workCached_ = 0;
  uint32_t workSkipped_ = 0;
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
  unsigned long workStartedAt_ = 0;
  int responseStatus_ = 0;
  uint32_t progressUploadStartedAt_ = 0;
  char previousVid_[64] = {};
  char loginUid_[128] = {};
  char psvts_[128] = {};
  float initialProgressFraction_ = 0.0f;
  bool initialProgressValid_ = false;
  WeReadBrowse::Kind browseKind_ = WeReadBrowse::Kind::PopularHighlights;
  WeReadBrowse::Cursor browseCursor_;
  WeReadBrowse::Cursor browseFirstReviewCursor_;
  WeReadBrowse::CacheManifest browseManifest_;
  bool browseCacheActive_ = false;
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
static_assert(sizeof(Operation) <= 8 * 1024, "WeRead operation exceeds its fixed heap budget");

}  // namespace WeReadClient
