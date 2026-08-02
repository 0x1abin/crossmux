#include "Txt.h"

#include <FsHelpers.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

namespace {
constexpr uint32_t CHAPTER_INDEX_MAGIC = 0x43545854;  // "TXTC"
constexpr uint8_t CHAPTER_INDEX_VERSION = 1;
constexpr size_t INDEX_YIELD_BYTES = 64 * 1024;

struct ChapterIndexHeader {
  uint32_t magic = CHAPTER_INDEX_MAGIC;
  uint32_t fileSize = 0;
  uint32_t count = 0;
  uint16_t recordSize = sizeof(txt_chapter_index::Record);
  uint8_t version = CHAPTER_INDEX_VERSION;
  txt_encoding::Encoding encoding = txt_encoding::Encoding::Unknown;
};
static_assert(sizeof(ChapterIndexHeader) == 16);

bool encodingMatches(const txt_encoding::Encoding expected, const txt_encoding::Encoding cached) {
  return expected == txt_encoding::Encoding::Unknown || cached == txt_encoding::Encoding::Unknown || expected == cached;
}

txt_encoding::Encoding detectEncoding(HalFile& source, uint8_t* buffer, const size_t capacity, const size_t fileSize) {
  size_t sourceOffset = 0;
  while (sourceOffset < fileSize) {
    const size_t chunkSize = std::min(capacity, fileSize - sourceOffset);
    const int bytesRead = source.read(buffer, chunkSize);
    if (bytesRead <= 0) break;
    for (int i = 0; i < bytesRead; ++i) {
      if (buffer[i] < 0x80) continue;
      const size_t nonAsciiOffset = sourceOffset + static_cast<size_t>(i);
      const size_t sampleSize = std::min(capacity, fileSize - nonAsciiOffset);
      if (!source.seek(nonAsciiOffset) || source.read(buffer, sampleSize) != static_cast<int>(sampleSize)) {
        return txt_encoding::Encoding::Utf8;
      }
      return txt_encoding::detect(buffer, sampleSize, nonAsciiOffset + sampleSize == fileSize);
    }
    sourceOffset += static_cast<size_t>(bytesRead);
  }
  return txt_encoding::Encoding::Unknown;
}
}  // namespace

Txt::Txt(std::string path, std::string cacheBasePath)
    : filepath(std::move(path)), cacheBasePath(std::move(cacheBasePath)) {
  // Generate cache path from file path hash
  const size_t hash = std::hash<std::string>{}(filepath);
  cachePath = this->cacheBasePath + "/txt_" + std::to_string(hash);
}

bool Txt::load() {
  if (loaded) {
    return true;
  }

  if (!Storage.exists(filepath.c_str())) {
    LOG_ERR("TXT", "File does not exist: %s", filepath.c_str());
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("TXT", filepath, file)) {
    LOG_ERR("TXT", "Failed to open file: %s", filepath.c_str());
    return false;
  }

  fileSize = file.size();
  file.close();

  loaded = true;
  LOG_DBG("TXT", "Loaded TXT file: %s (%zu bytes)", filepath.c_str(), fileSize);
  return true;
}

std::string Txt::getTitle() const {
  // Extract filename without path and extension
  size_t lastSlash = filepath.find_last_of('/');
  std::string filename = (lastSlash != std::string::npos) ? filepath.substr(lastSlash + 1) : filepath;

  // Remove .txt extension
  if (FsHelpers::hasTxtExtension(filename)) {
    filename.resize(filename.length() - 4);
  }

  return filename;
}

void Txt::setupCacheDir() const {
  if (!Storage.exists(cacheBasePath.c_str())) {
    Storage.mkdir(cacheBasePath.c_str());
  }
  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  }
}

std::string Txt::findCoverImage() const {
  // Get the folder containing the txt file
  size_t lastSlash = filepath.find_last_of('/');
  std::string folder = (lastSlash != std::string::npos) ? filepath.substr(0, lastSlash) : "";
  if (folder.empty()) {
    folder = "/";
  }

  // Get the base filename without extension (e.g., "mybook" from "/books/mybook.txt")
  std::string baseName = getTitle();

  // Image extensions to try
  const char* extensions[] = {".bmp", ".jpg", ".jpeg", ".png", ".BMP", ".JPG", ".JPEG", ".PNG"};

  // First priority: look for image with same name as txt file (e.g., mybook.jpg)
  for (const auto& ext : extensions) {
    std::string coverPath = folder + "/" + baseName + ext;
    if (Storage.exists(coverPath.c_str())) {
      LOG_DBG("TXT", "Found matching cover image: %s", coverPath.c_str());
      return coverPath;
    }
  }

  // Fallback: look for cover image files
  const char* coverNames[] = {"cover", "Cover", "COVER"};
  for (const auto& name : coverNames) {
    for (const auto& ext : extensions) {
      std::string coverPath = folder + "/" + std::string(name) + ext;
      if (Storage.exists(coverPath.c_str())) {
        LOG_DBG("TXT", "Found fallback cover image: %s", coverPath.c_str());
        return coverPath;
      }
    }
  }

  return "";
}

std::string Txt::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

