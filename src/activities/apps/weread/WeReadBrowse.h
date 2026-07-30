#pragma once

#include <HalStorage.h>
#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "WeReadProtocol.h"
#include "WeReadStore.h"

namespace WeReadBrowse {

enum class Kind : uint8_t {
  PopularHighlights,
  MyHighlights,
  PopularReviews,
};

constexpr size_t kKindCount = 3;
constexpr uint32_t kPageMagic = 0x31425257;  // WRB1
constexpr uint16_t kPageVersion = 1;
constexpr uint32_t kCacheMagic = 0x31435257;  // WRC1
constexpr uint16_t kCacheVersion = 1;
constexpr uint16_t kCacheManifestSize = 100;
constexpr uint32_t kMaxItemTextBytes = 64 * 1024;
constexpr uint32_t kMaxResponseBytes = 4 * 1024 * 1024;
constexpr uint32_t kMaxRecords = 4096;
constexpr uint32_t kMaxCachedReviews = 50;
constexpr uint16_t kRecordTextTruncated = 1U << 0;
constexpr uint16_t kPageResponseTruncated = 1U << 0;
constexpr uint16_t kPageHasMore = 1U << 1;
constexpr uint16_t kCacheReviewsLimited = 1U << 0;

struct Cursor {
  uint32_t page = 0;
  uint32_t maxIdx = 0;
  uint64_t syncKey = 0;
};

struct PageHeader {
  uint64_t nextSyncKey = 0;
  uint32_t magic = kPageMagic;
  uint32_t count = 0;
  uint32_t textBytes = 0;
  uint32_t nextMaxIdx = 0;
  uint16_t version = kPageVersion;
  uint16_t recordSize = 0;
  uint16_t flags = 0;
  Kind kind = Kind::PopularHighlights;
  uint8_t reserved = 0;
};
static_assert(sizeof(PageHeader) == 32);

struct Record {
  uint32_t textOffset = 0;
  uint32_t textLength = 0;
  uint32_t heat = 0;
  uint32_t createTime = 0;
  uint32_t idx = 0;
  uint16_t rating = 0;
  uint16_t flags = 0;
  char chapterUid[64] = {};
  char chapter[128] = {};
  char author[96] = {};
};
static_assert(sizeof(Record) == 312);

struct CacheManifest {
  uint32_t magic = kCacheMagic;
  uint16_t version = kCacheVersion;
  uint16_t size = kCacheManifestSize;
  char ownerVid[64] = {};
  uint32_t pageCounts[kKindCount] = {};
  uint32_t recordCounts[kKindCount] = {};
  uint16_t flags = 0;
  uint8_t activeSlot = 0;
  uint8_t reserved = 0;
};
static_assert(sizeof(CacheManifest) == 100);

constexpr size_t kindIndex(const Kind kind) { return static_cast<size_t>(kind); }

std::string indexPath(const char* bookId, uint8_t slot, Kind kind, uint32_t page);
std::string textPath(const char* bookId, uint8_t slot, Kind kind, uint32_t page);
bool loadCache(const char* bookId, const char* ownerVid, CacheManifest& manifest);
bool beginCache(const char* bookId, const char* ownerVid, CacheManifest& manifest);
bool commitCache(const char* bookId, const CacheManifest& manifest);
void abortCache(const char* bookId, uint8_t slot);
bool clearAllCaches();
bool clearLegacyWorkspace();
bool openPage(const char* bookId, const CacheManifest& manifest, Kind kind, uint32_t page, PageHeader& header,
              HalFile& index, HalFile& text);
bool readRecord(HalFile& index, const PageHeader& header, uint32_t recordIndex, Record& record);

class PageWriter {
 public:
  ~PageWriter() { abort(); }
  bool begin(const char* bookId, uint8_t slot, Kind kind, uint32_t page);
  bool beginRecord();
  bool appendText(const uint8_t* data, size_t len);
  bool finishRecord(Record record);
  bool finish(bool hasMore, uint32_t nextMaxIdx, uint64_t nextSyncKey, bool responseTruncated);
  void abort();
  uint32_t count() const { return header_.count; }
  bool pageTruncated() const { return pageTruncated_; }

