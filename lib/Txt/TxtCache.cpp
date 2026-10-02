#include <BufferedFile.h>
#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <Utf8.h>

#include <cstring>
#include <limits>

#include "Txt.h"
#include "TxtProgress.h"
#include "TxtToHtml.h"

namespace {
bool openChapters(Txt& txt, HalFile& chapters, txt_encoding::Encoding& encoding, uint32_t& count) {
  if (!txt.load()) return false;
  if (txt.openChapterIndex(chapters, encoding, count)) return true;
  chapters.close();  // The invalid index must close before replacement/reopen.
  // One 8 KiB indexing workspace, released before allocating conversion buffers.
  auto scratch = makeUniqueNoThrow<uint8_t[]>(8192);
  if (!scratch) {
    LOG_ERR("TXT", "OOM: chapter-index workspace (8192 bytes)");
    return false;
  }
  return txt.buildChapterIndex(encoding, scratch.get(), 8192, count) && txt.openChapterIndex(chapters, encoding, count);
}

struct ConversionIndex {
  Txt& txt;
  HalFile& chapters;
  HalFile& mapping;
  txt_progress::Header header;
  txt_chapter_index::Record chapter;
  uint32_t chapterCount = 0;
  uint32_t nextChapter = 0;
  uint8_t lastWidth = 255;
  uint8_t lastStep = 255;

  bool advance() {
    if (nextChapter == chapterCount) {
      chapter.sourceOffset = UINT32_MAX;
      return true;
    }
    return txt.readChapter(chapters, chapterCount, nextChapter++, chapter);
  }
};
}  // namespace

bool Txt::isTxtOrMd(std::string_view path) {
  return FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path);
}

std::string Txt::findCompanionCoverImage(const std::string& filepath) {
  std::string folder = FsHelpers::extractFolderPath(filepath);

  std::string baseName = FsHelpers::getFileNameWithoutExtension(filepath);
  const char* extensions[] = {".bmp", ".jpg", ".jpeg", ".png", ".BMP", ".JPG", ".JPEG", ".PNG"};

  for (const auto& ext : extensions) {
    std::string coverPath = folder + "/" + baseName + ext;
    if (Storage.exists(coverPath.c_str())) return coverPath;
  }

  const char* coverNames[] = {"cover", "Cover", "COVER"};
  for (const auto& name : coverNames) {
    for (const auto& ext : extensions) {
      std::string coverPath = folder + "/" + std::string(name) + ext;
      if (Storage.exists(coverPath.c_str())) return coverPath;
    }
  }

  return "";
}

bool Txt::convertCoverImageToBmp(const std::string& imagePath, const std::string& destBmpPath, int thumbHeight,
                                 bool cropped, bool originalThresholds) {
  if (!Storage.exists(imagePath.c_str())) return false;

  const bool isBmp = FsHelpers::hasBmpExtension(imagePath);
  const bool isJpg = FsHelpers::hasJpgExtension(imagePath);
  const bool isPng = FsHelpers::hasPngExtension(imagePath);
  if (!isBmp && !isJpg && !isPng) return false;

  HalFile src, dst;
  if (!Storage.openFileForRead("TXT", imagePath, src) || !Storage.openFileForWrite("TXT", destBmpPath, dst)) {
    return false;
  }

  bool success = false;
  if (isBmp) {
    uint8_t buf[128];
    int n;
    success = true;
    while ((n = src.read(buf, sizeof(buf))) > 0) {
      if (dst.write(buf, n) != static_cast<size_t>(n)) {
        success = false;
        break;
      }
    }
    if (n < 0) {
      success = false;
    }
  } else if (thumbHeight > 0) {
    const int targetWidth = thumbHeight * 0.6;
    const int targetHeight = thumbHeight;
    if (isJpg) {
      success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(src, dst, targetWidth, targetHeight);
    } else if (isPng) {
      success = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(src, dst, targetWidth, targetHeight);
    }
  } else {
    if (isJpg) {
      success = JpegToBmpConverter::jpegFileToBmpStream(src, dst, cropped, originalThresholds);
    } else if (isPng) {
      success = PngToBmpConverter::pngFileToBmpStream(src, dst, cropped, originalThresholds);
    }
  }

  src.close();
  dst.close();

  if (!success) {
    Storage.remove(destBmpPath.c_str());
  }

  return success;
}

