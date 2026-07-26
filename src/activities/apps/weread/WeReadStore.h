#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace WeReadStore {

constexpr const char* kRoot = "/.crosspoint/weread";
constexpr const char* kSessionPath = "/.crosspoint/weread/session.bin";
constexpr const char* kShelfPath = "/.crosspoint/weread/shelf.bin";
constexpr uint32_t kShelfMagic = 0x34535257;  // WRS4
constexpr uint32_t kTocMagic = 0x31545257;    // WRT1
constexpr uint16_t kIndexVersion = 1;

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
};

struct TocRecord {
  char chapterUid[64] = {};
  char title[192] = {};
  uint32_t chapterIdx = 0;
  uint8_t paid = 0;
  uint8_t reserved[3] = {};
};

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

bool ensureRoot();
bool loadSession(Session& session);
bool saveSession(const Session& session);
bool clearSession();
bool clearShelf();

bool openShelf(HalFile& file, uint32_t& count);
bool openToc(const std::string& path, HalFile& file, uint32_t& count);
bool readShelfRecord(HalFile& file, uint32_t index, ShelfRecord& record);
bool readTocRecord(HalFile& file, uint32_t index, TocRecord& record);

std::string bookDirectory(const char* bookId);
std::string tocPath(const char* bookId);
std::string chapterPath(const std::string& bookDir, uint32_t chapterIndex);
std::string finalBookPath(const ShelfRecord& book);

bool atomicReplace(const std::string& partPath, const std::string& finalPath);
bool looksLikeZip(const std::string& path);

class StoreOnlyZipWriter {
 public:
  bool begin(const std::string& outputPath, const std::string& centralPath);
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
  std::unique_ptr<uint8_t[]> buffer_;
  std::string outputPath_;
  std::string centralPath_;
  uint16_t entryCount_ = 0;
  bool active_ = false;
};

}  // namespace WeReadStore