 private:
  HalFile index_;
  HalFile text_;
  char indexFinal_[192] = {};
  char indexPart_[192] = {};
  char textFinal_[192] = {};
  char textPart_[192] = {};
  PageHeader header_;
  uint32_t recordStart_ = 0;
  uint32_t recordBytes_ = 0;
  bool recordTruncated_ = false;
  bool pageTruncated_ = false;
  bool recordActive_ = false;
  bool active_ = false;
};

class ResponseParser {
 public:
  ResponseParser(const char* bookId, uint8_t slot, Kind kind, uint32_t page, uint32_t maxRecords = kMaxRecords);

  bool reset();
  bool feed(const uint8_t* data, size_t len);
  bool finish();
  int errorCode() const { return errorCode_; }
  uint32_t count() const { return count_; }
  bool hasMore() const { return hasMore_; }
  uint32_t nextMaxIdx() const { return nextMaxIdx_; }
  uint64_t nextSyncKey() const { return nextSyncKey_; }
  bool responseTruncated() const { return responseTruncated_; }
  bool storageFailed() const { return storageFailed_; }

 private:
  enum class Field : uint8_t {
    None,
    Records,
    ErrorCode,
    HasMore,
    SyncKey,
    Text,
    HtmlText,
    ChapterUid,
    Chapter,
    Author,
    AuthorName,
    Heat,
    Rating,
    CreateTime,
    Idx,
  };

  struct TextFilter {
    ResponseParser* owner = nullptr;
    char entity[16] = {};
    uint8_t entityLen = 0;
    bool html = false;
    bool inTag = false;
    bool inEntity = false;
    bool pendingSpace = false;
    bool atLineStart = true;
  };

  static JsonCallbacks callbacks(ResponseParser* parser);
  static void onKey(void* raw, const char* key, size_t len);
  static void onString(void* raw, const char* value, size_t len);
  static void onNumber(void* raw, const char* value, size_t len);
  static void onBool(void* raw, bool value);
  static void onObjectStart(void* raw);
  static void onObjectEnd(void* raw);
  static void onArrayStart(void* raw);
  static void onArrayEnd(void* raw);
  static void onStringChunk(void* raw, const char* value, size_t len, bool final);
  static bool decodedTextSink(void* raw, const uint8_t* data, size_t len);

  void acceptValue(const char* value, size_t len);
  void startText(bool html);
  bool feedText(const char* value, size_t len, bool final);
  bool filterText(const uint8_t* data, size_t len);
  bool emitFiltered(const uint8_t* data, size_t len);
  bool emitEntity();
  bool emitNewline();
  void startRecord();
  void finishRecord();
  bool isRecordTextField() const;

  Kind kind_;
  uint32_t page_;
  uint32_t maxRecords_;
  char bookId_[64] = {};
  uint8_t slot_ = 0;
  StreamingJsonParser parser_;
  PageWriter writer_;
  WeReadProtocol::JsonStringDecoder decoder_;
  Record current_;
  TextFilter filter_;
  Field field_ = Field::None;
  int depth_ = 0;
  int recordsDepth_ = -1;
  int recordDepth_ = -1;
  int errorCode_ = 0;
  uint32_t responseBytes_ = 0;
  uint32_t count_ = 0;
  uint32_t nextMaxIdx_ = 0;
  uint64_t nextSyncKey_ = 0;
  bool inRecords_ = false;
  bool inRecord_ = false;
  bool sawRecords_ = false;
  bool sawAnyKey_ = false;
  bool rootClosed_ = false;
  bool hasMore_ = false;
  bool responseTruncated_ = false;
  bool storageFailed_ = false;
  bool textSelected_ = false;
  bool textComplete_ = false;
  bool textFailed_ = false;
  bool skipRecord_ = false;
};

}  // namespace WeReadBrowse
