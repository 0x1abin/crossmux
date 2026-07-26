#include "WeReadStore.h"

#include <Logging.h>
#include <Memory.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "WeReadProtocol.h"
#include "util/StringUtils.h"

namespace WeReadStore {
namespace {

struct IndexHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;
  uint32_t count;
};
static_assert(sizeof(IndexHeader) == 12);

constexpr size_t kIoBufferSize = 1024;
constexpr size_t kMaxSessionFileSize = 2048;
constexpr char kSessionMagic[] = "WRA1\n";
constexpr char kShelfPartPath[] = "/.crosspoint/weread/shelf.bin.part";

template <size_t N>
bool setBounded(char (&dest)[N], const char* value, const size_t len) {
  if (!value || len >= N || memchr(value, '\n', len) || memchr(value, '\r', len)) return false;
  if (len == 0) {
    dest[0] = '\0';
    return true;
  }
  memcpy(dest, value, len);
  dest[len] = '\0';
  return true;
}

bool readIndexHeader(const char* path, const uint32_t magic, const uint16_t recordSize, HalFile& file,
                     uint32_t& count) {
  count = 0;
  if (!Storage.openFileForRead("WR", path, file)) return false;
  IndexHeader header{};
  if (file.read(&header, sizeof(header)) != static_cast<int>(sizeof(header)) || header.magic != magic ||
      header.version != kIndexVersion || header.recordSize != recordSize) {
    return false;
  }
  const uint64_t expectedSize =
      static_cast<uint64_t>(sizeof(IndexHeader)) + static_cast<uint64_t>(header.count) * header.recordSize;
  if (file.fileSize64() != expectedSize) return false;
  count = header.count;
  return true;
}

bool readRecord(HalFile& file, const uint32_t index, const uint16_t recordSize, void* record) {
  const uint64_t offset = sizeof(IndexHeader) + static_cast<uint64_t>(index) * recordSize;
  return offset <= SIZE_MAX && file.seek(static_cast<size_t>(offset)) &&
         file.read(record, recordSize) == static_cast<int>(recordSize);
}

uint16_t readLe16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | static_cast<uint16_t>(bytes[1] << 8);
}

uint32_t readLe32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

bool hasOpfSuffix(const char* name, const size_t len) {
  if (len < 4) return false;
  return name[len - 4] == '.' && (name[len - 3] == 'o' || name[len - 3] == 'O') &&
         (name[len - 2] == 'p' || name[len - 2] == 'P') && (name[len - 1] == 'f' || name[len - 1] == 'F');
}

bool validateCentralDirectory(HalFile& file, const uint32_t offset, const uint32_t size, const uint16_t entries) {
  const uint64_t end = static_cast<uint64_t>(offset) + size;
  if (entries < 3 || !file.seek(offset)) return false;
  bool hasMimetype = false;
  bool hasContainer = false;
  bool hasOpf = false;
  for (uint16_t i = 0; i < entries; ++i) {
    uint8_t header[46];
    if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) || readLe32(header) != 0x02014B50 ||
        readLe16(header + 34) != 0 || readLe32(header + 42) >= offset) {
      return false;
    }
    const uint16_t nameLen = readLe16(header + 28);
    const uint32_t trailing = static_cast<uint32_t>(readLe16(header + 30)) + readLe16(header + 32);
    char name[256];
    if (nameLen == 0 || nameLen >= sizeof(name) || file.read(name, nameLen) != static_cast<int>(nameLen)) {
      return false;
    }
    name[nameLen] = '\0';
    hasMimetype |= strcmp(name, "mimetype") == 0;
    hasContainer |= strcmp(name, "META-INF/container.xml") == 0;
    hasOpf |= hasOpfSuffix(name, nameLen);
    const uint64_t next = static_cast<uint64_t>(file.position()) + trailing;
    if (next > end || !file.seek64(next)) return false;
  }
  return file.position() == end && hasMimetype && hasContainer && hasOpf;
}

}  // namespace

void Session::clear() { memset(this, 0, sizeof(*this)); }

bool Session::setCookie(const char* name, const char* value, const size_t valueLen) {
  if (!name || !value) return false;
  if (strcmp(name, "wr_vid") == 0) return setBounded(vid, value, valueLen);
  if (strcmp(name, "wr_skey") == 0) return setBounded(skey, value, valueLen);
  if (strcmp(name, "wr_rt") == 0) return setBounded(rt, value, valueLen);
  return false;
}

