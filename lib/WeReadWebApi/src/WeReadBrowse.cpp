#include "WeReadBrowse.h"

#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../../../src/util/StringUtils.h"

namespace WeReadBrowse {
namespace {

constexpr char kLegacyWorkspace[] = "/.crosspoint/weread/browse";
constexpr char kCacheRoot[] = "/.crosspoint/weread/browse-cache";

const char* kindName(const Kind kind) {
  switch (kind) {
    case Kind::PopularHighlights:
      return "popular-highlights";
    case Kind::MyHighlights:
      return "my-highlights";
    case Kind::PopularReviews:
      return "popular-reviews";
  }
  return "unknown";
}

std::string bookCacheDirectory(const char* bookId) {
  return std::string(kCacheRoot) + "/" + StringUtils::sanitizeFilename(bookId ? bookId : "", 56);
}

std::string slotDirectory(const char* bookId, const uint8_t slot) {
  return bookCacheDirectory(bookId) + (slot == 0 ? "/slot0" : "/slot1");
}

std::string manifestPath(const char* bookId) { return bookCacheDirectory(bookId) + "/cache.bin"; }

bool makePath(const char* bookId, const uint8_t slot, const Kind kind, const uint32_t page, const char* extension,
              char* output, const size_t outputSize) {
  if (!bookId || !bookId[0] || slot > 1) return false;
  const std::string directory = slotDirectory(bookId, slot);
  const int length = snprintf(output, outputSize, "%s/%s-%06u.%s", directory.c_str(), kindName(kind),
                              static_cast<unsigned>(page), extension);
  return length > 0 && static_cast<size_t>(length) < outputSize;
}

bool boundedString(const char* value, const size_t capacity) { return memchr(value, '\0', capacity) != nullptr; }

bool validKind(const Kind kind) {
  switch (kind) {
    case Kind::PopularHighlights:
    case Kind::MyHighlights:
    case Kind::PopularReviews:
      return true;
  }
  return false;
}

uint64_t parseUint64(const char* value, const size_t len) {
  if (!value || len == 0) return 0;
  uint64_t result = 0;
  for (size_t i = 0; i < len; ++i) {
    if (value[i] < '0' || value[i] > '9') return 0;
    const uint64_t digit = static_cast<uint64_t>(value[i] - '0');
    if (result > (UINT64_MAX - digit) / 10) return 0;
    result = result * 10 + digit;
  }
  return result;
}

template <size_t N>
void copyDecoded(char (&destination)[N], const char* value, const size_t len) {
  WeReadProtocol::decodeJsonString(value, len, destination, N);
}

size_t encodeCodepoint(const uint32_t codepoint, uint8_t (&bytes)[4]) {
  if (codepoint == 0 || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) return 0;
  size_t count = 0;
  if (codepoint <= 0x7F) {
    bytes[count++] = static_cast<uint8_t>(codepoint);
  } else if (codepoint <= 0x7FF) {
    bytes[count++] = static_cast<uint8_t>(0xC0 | (codepoint >> 6));
    bytes[count++] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    bytes[count++] = static_cast<uint8_t>(0xE0 | (codepoint >> 12));
    bytes[count++] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
    bytes[count++] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0x10FFFF) {
    bytes[count++] = static_cast<uint8_t>(0xF0 | (codepoint >> 18));
    bytes[count++] = static_cast<uint8_t>(0x80 | ((codepoint >> 12) & 0x3F));
    bytes[count++] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
    bytes[count++] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  } else {
    return 0;
  }
  return count;
}

bool commitPair(const char* indexPart, const char* indexFinal, const char* textPart, const char* textFinal) {
  if (Storage.exists(indexFinal)) Storage.remove(indexFinal);
  if (Storage.exists(textFinal)) Storage.remove(textFinal);
  if (!Storage.rename(textPart, textFinal)) return false;
  if (Storage.rename(indexPart, indexFinal)) return true;
  Storage.remove(textFinal);
  return false;
}

}  // namespace

std::string indexPath(const char* bookId, const uint8_t slot, const Kind kind, const uint32_t page) {
  char path[192] = {};
  return makePath(bookId, slot, kind, page, "idx", path, sizeof(path)) ? path : "";
}

std::string textPath(const char* bookId, const uint8_t slot, const Kind kind, const uint32_t page) {
  char path[192] = {};
  return makePath(bookId, slot, kind, page, "txt", path, sizeof(path)) ? path : "";
}

bool openPage(const char* bookId, const CacheManifest& manifest, const Kind kind, const uint32_t page,
              PageHeader& header, HalFile& index, HalFile& text) {
  header = {};
  if (!validKind(kind) || manifest.activeSlot > 1 || page >= manifest.pageCounts[kindIndex(kind)]) return false;
  static constexpr uint16_t kKnownPageFlags = kPageResponseTruncated | kPageHasMore;
  if (!Storage.openFileForRead("WR", indexPath(bookId, manifest.activeSlot, kind, page), index) ||
      index.read(&header, sizeof(header)) != static_cast<int>(sizeof(header)) || header.magic != kPageMagic ||
      header.version != kPageVersion || header.recordSize != sizeof(Record) || header.kind != kind ||
      header.count > kMaxRecords || (header.flags & ~kKnownPageFlags) != 0 || header.reserved != 0 ||
      index.fileSize64() != sizeof(PageHeader) + static_cast<uint64_t>(header.count) * sizeof(Record) ||
      !Storage.openFileForRead("WR", textPath(bookId, manifest.activeSlot, kind, page), text) ||
      text.fileSize64() != header.textBytes) {
    return false;
  }
  return true;
}

namespace {

bool validateCache(const char* bookId, const char* ownerVid, const CacheManifest& manifest) {
  static constexpr uint16_t kKnownCacheFlags = kCacheReviewsLimited;
  if (!bookId || !bookId[0] || !ownerVid || !ownerVid[0] || manifest.magic != kCacheMagic ||
      manifest.version != kCacheVersion || manifest.size != sizeof(CacheManifest) || manifest.activeSlot > 1 ||
      (manifest.flags & ~kKnownCacheFlags) != 0 || manifest.reserved != 0 ||
      !boundedString(manifest.ownerVid, sizeof(manifest.ownerVid)) || strcmp(manifest.ownerVid, ownerVid) != 0 ||
      manifest.pageCounts[kindIndex(Kind::PopularHighlights)] != 1 ||
      manifest.pageCounts[kindIndex(Kind::MyHighlights)] != 1 ||
      manifest.pageCounts[kindIndex(Kind::PopularReviews)] == 0 ||
      manifest.pageCounts[kindIndex(Kind::PopularReviews)] > kMaxCachedReviews ||
      manifest.recordCounts[kindIndex(Kind::PopularHighlights)] > kMaxRecords ||
      manifest.recordCounts[kindIndex(Kind::MyHighlights)] > kMaxRecords ||
      manifest.recordCounts[kindIndex(Kind::PopularReviews)] > kMaxCachedReviews ||
      ((manifest.flags & kCacheReviewsLimited) != 0 &&
       manifest.recordCounts[kindIndex(Kind::PopularReviews)] != kMaxCachedReviews)) {
    return false;
  }

  static constexpr Kind kKinds[] = {Kind::PopularHighlights, Kind::MyHighlights, Kind::PopularReviews};
  return std::all_of(kKinds, kKinds + kKindCount, [&](const Kind kind) {
    uint32_t records = 0;
    for (uint32_t page = 0; page < manifest.pageCounts[kindIndex(kind)]; ++page) {
      PageHeader header;
      HalFile index;
      HalFile text;
      if (!openPage(bookId, manifest, kind, page, header, index, text) || records > UINT32_MAX - header.count) {
        return false;
      }
      records += header.count;
    }
    return records == manifest.recordCounts[kindIndex(kind)];
  });
}

bool readManifest(const char* bookId, const char* ownerVid, const std::string& path, CacheManifest& manifest) {
  manifest = {};
  HalFile file;
  return Storage.openFileForRead("WR", path, file) && file.fileSize64() == sizeof(manifest) &&
         file.read(&manifest, sizeof(manifest)) == static_cast<int>(sizeof(manifest)) &&
         validateCache(bookId, ownerVid, manifest);
}

}  // namespace

bool loadCache(const char* bookId, const char* ownerVid, CacheManifest& manifest) {
  manifest = {};
  const std::string finalPath = manifestPath(bookId);
  const std::string backupPath = finalPath + ".bak";
  if (!Storage.exists(finalPath.c_str()) && Storage.exists(backupPath.c_str()) &&
      !Storage.rename(backupPath.c_str(), finalPath.c_str())) {
    return false;
  }
  if (readManifest(bookId, ownerVid, finalPath, manifest)) {
    if (Storage.exists(backupPath.c_str())) Storage.remove(backupPath.c_str());
    return true;
  }
  if (!Storage.exists(backupPath.c_str())) return false;
  if (Storage.exists(finalPath.c_str()) && !Storage.remove(finalPath.c_str())) return false;
  if (!Storage.rename(backupPath.c_str(), finalPath.c_str())) return false;
  return readManifest(bookId, ownerVid, finalPath, manifest);
}

bool beginCache(const char* bookId, const char* ownerVid, CacheManifest& manifest) {
  if (!bookId || !bookId[0] || !ownerVid || !ownerVid[0] || strlen(ownerVid) >= sizeof(manifest.ownerVid) ||
      !WeReadStore::ensureRoot() || !Storage.ensureDirectoryExists(kCacheRoot)) {
    return false;
  }
  CacheManifest current;
  const bool hasCurrent = loadCache(bookId, ownerVid, current);
  const std::string bookDirectory = bookCacheDirectory(bookId);
  if (!hasCurrent && Storage.exists(bookDirectory.c_str()) && !Storage.removeDir(bookDirectory.c_str())) return false;
  if (!Storage.ensureDirectoryExists(bookDirectory.c_str())) return false;

  manifest = {};
  memcpy(manifest.ownerVid, ownerVid, strlen(ownerVid) + 1);
  manifest.activeSlot = hasCurrent ? static_cast<uint8_t>(1 - current.activeSlot) : 0;
  const std::string staging = slotDirectory(bookId, manifest.activeSlot);
  if (Storage.exists(staging.c_str()) && !Storage.removeDir(staging.c_str())) return false;
  return Storage.ensureDirectoryExists(staging.c_str());
}

bool commitCache(const char* bookId, const CacheManifest& manifest) {
  if (!validateCache(bookId, manifest.ownerVid, manifest)) return false;
  const std::string finalPath = manifestPath(bookId);
  const std::string partPath = finalPath + ".part";
  if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
  bool written = false;
  {
    HalFile file;
    written =
        Storage.openFileForWrite("WR", partPath, file) && file.write(&manifest, sizeof(manifest)) == sizeof(manifest);
    if (written) file.flush();
  }
  if (!written || !WeReadStore::atomicReplace(partPath, finalPath)) {
    if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
    return false;
  }
  const std::string oldSlot = slotDirectory(bookId, static_cast<uint8_t>(1 - manifest.activeSlot));
  if (Storage.exists(oldSlot.c_str()) && !Storage.removeDir(oldSlot.c_str())) {
    LOG_ERR("WR", "failed to remove old browse cache slot");
  }
  return true;
}

void abortCache(const char* bookId, const uint8_t slot) {
  if (!bookId || !bookId[0] || slot > 1) return;
  const std::string staging = slotDirectory(bookId, slot);
  if (Storage.exists(staging.c_str())) Storage.removeDir(staging.c_str());
  const std::string partPath = manifestPath(bookId) + ".part";
  if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
}

bool clearAllCaches() {
  if (!Storage.exists(kCacheRoot)) return true;
  return Storage.removeDir(kCacheRoot);
}

bool clearLegacyWorkspace() {
  if (!Storage.exists(kLegacyWorkspace)) return true;
  return Storage.removeDir(kLegacyWorkspace);
}

bool readRecord(HalFile& index, const PageHeader& header, const uint32_t recordIndex, Record& record) {
  if (!index.isOpen() || recordIndex >= header.count) return false;
  const uint64_t offset = sizeof(PageHeader) + static_cast<uint64_t>(recordIndex) * sizeof(Record);
  if (offset > SIZE_MAX || !index.seek(static_cast<size_t>(offset)) ||
      index.read(&record, sizeof(record)) != static_cast<int>(sizeof(record)) ||
      !boundedString(record.chapterUid, sizeof(record.chapterUid)) ||
      !boundedString(record.chapter, sizeof(record.chapter)) || !boundedString(record.author, sizeof(record.author)) ||
      (record.flags & ~kRecordTextTruncated) != 0 ||
      static_cast<uint64_t>(record.textOffset) + record.textLength > header.textBytes) {
    return false;
  }
  return true;
}

bool PageWriter::begin(const char* bookId, const uint8_t slot, const Kind kind, const uint32_t page) {
  abort();
  if (!makePath(bookId, slot, kind, page, "idx", indexFinal_, sizeof(indexFinal_)) ||
      !makePath(bookId, slot, kind, page, "idx.part", indexPart_, sizeof(indexPart_)) ||
      !makePath(bookId, slot, kind, page, "txt", textFinal_, sizeof(textFinal_)) ||
      !makePath(bookId, slot, kind, page, "txt.part", textPart_, sizeof(textPart_))) {
    return false;
  }
  if (Storage.exists(indexFinal_)) Storage.remove(indexFinal_);
  if (Storage.exists(textFinal_)) Storage.remove(textFinal_);
  if (Storage.exists(indexPart_)) Storage.remove(indexPart_);
  if (Storage.exists(textPart_)) Storage.remove(textPart_);
  if (!Storage.openFileForWrite("WR", indexPart_, index_) || !Storage.openFileForWrite("WR", textPart_, text_)) {
    abort();
    return false;
  }
  header_ = {};
  header_.recordSize = sizeof(Record);
  header_.kind = kind;
  pageTruncated_ = false;
  if (index_.write(&header_, sizeof(header_)) != sizeof(header_)) {
    abort();
    return false;
  }
  active_ = true;
  return true;
}

bool PageWriter::beginRecord() {
  if (!active_ || recordActive_ || header_.count >= kMaxRecords) return false;
  recordStart_ = header_.textBytes;
  recordBytes_ = 0;
  recordTruncated_ = false;
  recordActive_ = true;
  return true;
}

bool PageWriter::appendText(const uint8_t* data, const size_t len) {
  if (!active_ || !recordActive_ || (!data && len != 0)) return false;
  const uint32_t available = std::min(kMaxItemTextBytes - recordBytes_, kMaxResponseBytes - header_.textBytes);
  const size_t accepted = std::min<size_t>(len, available);
  if (accepted > 0 && text_.write(data, accepted) != accepted) return false;
  recordBytes_ += static_cast<uint32_t>(accepted);
  header_.textBytes += static_cast<uint32_t>(accepted);
  if (accepted != len) {
    recordTruncated_ = true;
    if (header_.textBytes == kMaxResponseBytes) pageTruncated_ = true;
  }
  return true;
}

bool PageWriter::finishRecord(Record record) {
  if (!active_ || !recordActive_) return false;
  record.textOffset = recordStart_;
  record.textLength = recordBytes_;
  if (recordTruncated_) record.flags |= kRecordTextTruncated;
  recordActive_ = false;
  if (record.textLength == 0) return true;
  if (index_.write(&record, sizeof(record)) != sizeof(record)) return false;
  ++header_.count;
  return true;
}

bool PageWriter::finish(const bool hasMore, const uint32_t nextMaxIdx, const uint64_t nextSyncKey,
                        const bool responseTruncated) {
  if (!active_ || recordActive_) return false;
  header_.nextMaxIdx = hasMore ? nextMaxIdx : 0;
  header_.nextSyncKey = hasMore ? nextSyncKey : 0;
  if (hasMore) header_.flags |= kPageHasMore;
  if (responseTruncated || pageTruncated_) header_.flags |= kPageResponseTruncated;
  if (!index_.seek(0) || index_.write(&header_, sizeof(header_)) != sizeof(header_)) {
    abort();
    return false;
  }
  index_.flush();
  text_.flush();
  index_.close();
  text_.close();
  active_ = false;
  if (!commitPair(indexPart_, indexFinal_, textPart_, textFinal_)) {
    Storage.remove(indexPart_);
    Storage.remove(textPart_);
    return false;
  }
  return true;
}

void PageWriter::abort() {
  if (index_.isOpen()) index_.close();
  if (text_.isOpen()) text_.close();
  active_ = false;
  recordActive_ = false;
  pageTruncated_ = false;
  if (indexPart_[0] && Storage.exists(indexPart_)) Storage.remove(indexPart_);
  if (textPart_[0] && Storage.exists(textPart_)) Storage.remove(textPart_);
}

ResponseParser::ResponseParser(const char* bookId, const uint8_t slot, const Kind kind, const uint32_t page,
                               const uint32_t maxRecords)
    : kind_(kind),
      page_(page),
      maxRecords_(std::min(maxRecords, kMaxRecords)),
      slot_(slot),
      parser_(callbacks(this)),
      decoder_(decodedTextSink, &filter_) {
  if (bookId) strncpy(bookId_, bookId, sizeof(bookId_) - 1);
  filter_.owner = this;
}

JsonCallbacks ResponseParser::callbacks(ResponseParser* parser) {
  return {parser,        onKey,       onString,     onNumber,   onBool,       nullptr,
          onObjectStart, onObjectEnd, onArrayStart, onArrayEnd, onStringChunk};
}

bool ResponseParser::reset() {
  writer_.abort();
  current_ = {};
  filter_ = {};
  filter_.owner = this;
  field_ = Field::None;
  depth_ = 0;
  recordsDepth_ = -1;
  recordDepth_ = -1;
  errorCode_ = 0;
  responseBytes_ = 0;
  count_ = 0;
  nextMaxIdx_ = 0;
  nextSyncKey_ = 0;
  inRecords_ = false;
  inRecord_ = false;
  sawRecords_ = false;
  sawAnyKey_ = false;
  rootClosed_ = false;
  hasMore_ = false;
  responseTruncated_ = false;
  storageFailed_ = false;
  textSelected_ = false;
  textComplete_ = false;
  textFailed_ = false;
  skipRecord_ = false;
  parser_.reset();
  decoder_.reset();
  return bookId_[0] && slot_ <= 1 && maxRecords_ > 0 && writer_.begin(bookId_, slot_, kind_, page_);
}

bool ResponseParser::feed(const uint8_t* data, const size_t len) {
  if (!data && len != 0) return false;
  if (len > kMaxResponseBytes - std::min(responseBytes_, kMaxResponseBytes)) {
    responseTruncated_ = true;
  }
  responseBytes_ = static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(responseBytes_) + len, UINT32_MAX));
  parser_.feed(reinterpret_cast<const char*>(data), len);
  return !parser_.hasError() && !textFailed_;
}