bool Txt::generateCoverBmp() const {
  // Already generated, return true
  if (Storage.exists(getCoverBmpPath().c_str())) {
    return true;
  }

  std::string coverImagePath = findCoverImage();
  if (coverImagePath.empty()) {
    LOG_DBG("TXT", "No cover image found for TXT file");
    return false;
  }

  // Setup cache directory
  setupCacheDir();

  if (FsHelpers::hasBmpExtension(coverImagePath)) {
    // Copy BMP file to cache
    LOG_DBG("TXT", "Copying BMP cover image to cache");
    HalFile src, dst;
    if (!Storage.openFileForRead("TXT", coverImagePath, src)) {
      return false;
    }
    if (!Storage.openFileForWrite("TXT", getCoverBmpPath(), dst)) {
      return false;
    }
    uint8_t buffer[1024];
    while (src.available()) {
      size_t bytesRead = src.read(buffer, sizeof(buffer));
      dst.write(buffer, bytesRead);
    }
    LOG_DBG("TXT", "Copied BMP cover to cache");
    return true;
  } else if (FsHelpers::hasJpgExtension(coverImagePath)) {
    // Convert JPG/JPEG to BMP (same approach as Epub)
    LOG_DBG("TXT", "Generating BMP from JPG cover image");
    HalFile coverJpg, coverBmp;
    if (!Storage.openFileForRead("TXT", coverImagePath, coverJpg)) {
      return false;
    }
    if (!Storage.openFileForWrite("TXT", getCoverBmpPath(), coverBmp)) {
      return false;
    }
    const bool success = JpegToBmpConverter::jpegFileToBmpStream(coverJpg, coverBmp);

    if (!success) {
      LOG_ERR("TXT", "Failed to generate BMP from JPG cover image");
      Storage.remove(getCoverBmpPath().c_str());
    } else {
      LOG_DBG("TXT", "Generated BMP from JPG cover image");
    }
    return success;
  }

  // PNG files are not supported (would need a PNG decoder)
  LOG_ERR("TXT", "Cover image format not supported (only BMP/JPG/JPEG)");
  return false;
}