bool Session::cookieHeader(char* out, const size_t outSize) const {
  if (!out || outSize == 0 || !valid()) return false;
  const int written =
      snprintf(out, outSize, "wr_vid=%s; wr_skey=%s%s%s", vid, skey, rt[0] ? "; wr_rt=" : "", rt[0] ? rt : "");
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool ensureRoot() { return Storage.ensureDirectoryExists("/.crosspoint") && Storage.ensureDirectoryExists(kRoot); }

bool loadSession(Session& session) {
  session.clear();
  if (!Storage.exists(kSessionPath)) return false;

  HalFile file;
  if (!Storage.openFileForRead("WR", kSessionPath, file) || file.fileSize() == 0 ||
      file.fileSize() > kMaxSessionFileSize) {
    return false;
  }

  // This file is bounded to 2 KB above; the existing obfuscation API operates
  // on std::string, so a small one-shot heap buffer is unavoidable here.
  const String encoded = Storage.readFile(kSessionPath);
  bool decodedOk = false;
  const std::string decoded = obfuscation::deobfuscateFromBase64(encoded.c_str(), &decodedOk);
  if (!decodedOk || decoded.compare(0, sizeof(kSessionMagic) - 1, kSessionMagic) != 0) return false;

  size_t start = sizeof(kSessionMagic) - 1;
  char* fields[] = {session.vid, session.skey, session.rt};
  const size_t capacities[] = {sizeof(session.vid), sizeof(session.skey), sizeof(session.rt)};
  for (size_t i = 0; i < 3; ++i) {
    const size_t end = decoded.find('\n', start);
    if (end == std::string::npos || end - start >= capacities[i] || memchr(decoded.data() + start, '\0', end - start)) {
      session.clear();
      return false;
    }
    memcpy(fields[i], decoded.data() + start, end - start);
    fields[i][end - start] = '\0';
    start = end + 1;
  }
  if (start != decoded.size() || !session.valid()) {
    session.clear();
    return false;
  }
  return true;
}

bool saveSession(const Session& session) {
  if (!session.valid() || !ensureRoot()) return false;
  // Bounded by Session's fixed fields (< 840 bytes). The shared obfuscator
  // requires std::string and this cold path writes only after cookie changes.
  std::string plain;
  plain.reserve(16 + strlen(session.vid) + strlen(session.skey) + strlen(session.rt));
  plain = kSessionMagic;
  plain += session.vid;
  plain += '\n';
  plain += session.skey;
  plain += '\n';
  plain += session.rt;
  plain += '\n';
  const String encoded = obfuscation::obfuscateToBase64(plain);
  return Storage.writeFile(kSessionPath, encoded);
}

bool clearSession() { return !Storage.exists(kSessionPath) || Storage.remove(kSessionPath); }

bool clearShelf() {
  bool ok = true;
  if (Storage.exists(kShelfPath)) ok = Storage.remove(kShelfPath) && ok;
  if (Storage.exists(kShelfPartPath)) ok = Storage.remove(kShelfPartPath) && ok;
  return ok;
}

bool IndexWriter::begin(const std::string& finalPath, const uint32_t magic, const uint16_t recordSize) {
  abort();
  finalPath_ = finalPath;
  tempPath_ = finalPath + ".part";
  magic_ = magic;
  recordSize_ = recordSize;
  count_ = 0;
  if (Storage.exists(tempPath_.c_str())) Storage.remove(tempPath_.c_str());
  if (!Storage.openFileForWrite("WR", tempPath_, file_)) return false;
  const IndexHeader header{magic_, kIndexVersion, recordSize_, 0};
  active_ = file_.write(&header, sizeof(header)) == sizeof(header);
  if (!active_) abort();
  return active_;
}

bool IndexWriter::append(const void* record) {
  if (!active_ || !record || count_ == UINT32_MAX) return false;
  if (file_.write(record, recordSize_) != recordSize_) return false;
  ++count_;
  return true;
}

bool IndexWriter::finish() {
  if (!active_ || !file_.seek(offsetof(IndexHeader, count)) || file_.write(&count_, sizeof(count_)) != sizeof(count_)) {
    abort();
    return false;
  }
  file_.flush();
  file_.close();
  active_ = false;
  if (!atomicReplace(tempPath_, finalPath_)) {
    Storage.remove(tempPath_.c_str());
    return false;
  }
  return true;
}

void IndexWriter::abort() {
  if (file_.isOpen()) file_.close();
  active_ = false;
  if (!tempPath_.empty() && Storage.exists(tempPath_.c_str())) Storage.remove(tempPath_.c_str());
  count_ = 0;
}

bool openShelf(HalFile& file, uint32_t& count) {
  return readIndexHeader(kShelfPath, kShelfMagic, sizeof(ShelfRecord), file, count);
}

bool openToc(const std::string& path, HalFile& file, uint32_t& count) {
  return readIndexHeader(path.c_str(), kTocMagic, sizeof(TocRecord), file, count);
}

bool readShelfRecord(HalFile& file, const uint32_t index, ShelfRecord& record) {
  return readRecord(file, index, sizeof(record), &record);
}

bool readTocRecord(HalFile& file, const uint32_t index, TocRecord& record) {
  return readRecord(file, index, sizeof(record), &record);
}

std::string bookDirectory(const char* bookId) {
  return std::string(kRoot) + "/" + StringUtils::sanitizeFilename(bookId ? bookId : "", 56);
}

std::string tocPath(const char* bookId) { return bookDirectory(bookId) + "/toc.bin"; }

std::string chapterPath(const std::string& bookDir, const uint32_t chapterIndex) {
  char filename[40];
  snprintf(filename, sizeof(filename), "/chapters/%06u.xhtml", static_cast<unsigned>(chapterIndex));
  return bookDir + filename;
}

std::string finalBookPath(const ShelfRecord& book) {
  const std::string title = StringUtils::sanitizeFilename(book.title, 80);
  const std::string id = StringUtils::sanitizeFilename(book.bookId, 40);
  return "/WeRead/" + title + "-" + id + ".epub";
}

bool atomicReplace(const std::string& partPath, const std::string& finalPath) {
  const std::string backupPath = finalPath + ".bak";
  if (Storage.exists(backupPath.c_str())) {
    if (Storage.exists(finalPath.c_str())) {
      Storage.remove(backupPath.c_str());
    } else if (!Storage.rename(backupPath.c_str(), finalPath.c_str())) {
      return false;
    }
  }
  const bool hadFinal = Storage.exists(finalPath.c_str());
  if (hadFinal && !Storage.rename(finalPath.c_str(), backupPath.c_str())) return false;
  if (!Storage.rename(partPath.c_str(), finalPath.c_str())) {
    if (hadFinal) Storage.rename(backupPath.c_str(), finalPath.c_str());
    return false;
  }
  if (hadFinal) Storage.remove(backupPath.c_str());
  return true;
}

bool looksLikeZip(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("WR", path, file) || file.fileSize64() < 80 || file.fileSize64() > UINT32_MAX) {
    return false;
  }

  // EPUB requires the first entry to be an uncompressed "mimetype" file.
  uint8_t local[30];
  if (file.read(local, sizeof(local)) != static_cast<int>(sizeof(local)) || readLe32(local) != 0x04034B50 ||
      readLe16(local + 8) != 0 || readLe32(local + 18) != 20 || readLe32(local + 22) != 20 ||
      readLe16(local + 26) != 8) {
    return false;
  }
  char name[8];
  char mimetype[20];
  if (file.read(name, sizeof(name)) != static_cast<int>(sizeof(name)) ||
      (readLe16(local + 28) > 0 && !file.seek(file.position() + readLe16(local + 28))) ||
      file.read(mimetype, sizeof(mimetype)) != static_cast<int>(sizeof(mimetype)) ||
      memcmp(name, "mimetype", sizeof(name)) != 0 || memcmp(mimetype, "application/epub+zip", sizeof(mimetype)) != 0) {
    return false;
  }

  // ZIP comments can make EOCD sit up to 65,557 bytes from EOF. One reusable
  // 512-byte heap buffer scans backward without a response-sized allocation.
  auto buffer = makeUniqueNoThrow<uint8_t[]>(512);
  if (!buffer) {
    LOG_ERR("WR", "OOM: 512-byte ZIP validation buffer");
    return false;
  }
  uint8_t* const bytes = buffer.get();
  const uint64_t size = file.fileSize64();
  const uint64_t start = size > 65557 ? size - 65557 : 0;
  uint64_t blockEnd = size;
  while (blockEnd > start) {
    const uint64_t blockStart = blockEnd - start > 512 ? blockEnd - 512 : start;
    const size_t wanted = static_cast<size_t>(blockEnd - blockStart);
    if (!file.seek64(blockStart) || file.read(bytes, wanted) != static_cast<int>(wanted)) return false;
    if (wanted >= 4) {
      for (size_t i = wanted - 4;; --i) {
        if (readLe32(&bytes[i]) == 0x06054B50) {
          const uint64_t eocdOffset = blockStart + i;
          uint8_t eocd[22];
          if (file.seek64(eocdOffset) && file.read(eocd, sizeof(eocd)) == static_cast<int>(sizeof(eocd))) {
            const uint16_t entries = readLe16(eocd + 10);
            const uint32_t centralSize = readLe32(eocd + 12);
            const uint32_t centralOffset = readLe32(eocd + 16);
            if (readLe16(eocd + 4) == 0 && readLe16(eocd + 6) == 0 && entries > 0 && readLe16(eocd + 8) == entries &&
                eocdOffset + sizeof(eocd) + readLe16(eocd + 20) == size &&
                static_cast<uint64_t>(centralOffset) + centralSize == eocdOffset &&
                validateCentralDirectory(file, centralOffset, centralSize, entries)) {
              return true;
            }
          }
        }
        if (i == 0) break;
      }
    }
    if (blockStart == start) break;
    blockEnd = blockStart + 3;
  }
  return false;
}

