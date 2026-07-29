#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace WeReadStore {

constexpr const char* kRoot = "/.crosspoint/weread";
constexpr const char* kDisclaimerAcceptancePath = "/.crosspoint/weread/disclaimer.accepted";
constexpr const char* kSessionPath = "/.crosspoint/weread/session.bin";
constexpr const char* kShelfPath = "/.crosspoint/weread/shelf.bin";
constexpr uint32_t kShelfMagic = 0x35535257;            // WRS5
constexpr uint32_t kTocMagic = 0x32545257;              // WRT2
constexpr uint32_t kImageMagic = 0x31495257;            // WRI1
constexpr uint32_t kImageWorkMagic = 0x31504957;        // WIP1
constexpr uint32_t kBookOptionsMagic = 0x314F5257;      // WRO1
constexpr uint32_t kBookDetailMagic = 0x31444257;       // WBD1
constexpr uint32_t kInitialProgressMagic = 0x31505257;  // WRP1
constexpr uint16_t kIndexVersion = 1;
constexpr uint16_t kBookOptionsVersion = 1;
constexpr uint16_t kBookDetailVersion = 1;
constexpr uint16_t kBookDetailHeaderSize = 1024;
constexpr uint32_t kMaxBookIntroBytes = 64 * 1024;
constexpr uint16_t kBookDetailIntroTruncated = 1U << 0;

enum class ImagePolicy : uint8_t {
  Embed,
  Exclude,
};

struct BookOptions {
  uint32_t magic = kBookOptionsMagic;
  uint16_t version = kBookOptionsVersion;
  ImagePolicy imagePolicy = ImagePolicy::Embed;
  uint8_t reserved = 0;
};
static_assert(sizeof(BookOptions) == 8);

struct InitialProgress {
  uint32_t magic = kInitialProgressMagic;
  uint32_t millionths = 0;
};
static_assert(sizeof(InitialProgress) == 8);

struct Session {
  char vid[64] = {};
  char skey[384] = {};
  char rt[384] = {};

  bool valid() const { return vid[0] != '\0' && skey[0] != '\0'; }
  void clear();
  bool setCookie(const char* name, const char* value, size_t valueLen);
  bool cookieHeader(char* out, size_t outSize) const;
};

struct ShelfRecord {
  char bookId[64] = {};
  char title[192] = {};
  char author[96] = {};
  uint32_t readUpdateTime = 0;
};
static_assert(sizeof(ShelfRecord) == 356);

enum class ShelfSortResult : uint8_t {
  Ok,
  OutOfMemory,
  StorageError,
};

struct BookDetailHeader {
  uint32_t magic = kBookDetailMagic;
  uint16_t version = kBookDetailVersion;
  uint16_t headerSize = kBookDetailHeaderSize;
  uint32_t introLength = 0;
  uint16_t newRating = 0;
  uint16_t flags = 0;
  uint32_t newRatingCount = 0;
  uint32_t totalWords = 0;
  char title[192] = {};
  char author[96] = {};
  char publisher[96] = {};
  char category[96] = {};
  char coverUrl[512] = {};
  uint8_t reserved[8] = {};
};
static_assert(sizeof(BookDetailHeader) == kBookDetailHeaderSize);

struct TocRecord {
  char chapterUid[64] = {};
  char title[192] = {};
  uint32_t wordCount = 0;
  uint32_t chapterIdx = 0;
  uint8_t paid = 0;
  uint8_t reserved[3] = {};
};
static_assert(sizeof(TocRecord) == 268);

struct ImageRecord {
  char href[64] = {};
  char url[512] = {};
};
static_assert(sizeof(ImageRecord) == 576);

enum class ImageWorkState : uint8_t {
  Pending,
  Complete,
  Skipped,
};

struct ImageWorkRecord {
  ImageRecord image;
  ImageWorkState state = ImageWorkState::Pending;
  uint8_t attempts = 0;
  uint8_t redirects = 0;
  uint8_t reserved = 0;
};
static_assert(sizeof(ImageWorkRecord) == 580);

class IndexWriter {
 public:
  bool begin(const std::string& finalPath, uint32_t magic, uint16_t recordSize);
  bool append(const void* record);
  bool finish();
  void abort();
  uint32_t count() const { return count_; }

 private:
  HalFile file_;
  std::string finalPath_;
  std::string tempPath_;
  uint32_t magic_ = 0;
  uint32_t count_ = 0;
  uint16_t recordSize_ = 0;
  bool active_ = false;
};

