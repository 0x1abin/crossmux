#include "ZipFile.h"

#include <HalStorage.h>
#include <InflateReader.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <limits>

#include "ZipParsing.h"

struct ZipInflateCtx {
  HalFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

struct ZipPrefixInflateCtx {
  InflateReader reader;  // Must be first: uzlib passes only its embedded state to the callback.
  HalFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};
static_assert(offsetof(ZipPrefixInflateCtx, reader) == 0);

namespace {
constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;
// ponytail: image probes only need a bounded prefix; raise this only for a real
// EPUB whose valid JPEG frame header lies beyond 16KB.
constexpr size_t EARLY_STOP_PREFIX_BYTES = 16 * 1024;

// RAII zip: opens the zip if not already open, closes on destruction only if
// it performed the open.  Removes the wasOpen/close boilerplate from every method.
class ScopedOpenClose final {
 public:
  [[nodiscard]] explicit ScopedOpenClose(ZipFile& zf) : zf(zf), needsClose(!zf.isOpen()) {
    if (needsClose) ok = zf.open();
  }
  ~ScopedOpenClose() {
    if (needsClose && ok) zf.close();
  }
  ScopedOpenClose(const ScopedOpenClose&) = delete;
  ScopedOpenClose& operator=(const ScopedOpenClose&) = delete;
  ScopedOpenClose(ScopedOpenClose&&) = delete;
  ScopedOpenClose& operator=(ScopedOpenClose&&) = delete;
  explicit operator bool() const { return ok || !needsClose; }

 private:
  ZipFile& zf;
  bool needsClose = false;
  bool ok = true;  // true when zip was already open (no open() call needed)
};

size_t zipFillCallback(void* vctx, const uint8_t** data) {
  auto* ctx = static_cast<ZipInflateCtx*>(vctx);
  if (ctx->fileRemaining == 0) return 0;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const int result = ctx->file->read(ctx->readBuf, toRead);
  // HalFile::read() returns a negative int on error. Treat it as end-of-stream
  // rather than letting the negative-to-size_t conversion underflow fileRemaining
  // and report a huge bytesRead, which would have the inflate library read past
  // the end of readBuf.
  if (result < 0) {
    LOG_ERR("ZIP", "Failed to read compressed data: %d", result);
    return 0;
  }
  const size_t bytesRead = static_cast<size_t>(result);
  ctx->fileRemaining -= bytesRead;

  *data = ctx->readBuf;
  return bytesRead;
}

int zipPrefixReadCallback(uzlib_uncomp* uncomp) {
  auto* ctx = reinterpret_cast<ZipPrefixInflateCtx*>(uncomp);
  if (ctx->fileRemaining == 0) return -1;

  const size_t toRead = std::min(ctx->fileRemaining, ctx->readBufSize);
  const int readResult = ctx->file->read(ctx->readBuf, toRead);
  const size_t bytesRead = readResult > 0 ? static_cast<size_t>(readResult) : 0;
  ctx->fileRemaining -= bytesRead;
  if (bytesRead == 0) return -1;

  uncomp->source = ctx->readBuf + 1;
  uncomp->source_limit = ctx->readBuf + bytesRead;
  return ctx->readBuf[0];
}

bool streamDeflatedPrefix(HalFile& file, Print& out, const size_t chunkSize, const size_t compressedSize,
                          const size_t inflatedSize) {
  if (inflatedSize == 0) return true;

  const size_t prefixSize = std::min(inflatedSize, EARLY_STOP_PREFIX_BYTES);
  // The bounded prefix cannot live on the small render-task stack. Allocate the
  // largest block first so a fragmented heap does not lose the 16KB run.
  auto prefix = makeUniqueNoThrow<uint8_t[]>(prefixSize);
  if (!prefix) {
    LOG_ERR("ZIP", "Failed to allocate %zu-byte early-stop prefix", prefixSize);
    return false;
  }
  auto readBuffer = makeUniqueNoThrow<uint8_t[]>(chunkSize);
  if (!readBuffer) {
    LOG_ERR("ZIP", "Failed to allocate %zu-byte early-stop input buffer", chunkSize);
    return false;
  }

  ZipPrefixInflateCtx ctx;
  ctx.file = &file;
  ctx.fileRemaining = compressedSize;
  ctx.readBuf = readBuffer.get();
  ctx.readBufSize = chunkSize;
  ctx.reader.init(false);  // one-shot output history lives in prefix; no 32KB ring
  ctx.reader.setReadCallback(zipPrefixReadCallback);

  size_t produced = 0;
  const InflateStatus status = ctx.reader.readAtMost(prefix.get(), prefixSize, &produced);
  if (status == InflateStatus::Error) {
    LOG_ERR("ZIP", "Early-stop decompression failed");
    return false;
  }

  if (out.write(prefix.get(), produced) != produced) return true;  // sink has what it needs
  if (status == InflateStatus::Done) {
    if (produced != inflatedSize) {
      LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", inflatedSize, produced);
      return false;
    }
    return true;
  }

  LOG_DBG("ZIP", "Early-stop sink needs more than %zu bytes", prefixSize);
  return false;
}
}  // namespace

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  // Phase 1: Try scanning from cursor position first
  uint32_t startPos = lastCentralDirPosValid ? lastCentralDirPos : zipDetails.centralDirOffset;
  bool wrapped = false;
  bool found = false;