bool StoreOnlyZipWriter::writeBytes(HalFile& file, const void* data, const size_t len) {
  return len == 0 || file.write(data, len) == len;
}

bool StoreOnlyZipWriter::writeU16(HalFile& file, const uint16_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  return writeBytes(file, bytes, sizeof(bytes));
}

bool StoreOnlyZipWriter::writeU32(HalFile& file, const uint32_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                           static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  return writeBytes(file, bytes, sizeof(bytes));
}

bool StoreOnlyZipWriter::begin(const std::string& outputPath, const std::string& centralPath) {
  abort();
  outputPath_ = outputPath;
  centralPath_ = centralPath;
  if (Storage.exists(outputPath_.c_str())) Storage.remove(outputPath_.c_str());
  if (Storage.exists(centralPath_.c_str())) Storage.remove(centralPath_.c_str());
  if (!Storage.openFileForWrite("WR", outputPath_, output_) ||
      !Storage.openFileForWrite("WR", centralPath_, central_)) {
    abort();
    return false;
  }
  // One fixed 1 KB transfer buffer is allocated once for the writer lifetime;
  // chapter files can be arbitrarily large without changing heap use.
  buffer_ = makeUniqueNoThrow<uint8_t[]>(kIoBufferSize);
  if (!buffer_) {
    LOG_ERR("WR", "OOM: %u-byte EPUB writer buffer", static_cast<unsigned>(kIoBufferSize));
    abort();
    return false;
  }
  entryCount_ = 0;
  active_ = true;
  return true;
}