bool ResponseParser::finish() {
  if (errorCode_ != 0 && rootClosed_ && !parser_.hasError()) {
    writer_.abort();
    return true;
  }
  const bool emptyObject = !sawAnyKey_ && !sawRecords_;
  if (inRecord_ || !rootClosed_ || (!sawRecords_ && !emptyObject) || parser_.hasError() || textFailed_) {
    writer_.abort();
    return false;
  }
  count_ = writer_.count();
  responseTruncated_ = responseTruncated_ || writer_.pageTruncated();
  if (writer_.finish(hasMore_, nextMaxIdx_, nextSyncKey_, responseTruncated_)) return true;
  storageFailed_ = true;
  return false;
}

void ResponseParser::onKey(void* raw, const char* key, size_t) {
  auto& self = *static_cast<ResponseParser*>(raw);
  self.sawAnyKey_ = true;
  const char* recordsKey = self.kind_ == Kind::PopularHighlights ? "items"
                           : self.kind_ == Kind::MyHighlights    ? "updated"
                                                                 : "reviews";
  if (strcmp(key, recordsKey) == 0) {
    self.field_ = Field::Records;
  } else if (strcmp(key, "errcode") == 0 || strcmp(key, "errCode") == 0) {
    self.field_ = Field::ErrorCode;
  } else if (strcmp(key, "reviewsHasMore") == 0) {
    self.field_ = Field::HasMore;
  } else if (strcmp(key, "synckey") == 0) {
    self.field_ = Field::SyncKey;
  } else if (strcmp(key, "markText") == 0 || strcmp(key, "content") == 0) {
    self.field_ = Field::Text;
  } else if (strcmp(key, "htmlContent") == 0) {
    self.field_ = Field::HtmlText;
  } else if (strcmp(key, "chapterUid") == 0) {
    self.field_ = Field::ChapterUid;
  } else if (strcmp(key, "chapterTitle") == 0 || strcmp(key, "chapterName") == 0) {
    self.field_ = Field::Chapter;
  } else if (strcmp(key, "author") == 0) {
    self.field_ = Field::Author;
  } else if (strcmp(key, "name") == 0 || strcmp(key, "nick") == 0 || strcmp(key, "nickname") == 0) {
    self.field_ = Field::AuthorName;
  } else if (strcmp(key, "totalCount") == 0 || strcmp(key, "likeCount") == 0) {
    self.field_ = Field::Heat;
  } else if (strcmp(key, "star") == 0 || strcmp(key, "rating") == 0) {
    self.field_ = Field::Rating;
  } else if (strcmp(key, "createTime") == 0) {
    self.field_ = Field::CreateTime;
  } else if (strcmp(key, "idx") == 0) {
    self.field_ = Field::Idx;
  } else {
    self.field_ = Field::None;
  }
}