class BookDetailWriter {
 public:
  bool begin(const std::string& bookDir);
  bool appendIntro(const uint8_t* data, size_t len);
  bool finish(BookDetailHeader header);
  void abort();

 private:
  HalFile file_;
  std::string finalPath_;
  std::string partPath_;
  uint32_t introLength_ = 0;
  bool truncated_ = false;
  bool active_ = false;
};

bool ensureRoot();
bool hasAcceptedDisclaimer();
bool acceptDisclaimer();
bool loadSession(Session& session);
bool saveSession(const Session& session);
bool clearSession();
bool clearShelf();

bool openShelf(HalFile& file, uint32_t& count);
ShelfSortResult sortShelfByRecent();
bool openToc(const std::string& path, HalFile& file, uint32_t& count);
bool openImageIndex(const std::string& path, HalFile& file, uint32_t& count);
bool openImageWorkIndex(const std::string& path, HalFile& file, uint32_t& count);
bool openImageWorkIndexForUpdate(const std::string& path, HalFile& file, uint32_t& count);
bool readShelfRecord(HalFile& file, uint32_t index, ShelfRecord& record);
bool readTocRecord(HalFile& file, uint32_t index, TocRecord& record);
bool readImageRecord(HalFile& file, uint32_t index, ImageRecord& record);
bool readImageWorkRecord(HalFile& file, uint32_t index, ImageWorkRecord& record);
bool updateImageWorkRecord(HalFile& file, uint32_t count, uint32_t index, const ImageWorkRecord& record);

std::string bookDirectory(const char* bookId);
std::string tocPath(const char* bookId);
std::string chapterPath(const std::string& bookDir, uint32_t chapterIndex);
std::string imageIndexPath(const std::string& bookDir, uint32_t chapterIndex);
std::string imageWorkPath(const std::string& bookDir);
std::string optionsPath(const std::string& bookDir);
std::string initialProgressPath(const char* bookId);
std::string detailPath(const std::string& bookDir);
std::string coverPath(const std::string& bookDir);
std::string finalBookPath(const ShelfRecord& book);
bool findBookIdForPath(const std::string& path, char* bookId, size_t bookIdSize);
bool parseGeneratedChapterHref(const std::string& href, uint32_t& tocIndex);
bool mapFractionToChapter(const std::string& path, float fraction, TocRecord& chapter, uint32_t& chapterOffset);
bool mapPageToChapter(const std::string& path, uint32_t tocIndex, uint16_t pageNumber, uint16_t pageCount,
                      TocRecord& chapter, uint32_t& chapterOffset, float& fraction);
bool mapChapterToFraction(const std::string& path, const char* chapterUid, uint32_t chapterOffset, float& fraction);
bool mapChapterToPosition(const std::string& path, const char* chapterUid, uint32_t chapterOffset, uint32_t& tocIndex,
                          float& chapterFraction, float& bookFraction);

bool loadBookOptions(const std::string& bookDir, BookOptions& options);
bool saveBookOptions(const std::string& bookDir, const BookOptions& options);
bool loadInitialProgress(const char* bookId, float& fraction);
bool saveInitialProgress(const char* bookId, float fraction);
bool clearInitialProgress(const char* bookId);
bool openBookDetail(const std::string& bookDir, BookDetailHeader& header, HalFile& file);
bool atomicReplace(const std::string& partPath, const std::string& finalPath);
bool looksLikeZip(const std::string& path);

class StoreOnlyZipWriter {
 public:
  bool begin(const std::string& outputPath, const std::string& centralPath, uint8_t* buffer, size_t bufferSize);
  bool addBuffer(const char* name, const uint8_t* data, size_t len);
  bool addFile(const char* name, const std::string& sourcePath);
  bool finish();
  void abort();

 private:
  struct CentralRecord {
    char name[96] = {};
    uint32_t crc = 0;
    uint32_t size = 0;
    uint32_t localOffset = 0;
    uint16_t flags = 0;
  };

  bool writeLocalHeader(const char* name, uint16_t flags, uint32_t crc, uint32_t size);
  bool appendCentral(const CentralRecord& record);
  bool writeCentralHeader(const CentralRecord& record);
  bool writeU16(HalFile& file, uint16_t value);
  bool writeU32(HalFile& file, uint32_t value);
  bool writeBytes(HalFile& file, const void* data, size_t len);

  HalFile output_;
  HalFile central_;
  uint8_t* buffer_ = nullptr;
  size_t bufferSize_ = 0;
  std::string outputPath_;
  std::string centralPath_;
  uint16_t entryCount_ = 0;
  bool active_ = false;
};

}  // namespace WeReadStore