  if (!file.seek(startPos)) return false;
  char itemName[256];

  uint16_t visited = 0;
  while (visited < zipDetails.totalEntries) {
    const uint32_t entryStart = file.position();
    CentralEntry entry;
    if (!readCentralEntry(entry, itemName, sizeof(itemName))) {
      // End of central directory
      if (!wrapped && lastCentralDirPosValid && startPos != zipDetails.centralDirOffset) {
        // Wrap around to beginning
        file.seek(zipDetails.centralDirOffset);
        wrapped = true;
        continue;
      }
      break;
    }
    ++visited;

    // If we've wrapped and reached our start position, stop
    if (wrapped && entryStart >= startPos) {
      break;
    }

    if (entry.nameLength < sizeof(itemName)) {
      if (strcmp(itemName, filename) == 0) {
        fileStat->method = entry.method;
        fileStat->compressedSize = entry.compressedSize;
        fileStat->uncompressedSize = entry.uncompressedSize;
        fileStat->localHeaderOffset = entry.localHeaderOffset;
        lastCentralDirPos = file.position();
        lastCentralDirPosValid = true;
        found = true;
        break;
      }
    }
  }

  return found;
}

bool ZipFile::readCentralEntry(CentralEntry& entry, char* name, const size_t nameCapacity) {
  constexpr size_t CENTRAL_HEADER_SIZE = 46;
  uint8_t header[CENTRAL_HEADER_SIZE];
  if (file.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) ||
      zipParsing::readLe32(header) != 0x02014b50) {
    return false;
  }

  entry.method = zipParsing::readLe16(header + 10);
  entry.crc32 = zipParsing::readLe32(header + 16);
  entry.compressedSize = zipParsing::readLe32(header + 20);
  entry.uncompressedSize = zipParsing::readLe32(header + 24);
  entry.nameLength = zipParsing::readLe16(header + 28);
  const uint16_t extraLength = zipParsing::readLe16(header + 30);
  const uint16_t commentLength = zipParsing::readLe16(header + 32);
  entry.localHeaderOffset = zipParsing::readLe32(header + 42);

  const uint64_t variableLength = static_cast<uint64_t>(entry.nameLength) + extraLength + commentLength;
  if (!zipParsing::rangeWithin(file.position(), variableLength, file.size())) return false;

  if (entry.nameLength < nameCapacity) {
    if (file.read(name, entry.nameLength) != static_cast<int>(entry.nameLength)) return false;
    name[entry.nameLength] = '\0';
  } else if (!file.seekCur(entry.nameLength)) {
    return false;
  }
  return file.seekCur(static_cast<uint32_t>(extraLength) + commentLength);
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const ScopedOpenClose zip{*this};
  if (!zip) return -1;

  constexpr auto localHeaderSize = 30;

  uint8_t pLocalHeader[localHeaderSize];
  const uint64_t fileOffset = fileStat.localHeaderOffset;

  if (!zipParsing::rangeWithin(fileOffset, localHeaderSize, file.size()) || !file.seek(fileOffset)) {
    LOG_ERR("ZIP", "Local file header is outside the archive");
    return -1;
  }
  const size_t read = file.read(pLocalHeader, localHeaderSize);

  if (read != localHeaderSize) {
    LOG_ERR("ZIP", "Something went wrong reading the local header");
    return -1;
  }

  if (zipParsing::readLe32(pLocalHeader) != 0x04034b50 /* ZIP local file header signature */) {
    LOG_ERR("ZIP", "Not a valid zip file header");
    return -1;
  }

  const uint16_t filenameLength = zipParsing::readLe16(pLocalHeader + 26);
  const uint16_t extraOffset = zipParsing::readLe16(pLocalHeader + 28);
  const uint64_t dataOffset = fileOffset + localHeaderSize + filenameLength + extraOffset;
  if (dataOffset > file.size() || dataOffset > static_cast<uint64_t>(std::numeric_limits<long>::max())) {
    LOG_ERR("ZIP", "Local file data offset is outside the archive");
    return -1;
  }
  return static_cast<long>(dataOffset);
}