bool Txt::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) {
    LOG_DBG("TXT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.removeDir(cachePath.c_str())) {
    LOG_ERR("TXT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("TXT", "Cache cleared successfully");
  return true;
}

bool Txt::readContent(uint8_t* buffer, size_t offset, size_t length) const {
  if (!loaded) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("TXT", filepath, file)) {
    return false;
  }

  if (!file.seek(offset)) {
    return false;
  }

  size_t bytesRead = file.read(buffer, length);
  return bytesRead > 0;
}

bool Txt::openChapterIndex(HalFile& file, txt_encoding::Encoding& encoding, uint32_t& count) const {
  count = 0;
  const std::string path = cachePath + "/chapters.bin";
  if (!Storage.openFileForRead("TXT", path, file)) return false;

  ChapterIndexHeader header;
  if (file.read(&header, sizeof(header)) != static_cast<int>(sizeof(header)) || header.magic != CHAPTER_INDEX_MAGIC ||
      header.version != CHAPTER_INDEX_VERSION || header.recordSize != sizeof(txt_chapter_index::Record) ||
      header.fileSize != fileSize || !txt_encoding::isSerializedValueValid(static_cast<uint8_t>(header.encoding)) ||
      !txt_encoding::isSupported(header.encoding) || !encodingMatches(encoding, header.encoding) ||
      header.count > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return false;
  }

  const uint64_t expectedSize =
      sizeof(header) + static_cast<uint64_t>(header.count) * sizeof(txt_chapter_index::Record);
  if (file.fileSize64() != expectedSize) return false;

  uint32_t previousOffset = 0;
  for (uint32_t i = 0; i < header.count; ++i) {
    txt_chapter_index::Record chapter;
    if (!readChapter(file, header.count, i, chapter) || (i > 0 && chapter.sourceOffset <= previousOffset) ||
        txt_chapter_index::chapterTitle(chapter.title).empty()) {
      return false;
    }
    previousOffset = chapter.sourceOffset;
  }
  if (encoding == txt_encoding::Encoding::Unknown) encoding = header.encoding;
  count = header.count;
  return true;
}

bool Txt::buildChapterIndex(txt_encoding::Encoding& encoding, uint8_t* scratch, const size_t scratchSize,
                            uint32_t& count) const {
  count = 0;
  if (!loaded || !scratch || scratchSize < 1024 || fileSize > std::numeric_limits<uint32_t>::max()) return false;

  setupCacheDir();
  const std::string finalPath = cachePath + "/chapters.bin";
  const std::string tempPath = finalPath + ".tmp";
  const std::string backupPath = finalPath + ".bak";
  if (Storage.exists(tempPath.c_str())) Storage.remove(tempPath.c_str());

  HalFile source;
  if (!Storage.openFileForRead("TXT", filepath, source)) return false;

  const size_t readCapacity = scratchSize / 2;
  uint8_t* const lineBuffer = scratch + readCapacity;
  const size_t lineCapacity = scratchSize - readCapacity;
  if (encoding == txt_encoding::Encoding::Unknown) {
    encoding = detectEncoding(source, scratch, readCapacity, fileSize);
    if (!source.seek(0)) return false;
  }
  if (!txt_encoding::isSupported(encoding)) return false;

  ChapterIndexHeader header;
  header.fileSize = static_cast<uint32_t>(fileSize);
  header.encoding = encoding;

  bool writeOk = false;
  {
    HalFile output;
    if (!Storage.openFileForWrite("TXT", tempPath, output) || output.write(&header, sizeof(header)) != sizeof(header)) {
      return false;
    }

    size_t lineLength = 0;
    size_t lineStartOffset = 0;
    size_t sourceOffset = 0;
    bool lineTooLong = false;

    const auto appendChapter = [&]() {
      if (lineTooLong || lineLength == 0) return true;

      size_t utf8Length = lineLength;
      if (encoding == txt_encoding::Encoding::Gbk) {
        const auto converted = txt_encoding::transcodeGbkInPlace(lineBuffer, lineLength, lineCapacity, true);
        if (converted.rawLength == 0 || converted.utf8Length == 0) return true;
        utf8Length = converted.utf8Length;
      } else {
        lineBuffer[utf8Length] = '\0';
      }

      const auto title =
          txt_chapter_index::chapterTitle(std::string_view(reinterpret_cast<const char*>(lineBuffer), utf8Length));
      if (title.empty()) return true;

      txt_chapter_index::Record chapter;
      chapter.sourceOffset = static_cast<uint32_t>(lineStartOffset);
      memcpy(chapter.title, title.data(), title.size());
      chapter.title[title.size()] = '\0';
      if (header.count >= static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
          output.write(&chapter, sizeof(chapter)) != sizeof(chapter)) {
        return false;
      }
      ++header.count;
      return true;
    };

    while (sourceOffset < fileSize) {
      const size_t chunkSize = std::min(readCapacity, fileSize - sourceOffset);
      const int bytesRead = source.read(scratch, chunkSize);
      if (bytesRead <= 0) break;
      for (int i = 0; i < bytesRead; ++i) {
        const uint8_t value = scratch[i];
        if (value == '\n') {
          if (!appendChapter()) return false;
          lineLength = 0;
          lineTooLong = false;
          lineStartOffset = sourceOffset + static_cast<size_t>(i) + 1;
        } else if (!lineTooLong) {
          if (lineLength + 1 < txt_chapter_index::TITLE_CAPACITY) {
            lineBuffer[lineLength++] = value;
          } else {
            lineTooLong = true;
          }
        }
      }
      sourceOffset += static_cast<size_t>(bytesRead);
      if (sourceOffset % INDEX_YIELD_BYTES < static_cast<size_t>(bytesRead)) vTaskDelay(1);
    }

    if (sourceOffset != fileSize || !appendChapter()) return false;
    if (!output.seek(0) || output.write(&header, sizeof(header)) != sizeof(header)) return false;
    output.flush();
    writeOk = true;
  }

  if (!writeOk) return false;
  if (Storage.exists(backupPath.c_str())) {
    if (Storage.exists(finalPath.c_str())) {
      Storage.remove(backupPath.c_str());
    } else if (!Storage.rename(backupPath.c_str(), finalPath.c_str())) {
      return false;
    }
  }
  const bool hadFinal = Storage.exists(finalPath.c_str());
  if (hadFinal && !Storage.rename(finalPath.c_str(), backupPath.c_str())) return false;
  if (!Storage.rename(tempPath.c_str(), finalPath.c_str())) {
    if (hadFinal) Storage.rename(backupPath.c_str(), finalPath.c_str());
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (hadFinal) Storage.remove(backupPath.c_str());
  count = header.count;
  LOG_DBG("TXT", "Built TXT chapter index: %u entries", static_cast<unsigned>(count));
  return true;
}

bool Txt::readChapter(HalFile& file, const uint32_t count, const uint32_t index,
                      txt_chapter_index::Record& chapter) const {
  if (index >= count) return false;
  const uint64_t position = sizeof(ChapterIndexHeader) + static_cast<uint64_t>(index) * sizeof(chapter);
  if (position > std::numeric_limits<size_t>::max() || !file.seek(static_cast<size_t>(position)) ||
      file.read(&chapter, sizeof(chapter)) != static_cast<int>(sizeof(chapter)) || chapter.sourceOffset >= fileSize ||
      memchr(chapter.title, '\0', sizeof(chapter.title)) == nullptr) {
    return false;
  }
  return true;
}

bool Txt::findChapterForOffset(HalFile& file, const uint32_t count, const uint32_t sourceOffset,
                               uint32_t& chapterIndex) const {
  if (count == 0) return false;
  uint32_t first = 0;
  uint32_t last = count;
  while (first < last) {
    const uint32_t middle = first + (last - first) / 2;
    txt_chapter_index::Record chapter;
    if (!readChapter(file, count, middle, chapter)) return false;
    if (chapter.sourceOffset <= sourceOffset) {
      first = middle + 1;
    } else {
      last = middle;
    }
  }
  chapterIndex = first == 0 ? 0 : first - 1;
  return true;
}