bool Txt::streamTxtToHtml(const std::string& filepath, Print& out, const std::string& cachePath) {
  Txt txt(filepath, "/.crosspoint");
  HalFile source, chapters;
  txt_encoding::Encoding encoding = txt_encoding::Encoding::Unknown;
  uint32_t chapterCount = 0;
  if (!openChapters(txt, chapters, encoding, chapterCount) || txt.getFileSize() >= UINT32_MAX ||
      !Storage.openFileForRead("TXT", filepath, source))
    return false;
  const std::string mapPath = cachePath + "/txt-map.bin";
  const std::string temporary = mapPath + ".tmp";
  bool converted = false;
  {
    HalFile mapping;
    if (!Storage.openFileForWrite("TXT", temporary, mapping)) return false;
    ConversionIndex index{txt, chapters, mapping};
    index.chapterCount = chapterCount;
    index.header.sourceSize = static_cast<uint32_t>(txt.getFileSize());
    index.header.encoding = static_cast<uint8_t>(encoding);
    if (!index.advance() || mapping.write(&index.header, sizeof(index.header)) != sizeof(index.header)) return false;
    TxtToHtml::Options options;
    options.encoding = encoding;
    options.context = &index;
    options.chapterOffset = [](void* context) { return static_cast<ConversionIndex*>(context)->chapter.sourceOffset; };
    options.nextChapter = [](void* context) { return static_cast<ConversionIndex*>(context)->advance(); };
    options.map = [](void* context, const uint32_t source, const uint32_t visible, const uint8_t width,
                     const uint8_t step) {
      auto& index = *static_cast<ConversionIndex*>(context);
      if (width == index.lastWidth && step == index.lastStep) return true;
      // Run-length records keep long ASCII/GBK spans compact; alternating widths remain disk-backed.
      if (index.mapping.position() > UINT32_MAX - sizeof(txt_progress::Record)) return false;
      const txt_progress::Record record{source, visible, width, step};
      if (index.mapping.write(&record, sizeof(record)) != sizeof(record)) return false;
      ++index.header.count;
      index.lastWidth = width;
      index.lastStep = step;
      return true;
    };
    converted = TxtToHtml::stream(
        filepath, &source,
        [](void* ctx, uint8_t* buf, size_t size) { return static_cast<HalFile*>(ctx)->read(buf, size); }, out, options);
    if (converted) {
      converted = mapping.seek(0) && mapping.write(&index.header, sizeof(index.header)) == sizeof(index.header);
      mapping.flush();
    }
  }
  if (!converted || !Storage.replaceFile(temporary.c_str(), mapPath.c_str())) {
    Storage.remove(temporary.c_str());
    return false;
  }
  return true;
}

namespace {
bool resolveSourcePosition(const std::string& filepath, const std::string& cachePath, uint32_t sourceOffset,
                           uint32_t& visibleOffset) {
  Txt txt(filepath, "/.crosspoint");
  if (!txt.load() || txt.getFileSize() >= UINT32_MAX || sourceOffset > txt.getFileSize()) return false;
  const std::string mapPath = cachePath + "/txt-map.bin";
  const std::string htmlPath = cachePath + "/html/0.html";
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (Storage.exists(htmlPath.c_str())) {
      HalFile mapping;
      if (Storage.openFileForRead("TXT", mapPath, mapping) &&
          txt_progress::resolve(mapping, txt.getFileSize(), sourceOffset, visibleOffset))
        return true;
    }
    if (attempt) break;
    if (!Storage.ensureDirectoryExists((cachePath + "/html").c_str())) break;
    const std::string temporary = htmlPath + ".tmp";
    bool converted = false;
    {
      HalFile output;
      if (!Storage.openFileForWrite("TXT", temporary, output)) break;
      converted = Txt::streamTxtToHtml(filepath, output, cachePath);
      output.flush();
    }
    if (!converted || !Storage.replaceFile(temporary.c_str(), htmlPath.c_str())) {
      Storage.remove(temporary.c_str());
      break;
    }
  }
  return false;
}
}  // namespace

bool Txt::resolveChapterPosition(const std::string& filepath, const std::string& cachePath, std::string_view anchor,
                                 uint32_t& visibleOffset) {
  if (anchor.size() <= 4 || anchor.substr(0, 4) != "txt-") return false;
  uint64_t sourceOffset = 0;
  for (char c : anchor.substr(4)) {
    if (c < '0' || c > '9') return false;
    sourceOffset = sourceOffset * 10 + static_cast<unsigned>(c - '0');
    if (sourceOffset > UINT32_MAX) return false;
  }
  return resolveSourcePosition(filepath, cachePath, static_cast<uint32_t>(sourceOffset), visibleOffset);
}

txt_progress::LegacyResult Txt::restoreLegacyProgress(const std::string& filepath, const std::string& cachePath,
                                                      uint32_t& visibleOffset) {
  Txt txt(filepath, "/.crosspoint");
  if (!txt.load() || txt.getFileSize() >= UINT32_MAX) return txt_progress::LegacyResult::Failed;
  uint32_t sourceOffset = 0;
  const auto legacy = txt_progress::readLegacySource(txt.getCachePath().c_str(), txt.getFileSize(), sourceOffset);
  if (legacy != txt_progress::LegacyResult::Restored) return legacy;
  if (resolveSourcePosition(filepath, cachePath, sourceOffset, visibleOffset)) return legacy;
  LOG_ERR("TXT", "Legacy progress migration failed; original progress retained: %s", filepath.c_str());
  return txt_progress::LegacyResult::Failed;
}

void Txt::invalidateCache(const std::string& cachePath) {
  Storage.removeDir((cachePath + "/html").c_str());
  Storage.removeDir((cachePath + "/sections").c_str());
}