void ResponseParser::onString(void* raw, const char* value, const size_t len) {
  auto& self = *static_cast<ResponseParser*>(raw);
  if (self.inRecord_ && !self.skipRecord_ && self.isRecordTextField()) {
    if (len == 0) {
      self.field_ = Field::None;
      return;
    }
    self.startText(self.field_ == Field::HtmlText);
    if (!self.feedText(value, len, true)) self.textFailed_ = true;
    self.textComplete_ = true;
    self.field_ = Field::None;
    return;
  }
  self.acceptValue(value, len);
}

void ResponseParser::onNumber(void* raw, const char* value, const size_t len) {
  static_cast<ResponseParser*>(raw)->acceptValue(value, len);
}

void ResponseParser::onBool(void* raw, const bool value) {
  auto& self = *static_cast<ResponseParser*>(raw);
  if (self.field_ == Field::HasMore) self.hasMore_ = value;
  self.field_ = Field::None;
}

void ResponseParser::onObjectStart(void* raw) {
  auto& self = *static_cast<ResponseParser*>(raw);
  ++self.depth_;
  if (self.inRecords_ && !self.inRecord_ && self.depth_ == self.recordsDepth_ + 1) self.startRecord();
  self.field_ = Field::None;
}

void ResponseParser::onObjectEnd(void* raw) {
  auto& self = *static_cast<ResponseParser*>(raw);
  if (self.inRecord_ && self.depth_ == self.recordDepth_) self.finishRecord();
  if (self.depth_ == 1) self.rootClosed_ = true;
  if (self.depth_ > 0) --self.depth_;
  self.field_ = Field::None;
}