bool StoreOnlyZipWriter::writeLocalHeader(const char* name, const uint16_t flags, const uint32_t crc,
                                          const uint32_t size) {
  const size_t nameLen = strlen(name);
  return nameLen < sizeof(CentralRecord{}.name) && writeU32(output_, 0x04034B50) && writeU16(output_, 20) &&
         writeU16(output_, flags) && writeU16(output_, 0) && writeU16(output_, 0) && writeU16(output_, 0) &&
         writeU32(output_, crc) && writeU32(output_, size) && writeU32(output_, size) &&
         writeU16(output_, static_cast<uint16_t>(nameLen)) && writeU16(output_, 0) &&
         writeBytes(output_, name, nameLen);
}

bool StoreOnlyZipWriter::appendCentral(const CentralRecord& record) {
  if (entryCount_ == UINT16_MAX || central_.write(&record, sizeof(record)) != sizeof(record)) return false;
  ++entryCount_;
  return true;
}

bool StoreOnlyZipWriter::addBuffer(const char* name, const uint8_t* data, const size_t len) {
  if (!active_ || !name || (!data && len != 0) || len > UINT32_MAX || output_.position() > UINT32_MAX) return false;
  uint32_t crc = WeReadProtocol::crc32Update(0xFFFFFFFF, data, len) ^ 0xFFFFFFFF;
  CentralRecord record;
  if (strlen(name) >= sizeof(record.name)) return false;
  strcpy(record.name, name);
  record.crc = crc;
  record.size = static_cast<uint32_t>(len);
  record.localOffset = static_cast<uint32_t>(output_.position());
  record.flags = 0x0800;  // UTF-8 names
  return writeLocalHeader(name, record.flags, crc, record.size) && writeBytes(output_, data, len) &&
         appendCentral(record);
}