bool Txt::validateCache(const std::string& filepath, const std::string& cachePath, size_t cachedSize) {
  char metadataVersion[4] = {};
  const std::string marker = cachePath + "/txt-cache-version";
  bool valid = Storage.readFileToBuffer(marker.c_str(), metadataVersion, sizeof(metadataVersion)) == 1 &&
               metadataVersion[0] == '2';

  // 1. If html/0.html exists, check its embedded version comment
  const std::string htmlPath = cachePath + "/html/0.html";
  if (Storage.exists(htmlPath.c_str())) {
    HalFile htmlFile;
    if (Storage.openFileForRead("TXT", htmlPath, htmlFile)) {
      char header[80] = {0};
      const int bytesRead = htmlFile.read(header, sizeof(header) - 1);
      if (bytesRead > 0) {
        header[bytesRead] = '\0';
        if (strstr(header, TxtToHtml::cacheVersionTag(filepath)) == nullptr) {
          LOG_DBG("TXT", "HTML cache version mismatch or missing, invalidating: %s", htmlPath.c_str());
          valid = false;
        }
      } else {
        valid = false;
      }
    } else {
      valid = false;
    }
  }

  // 2. Check if source file size changed
  if (valid && cachedSize > 0) {
    HalFile rawFile;
    if (Storage.openFileForRead("TXT", filepath, rawFile)) {
      if (rawFile.fileSize64() != cachedSize) {
        LOG_DBG("TXT", "File size changed (%u vs cached %u), invalidating cache", static_cast<uint32_t>(rawFile.size()),
                static_cast<uint32_t>(cachedSize));
        valid = false;
      }
    } else {
      valid = false;
    }
  }

  if (!valid) {
    LOG_DBG("TXT", "Cache invalid for %s, wiping html and sections", filepath.c_str());
    invalidateCache(cachePath);
  }

  return valid;
}

bool Txt::buildTxtCache(const std::string& filepath, const std::string& cachePath,
                        std::unique_ptr<BookMetadataCache>& bookMetadataCache) {
  LOG_DBG("TXT", "Building metadata cache for TXT: %s", filepath.c_str());

  if (!Storage.exists(cachePath.c_str())) {
    Storage.mkdir(cachePath.c_str());
  } else {
    invalidateCache(cachePath);
  }

  if (!bookMetadataCache->beginWrite()) {
    LOG_ERR("TXT", "Could not begin writing cache");
    return false;
  }

  if (!bookMetadataCache->beginContentOpfPass()) {
    LOG_ERR("TXT", "Could not begin writing content.opf pass");
    return false;
  }

  bookMetadataCache->createSpineEntry("content.html");

  if (!bookMetadataCache->endContentOpfPass()) {
    LOG_ERR("TXT", "Could not end writing content.opf pass");
    return false;
  }

  if (!bookMetadataCache->beginTocPass()) {
    LOG_ERR("TXT", "Could not begin writing toc pass");
    return false;
  }

  const std::string title = FsHelpers::getFileNameWithoutExtension(filepath);
  bookMetadataCache->createTocEntry(title, "content.html", "", 0);
  Txt txt(filepath, "/.crosspoint");
  HalFile chapters;
  txt_encoding::Encoding encoding = txt_encoding::Encoding::Unknown;
  uint32_t count = 0;
  if (!openChapters(txt, chapters, encoding, count) || count >= UINT16_MAX - 1) return false;
  for (uint32_t chapterIndex = 0; chapterIndex < count; ++chapterIndex) {
    txt_chapter_index::Record chapter;
    if (!txt.readChapter(chapters, count, chapterIndex, chapter)) return false;
    char anchor[32];
    snprintf(anchor, sizeof(anchor), "txt-%u", static_cast<unsigned>(chapter.sourceOffset));
    bookMetadataCache->createTocEntry(chapter.title, "content.html", anchor, 0);
  }

  if (!bookMetadataCache->endTocPass()) {
    LOG_ERR("TXT", "Could not end writing toc pass");
    return false;
  }

  if (!bookMetadataCache->endWrite()) {
    LOG_ERR("TXT", "Could not end writing cache");
    return false;
  }

  BookMetadataCache::BookMetadata bookMetadata;
  bookMetadata.title = utf8ComposeNfc(title);
  bookMetadata.language = "en";

  std::string companionCover = findCompanionCoverImage(filepath);
  if (!companionCover.empty()) {
    bookMetadata.coverItemHref = companionCover;
  }

  if (!bookMetadataCache->buildBookBin(filepath, bookMetadata)) {
    LOG_ERR("TXT", "Could not build book.bin for TXT");
    return false;
  }

  bookMetadataCache->cleanupTmpFiles();

  bookMetadataCache = makeUniqueNoThrow<BookMetadataCache>(cachePath);
  if (!bookMetadataCache || !bookMetadataCache->load()) {
    LOG_ERR("TXT", "Failed to reload cache after build");
    return false;
  }

  if (!Storage.writeFile((cachePath + "/txt-cache-version").c_str(), "2")) return false;
  return true;
}