void ResponseParser::onArrayStart(void* raw) {
  auto& self = *static_cast<ResponseParser*>(raw);
  ++self.depth_;
  if (self.field_ == Field::Records && !self.inRecords_) {
    self.inRecords_ = true;
    self.sawRecords_ = true;
    self.recordsDepth_ = self.depth_;
  }
  self.field_ = Field::None;
}

void ResponseParser::onArrayEnd(void* raw) {
  auto& self = *static_cast<ResponseParser*>(raw);
  if (self.inRecords_ && self.depth_ == self.recordsDepth_) {
    self.inRecords_ = false;
    self.recordsDepth_ = -1;
  }
  if (self.depth_ > 0) --self.depth_;
  self.field_ = Field::None;
}

void ResponseParser::onStringChunk(void* raw, const char* value, const size_t len, const bool final) {
  auto& self = *static_cast<ResponseParser*>(raw);
  if (!self.inRecord_ || self.skipRecord_ || !self.isRecordTextField()) return;
  self.startText(self.field_ == Field::HtmlText);
  if (!self.feedText(value, len, final)) self.textFailed_ = true;
  if (final) {
    self.textComplete_ = true;
    self.field_ = Field::None;
  }
}

bool ResponseParser::decodedTextSink(void* raw, const uint8_t* data, const size_t len) {
  auto& filter = *static_cast<TextFilter*>(raw);
  if (!filter.owner) return false;
  if (filter.owner->filterText(data, len)) return true;
  filter.owner->storageFailed_ = true;
  return false;
}

