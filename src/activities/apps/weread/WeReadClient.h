#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "WeReadStore.h"
#include "network/HttpDownloader.h"

namespace WeReadClient {

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
};

class Operation {
 public:
  enum class Kind : uint8_t { Sync, Download };
  enum class Event : uint8_t { None, QrReady, Authenticated, ChapterComplete, Complete, Cancelled, Failed };

  bool begin(Kind kind, const WeReadStore::ShelfRecord* book = nullptr);
  Event step();
  void cancel();
  void reset();

  Error error() const { return error_; }
  uint32_t completedChapters() const { return chapterIndex_; }
  uint32_t chapterCount() const { return chapterCount_; }
  const char* qrUrl() const { return url_; }
  const char* finalPath() const { return outputPath_.c_str(); }
  bool active() const;

 private:
  enum class Phase : uint8_t {
    Idle,
    LoginUid,
    LoginPollWait,
    LoginPoll,
    SyncShelf,
    Renew,
    PrepareDownload,
    FetchToc,
    OpenToc,
    LoadChapter,
    SyncClock,
    FetchPrimary,
    FetchText0,
    FetchText1,
    FetchEpub1,
    FetchEpub3,
    DecodeText,
    DecodeEpub,
    WriteUnavailable,
    AdvanceChapter,
    PackageBook,
    Complete,
    Cancelled,
    Failed,
  };

  static constexpr size_t kCookieSize = 896;
  static constexpr size_t kIoBufferSize = 1024;
  static constexpr size_t kUrlSize = 512;

  void startLogin(Phase resume);
  void requestAuthentication(Phase resume);
  Event fail(Error error);
  Event handleRequestError(Error error, Phase retryPhase);
  void requestSucceeded();
  void guardBookSession(const char* phase);
  bool preparePaths();
  bool waitForShardPace();
  Error fetchLoginUid();
  Error pollLogin();
  Error renewSession();
  Error syncShelfOnce();
  Error fetchTocOnce();
  Error fetchShardOnce(const char* endpoint, const std::string& destination);
  Event inspectPrimary();
  Event decodeChapter(bool plainText);
  Event finishWholeBook(const std::string& source);

  Phase phase_ = Phase::Idle;
  Phase resumePhase_ = Phase::Idle;
  Kind kind_ = Kind::Sync;
  Error error_ = Error::Ok;
  WeReadStore::Session session_;
  WeReadStore::ShelfRecord book_;
  WeReadStore::TocRecord chapter_;
  HttpDownloader::VerifiedSession bookSession_;
  HalFile tocFile_;
  uint32_t chapterCount_ = 0;
  uint32_t chapterIndex_ = 0;
  uint8_t requestAttempt_ = 0;
  bool cancelRequested_ = false;
  bool renewalAttempted_ = false;
  bool loginRecoveryAttempted_ = false;
  bool loginConfirmed_ = false;
  bool primaryPsvtsRefreshed_ = false;
  unsigned long loginStartedAt_ = 0;
  unsigned long nextActionAt_ = 0;
  unsigned long lastShardRequestAt_ = 0;
  int responseStatus_ = 0;
  char previousVid_[64] = {};
  char loginUid_[128] = {};
  char psvts_[128] = {};
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