bool ZipFile::loadZipDetails() {
  if (zipDetails.isSet) {
    return true;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  const size_t fileSize = file.size();
  if (fileSize < 22) {
    LOG_ERR("ZIP", "File too small to be a valid zip");
    return false;  // Minimum EOCD size is 22 bytes
  }

  // We scan the last 1KB (or the whole file if smaller) for the EOCD signature
  // 0x06054b50 is stored as 0x50, 0x4b, 0x05, 0x06 in little-endian
  const size_t scanRange = fileSize > 1024 ? 1024 : fileSize;
  const auto buffer = makeUniqueNoThrow<uint8_t[]>(scanRange);
  if (!buffer) {
    LOG_ERR("ZIP", "Failed to allocate memory for EOCD scan buffer");
    return false;
  }

  if (!file.seek(fileSize - scanRange) || file.read(buffer.get(), scanRange) != static_cast<int>(scanRange)) {
    LOG_ERR("ZIP", "Failed to read EOCD scan range");
    return false;
  }

  // Scan backwards for the signature
  zipParsing::EocdFields fields;
  if (!zipParsing::findEocd(buffer.get(), scanRange, fileSize, fields)) {
    LOG_ERR("ZIP", "EOCD signature not found in zip file");
    return false;
  }

  // Now extract the values we need from the EOCD record
  // Relative positions within EOCD:
  // Offset 10: Total number of entries (2 bytes)
  // Offset 16: Offset of start of central directory with respect to the starting disk number (4 bytes)
  zipDetails.totalEntries = fields.totalEntries;
  zipDetails.centralDirOffset = fields.centralDirOffset;
  zipDetails.isSet = true;
  return true;
}

bool ZipFile::open() {
  if (!Storage.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    // Explicit close() required: member variable persists beyond function scope
    file.close();
  }
  lastCentralDirPos = 0;
  lastCentralDirPosValid = false;
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes) {
  if (targets.empty()) {
    return 0;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return 0;

  if (!loadZipDetails()) return 0;

  if (!file.seek(zipDetails.centralDirOffset)) return 0;

  int matched = 0;
  const int targetCount = static_cast<int>(targets.size());
  char itemName[256];

  for (uint16_t i = 0; i < zipDetails.totalEntries; ++i) {
    CentralEntry entry;
    if (!readCentralEntry(entry, itemName, sizeof(itemName))) return 0;

    if (entry.nameLength < sizeof(itemName)) {
      const uint64_t hash = fnvHash64(itemName, entry.nameLength);
      SizeTarget key = {hash, entry.nameLength, 0};

      auto it = std::lower_bound(targets.begin(), targets.end(), key, [](const SizeTarget& a, const SizeTarget& b) {
        return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
      });

      while (it != targets.end() && it->hash == hash && it->len == entry.nameLength) {
        if (it->index < sizes.size()) {
          sizes[it->index] = entry.uncompressedSize;
          matched++;
        }
        ++it;
      }

      if (matched >= targetCount) {
        break;
      }
    }
  }

  return matched;
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  const ScopedOpenClose zip{*this};
  if (!zip) return nullptr;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return nullptr;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return nullptr;

  const size_t archiveSize = file.size();
  const size_t dataOffset = static_cast<size_t>(fileOffset);
  if (!zipParsing::rangeWithin(dataOffset, fileStat.compressedSize, archiveSize)) {
    LOG_ERR("ZIP", "Compressed data extends beyond the archive");
    return nullptr;
  }

  if (!file.seek(dataOffset)) return nullptr;

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  size_t dataSize = 0;
  if (!zipParsing::checkedOutputSize(inflatedDataSize, trailingNullByte, dataSize)) {
    LOG_ERR("ZIP", "Inflated data size overflow");
    return nullptr;
  }
  const auto data = static_cast<uint8_t*>(malloc(std::max<size_t>(dataSize, 1)));
  if (data == nullptr) {
    LOG_ERR("ZIP", "Failed to allocate memory for output buffer (%zu bytes)", dataSize);
    return nullptr;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const size_t dataRead = file.read(data, inflatedDataSize);

    if (dataRead != inflatedDataSize) {
      LOG_ERR("ZIP", "Failed to read data");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(1024));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      free(data);
      return nullptr;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = 1024;

    // One-shot mode: `data` holds the entire output, so back-references
    // resolve inside it and no 32KB window is allocated.
    InflateStream inflate;
    if (!inflate.init(false)) {
      LOG_ERR("ZIP", "Failed to init inflate stream");
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    inflate.setFill(zipFillCallback, &ctx);

    if (!inflate.read(data, inflatedDataSize)) {
      LOG_ERR("ZIP", "Failed to inflate file");
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    free(fileReadBuffer);

    // Continue out of block with data set
  } else {
    LOG_ERR("ZIP", "Unsupported compression method");
    free(data);
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize, const bool allowEarlyStop) {
  if (chunkSize == 0) {
    LOG_ERR("ZIP", "Chunk size must be non-zero");
    return false;
  }
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  const size_t archiveSize = file.size();
  const size_t dataOffset = static_cast<size_t>(fileOffset);
  if (!zipParsing::rangeWithin(dataOffset, fileStat.compressedSize, archiveSize)) {
    LOG_ERR("ZIP", "Compressed data extends beyond the archive");
    return false;
  }

  if (!file.seek(dataOffset)) return false;
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for buffer");
      return false;
    }

    size_t remaining = inflatedDataSize;
    while (remaining > 0) {
      const size_t dataRead = file.read(buffer, remaining < chunkSize ? remaining : chunkSize);
      if (dataRead == 0) {
        LOG_ERR("ZIP", "Could not read more bytes");
        free(buffer);
        return false;
      }

      if (out.write(buffer, dataRead) != dataRead) {
        free(buffer);
        if (allowEarlyStop) return true;  // sink has what it needs
        LOG_ERR("ZIP", "Failed to write all output bytes to stream");
        return false;
      }
      remaining -= dataRead;
    }

    free(buffer);
    return true;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    if (allowEarlyStop) {
      return streamDeflatedPrefix(file, out, chunkSize, deflatedDataSize, inflatedDataSize);
    }

    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      return false;
    }

    auto* outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!outputBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for output buffer");
      free(fileReadBuffer);
      return false;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = chunkSize;

    InflateStream inflate;
    if (!inflate.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate stream");
      free(outputBuffer);
      free(fileReadBuffer);
      return false;
    }
    inflate.setFill(zipFillCallback, &ctx);

    bool success = false;
    size_t totalProduced = 0;

    while (true) {
      size_t produced;
      const InflateStream::Status status = inflate.readAtMost(outputBuffer, chunkSize, &produced);

      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }

      if (produced > 0) {
        if (out.write(outputBuffer, produced) != produced) {
          LOG_ERR("ZIP", "Failed to write all output bytes to stream");
          break;
        }
      }

      if (status == InflateStream::Status::Done) {
        if (totalProduced != static_cast<size_t>(inflatedDataSize)) {
          LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(inflatedDataSize),
                  totalProduced);
          break;
        }
        LOG_DBG("ZIP", "Decompressed %d bytes into %d bytes", deflatedDataSize, inflatedDataSize);
        success = true;
        break;
      }

      if (status == InflateStream::Status::Error) {
        LOG_ERR("ZIP", "Decompression failed");
        break;
      }
      // InflateStream::Status::Ok: output buffer full, continue
    }

    free(outputBuffer);
    free(fileReadBuffer);
    return success;  // inflate destructor frees the decompressor state + window
  }

  LOG_ERR("ZIP", "Unsupported compression method");
  return false;
}