void ResponseParser::acceptValue(const char* value, const size_t len) {
  if (field_ == Field::ErrorCode) {
    char number[24] = {};
    if (len < sizeof(number)) {
      memcpy(number, value, len);
      errorCode_ = atoi(number);
    }
  } else if (field_ == Field::SyncKey) {
    nextSyncKey_ = parseUint64(value, len);
  } else if (field_ == Field::HasMore) {
    hasMore_ = len == 1 && value[0] == '1';
  } else if (inRecord_) {
    switch (field_) {
      case Field::ChapterUid:
        copyDecoded(current_.chapterUid, value, len);
        break;
      case Field::Chapter:
        copyDecoded(current_.chapter, value, len);
        break;
      case Field::Author:
      case Field::AuthorName:
        if (!current_.author[0]) copyDecoded(current_.author, value, len);
        break;
      case Field::Heat:
        current_.heat = WeReadProtocol::parseUint32OrZero(value, len);
        break;
      case Field::Rating: {
        const uint32_t rating = WeReadProtocol::parseUint32OrZero(value, len);
        current_.rating = static_cast<uint16_t>(std::min<uint32_t>(rating, UINT16_MAX));
        break;
      }
      case Field::CreateTime:
        current_.createTime = WeReadProtocol::parseUint32OrZero(value, len);
        break;
      case Field::Idx:
        current_.idx = WeReadProtocol::parseUint32OrZero(value, len);
        break;
      default:
        break;
    }
  }
  field_ = Field::None;
}