bool StoreOnlyZipWriter::addFile(const char* name, const std::string& sourcePath) {
  if (!active_ || !name || output_.position() > UINT32_MAX) return false;
  HalFile source;
  if (!Storage.openFileForRead("WR", sourcePath, source) || source.fileSize64() > UINT32_MAX) return false;

  CentralRecord record;
  if (strlen(name) >= sizeof(record.name)) return false;
  strcpy(record.name, name);
  record.size = static_cast<uint32_t>(source.fileSize64());
  record.localOffset = static_cast<uint32_t>(output_.position());
  record.flags = 0x0808;  // UTF-8 + data descriptor
  if (!writeLocalHeader(name, record.flags, 0, 0)) return false;

  uint32_t crc = 0xFFFFFFFF;
  uint32_t copied = 0;
  while (copied < record.size) {
    const size_t wanted = std::min<size_t>(kIoBufferSize, record.size - copied);
    const int got = source.read(buffer_.get(), wanted);
    if (got <= 0 || !writeBytes(output_, buffer_.get(), static_cast<size_t>(got))) return false;
    crc = WeReadProtocol::crc32Update(crc, buffer_.get(), static_cast<size_t>(got));
    copied += static_cast<uint32_t>(got);
  }
  record.crc = crc ^ 0xFFFFFFFF;
  return writeU32(output_, 0x08074B50) && writeU32(output_, record.crc) && writeU32(output_, record.size) &&
         writeU32(output_, record.size) && appendCentral(record);
}

bool StoreOnlyZipWriter::writeCentralHeader(const CentralRecord& record) {
  const size_t nameLen = strlen(record.name);
  return writeU32(output_, 0x02014B50) && writeU16(output_, 20) && writeU16(output_, 20) &&
         writeU16(output_, record.flags) && writeU16(output_, 0) && writeU16(output_, 0) && writeU16(output_, 0) &&
         writeU32(output_, record.crc) && writeU32(output_, record.size) && writeU32(output_, record.size) &&
         writeU16(output_, static_cast<uint16_t>(nameLen)) && writeU16(output_, 0) && writeU16(output_, 0) &&
         writeU16(output_, 0) && writeU16(output_, 0) && writeU32(output_, 0) &&
         writeU32(output_, record.localOffset) && writeBytes(output_, record.name, nameLen);
}

bool StoreOnlyZipWriter::finish() {
  if (!active_) return false;
  central_.flush();
  central_.close();
  if (output_.position() > UINT32_MAX) {
    abort();
    return false;
  }
  const uint32_t centralOffset = static_cast<uint32_t>(output_.position());
  bool centralOk = true;
  {
    HalFile centralRead;
    if (!Storage.openFileForRead("WR", centralPath_, centralRead)) {
      centralOk = false;
    }
    for (uint16_t i = 0; centralOk && i < entryCount_; ++i) {
      CentralRecord record;
      if (centralRead.read(&record, sizeof(record)) != static_cast<int>(sizeof(record)) ||
          !writeCentralHeader(record)) {
        centralOk = false;
      }
    }
  }
  if (!centralOk || output_.position() > UINT32_MAX) {
    abort();
    return false;
  }
  const uint32_t centralSize = static_cast<uint32_t>(output_.position()) - centralOffset;
  const bool ok = writeU32(output_, 0x06054B50) && writeU16(output_, 0) && writeU16(output_, 0) &&
                  writeU16(output_, entryCount_) && writeU16(output_, entryCount_) && writeU32(output_, centralSize) &&
                  writeU32(output_, centralOffset) && writeU16(output_, 0);
  output_.flush();
  output_.close();
  active_ = false;
  buffer_.reset();
  Storage.remove(centralPath_.c_str());
  if (!ok) Storage.remove(outputPath_.c_str());
  return ok;
}

void StoreOnlyZipWriter::abort() {
  if (output_.isOpen()) output_.close();
  if (central_.isOpen()) central_.close();
  buffer_.reset();
  active_ = false;
  entryCount_ = 0;
  if (!outputPath_.empty() && Storage.exists(outputPath_.c_str())) Storage.remove(outputPath_.c_str());
  if (!centralPath_.empty() && Storage.exists(centralPath_.c_str())) Storage.remove(centralPath_.c_str());
}

}  // namespace WeReadStore