void ResponseParser::startText(const bool html) {
  if (textSelected_) return;
  textSelected_ = true;
  filter_.html = html;
  filter_.inTag = false;
  filter_.inEntity = false;
  filter_.pendingSpace = false;
  filter_.atLineStart = true;
  filter_.entityLen = 0;
  decoder_.reset();
}

bool ResponseParser::feedText(const char* value, const size_t len, const bool final) {
  if (!decoder_.feed(value, len)) return false;
  if (!final) return true;
  if (!decoder_.finish()) return false;
  if (filter_.inEntity && !emitEntity()) return false;
  filter_.inTag = false;
  return true;
}

bool ResponseParser::filterText(const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const uint8_t value = data[i];
    if (filter_.html && filter_.inTag) {
      if (value == '>') {
        filter_.inTag = false;
        filter_.pendingSpace = true;
      }
      continue;
    }
    if (filter_.html && value == '<') {
      if (filter_.inEntity && !emitEntity()) return false;
      filter_.inTag = true;
      continue;
    }
    if (filter_.html && filter_.inEntity) {
      if (value == ';') {
        if (!emitEntity()) return false;
      } else if (static_cast<size_t>(filter_.entityLen) + 1 < sizeof(filter_.entity) && !std::isspace(value) &&
                 value != '&') {
        filter_.entity[filter_.entityLen++] = static_cast<char>(value);
      } else {
        if (!emitFiltered(reinterpret_cast<const uint8_t*>("&"), 1) ||
            !emitFiltered(reinterpret_cast<const uint8_t*>(filter_.entity), filter_.entityLen)) {
          return false;
        }
        filter_.inEntity = false;
        filter_.entityLen = 0;
      }
      continue;
    }
    if (filter_.html && value == '&') {
      filter_.inEntity = true;
      filter_.entityLen = 0;
      continue;
    }
    if (value == '\r') continue;
    if (value == '\n') {
      if (!emitNewline()) return false;
      continue;
    }
    if (std::isspace(value)) {
      filter_.pendingSpace = !filter_.atLineStart;
      continue;
    }
    if (!emitFiltered(&value, 1)) return false;
  }
  return true;
}

bool ResponseParser::emitFiltered(const uint8_t* data, const size_t len) {
  if (len == 0) return true;
  if (filter_.pendingSpace && !filter_.atLineStart) {
    static constexpr uint8_t space = ' ';
    if (!writer_.appendText(&space, 1)) return false;
  }
  filter_.pendingSpace = false;
  filter_.atLineStart = false;
  return writer_.appendText(data, len);
}

bool ResponseParser::emitEntity() {
  filter_.entity[filter_.entityLen] = '\0';
  const char* replacement = nullptr;
  if (strcmp(filter_.entity, "amp") == 0) replacement = "&";
  if (strcmp(filter_.entity, "lt") == 0) replacement = "<";
  if (strcmp(filter_.entity, "gt") == 0) replacement = ">";
  if (strcmp(filter_.entity, "quot") == 0) replacement = "\"";
  if (strcmp(filter_.entity, "apos") == 0) replacement = "'";
  if (strcmp(filter_.entity, "nbsp") == 0) replacement = " ";
  bool ok = true;
  if (replacement) {
    ok = emitFiltered(reinterpret_cast<const uint8_t*>(replacement), strlen(replacement));
  } else if (filter_.entity[0] == '#') {
    char* end = nullptr;
    const bool hex = filter_.entity[1] == 'x' || filter_.entity[1] == 'X';
    const char* digits = filter_.entity + (hex ? 2 : 1);
    const uint32_t codepoint = static_cast<uint32_t>(strtoul(digits, &end, hex ? 16 : 10));
    if (end != digits && *end == '\0' && codepoint != 0 && !(codepoint >= 0xD800 && codepoint <= 0xDFFF) &&
        codepoint <= 0x10FFFF) {
      uint8_t bytes[4];
      const size_t byteCount = encodeCodepoint(codepoint, bytes);
      ok = byteCount > 0 && emitFiltered(bytes, byteCount);
    } else {
      ok = emitFiltered(reinterpret_cast<const uint8_t*>("&"), 1) &&
           emitFiltered(reinterpret_cast<const uint8_t*>(filter_.entity), filter_.entityLen) &&
           emitFiltered(reinterpret_cast<const uint8_t*>(";"), 1);
    }
  } else {
    ok = emitFiltered(reinterpret_cast<const uint8_t*>("&"), 1) &&
         emitFiltered(reinterpret_cast<const uint8_t*>(filter_.entity), filter_.entityLen) &&
         emitFiltered(reinterpret_cast<const uint8_t*>(";"), 1);
  }
  filter_.inEntity = false;
  filter_.entityLen = 0;
  return ok;
}

bool ResponseParser::emitNewline() {
  if (filter_.atLineStart) return true;
  static constexpr uint8_t newline = '\n';
  filter_.pendingSpace = false;
  filter_.atLineStart = true;
  return writer_.appendText(&newline, 1);
}

void ResponseParser::startRecord() {
  current_ = {};
  textSelected_ = false;
  textComplete_ = false;
  skipRecord_ = writer_.count() >= maxRecords_;
  if (skipRecord_) {
    responseTruncated_ = true;
  } else {
    if (!writer_.beginRecord()) {
      storageFailed_ = true;
      textFailed_ = true;
    }
  }
  inRecord_ = true;
  recordDepth_ = depth_;
}

void ResponseParser::finishRecord() {
  if (skipRecord_) {
    inRecord_ = false;
    recordDepth_ = -1;
    skipRecord_ = false;
    return;
  }
  if (textSelected_ && !decoder_.finish()) textFailed_ = true;
  if (!textFailed_ && !writer_.finishRecord(current_)) {
    storageFailed_ = true;
    textFailed_ = true;
  }
  if (current_.idx != 0) nextMaxIdx_ = current_.idx;
  inRecord_ = false;
  recordDepth_ = -1;
  textSelected_ = false;
  textComplete_ = false;
}

bool ResponseParser::isRecordTextField() const {
  if (field_ != Field::Text && field_ != Field::HtmlText) return false;
  if (textSelected_ && textComplete_) return false;
  if (kind_ != Kind::PopularReviews) return field_ == Field::Text;
  return field_ == Field::Text || field_ == Field::HtmlText;
}

}  // namespace WeReadBrowse
