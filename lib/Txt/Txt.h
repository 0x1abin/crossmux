#pragma once

#include <HalStorage.h>
#include <Print.h>

#include <string_view>
class BookMetadataCache;

#include <memory>
#include <string>

#include "TxtChapterIndex.h"
#include "TxtEncoding.h"
#include "TxtProgress.h"

class Txt {
  std::string filepath;
  std::string cacheBasePath;
  std::string cachePath;
  bool loaded = false;
  size_t fileSize = 0;

 public:
  static bool isTxtOrMd(std::string_view path);
  static bool validateCache(const std::string& filepath, const std::string& cachePath, size_t cachedSize);
  static void invalidateCache(const std::string& cachePath);
  static bool streamTxtToHtml(const std::string& filepath, Print& out, const std::string& cachePath);
  static int tocIndexForPosition(const std::string& filepath, const std::string& cachePath, uint32_t visibleOffset,
                                 uint8_t* chapterProgress = nullptr);
  static bool resolveChapterPosition(const std::string& filepath, const std::string& cachePath, std::string_view anchor,
                                     uint32_t& visibleOffset);
  static txt_progress::LegacyResult restoreLegacyProgress(const std::string& filepath, const std::string& cachePath,
                                                          uint32_t& visibleOffset);
  static std::string findCompanionCoverImage(const std::string& filepath);
  static bool convertCoverImageToBmp(const std::string& imagePath, const std::string& destBmpPath, int thumbHeight = 0,
                                     bool cropped = false, bool originalThresholds = false);
  static bool buildTxtCache(const std::string& filepath, const std::string& cachePath,
                            std::unique_ptr<BookMetadataCache>& bookMetadataCache);
  explicit Txt(std::string path, std::string cacheBasePath);

  bool load();
  [[nodiscard]] const std::string& getPath() const { return filepath; }
  [[nodiscard]] const std::string& getCachePath() const { return cachePath; }
  [[nodiscard]] std::string getTitle() const;
  [[nodiscard]] size_t getFileSize() const { return fileSize; }

  void setupCacheDir() const;
  bool clearCache() const;

  // Cover image support - looks for cover.bmp/jpg/jpeg/png in same folder as txt file
  [[nodiscard]] std::string getCoverBmpPath() const;
  [[nodiscard]] bool generateCoverBmp() const;
  [[nodiscard]] std::string findCoverImage() const;

  // Read content from file
  [[nodiscard]] bool readContent(uint8_t* buffer, size_t offset, size_t length) const;

  // Disk-backed chapter index. Titles are read one record at a time so chapter
  // count does not affect steady-state RAM usage.
  [[nodiscard]] bool openChapterIndex(HalFile& file, txt_encoding::Encoding& encoding, uint32_t& count) const;
  [[nodiscard]] bool buildChapterIndex(txt_encoding::Encoding& encoding, uint8_t* scratch, size_t scratchSize,
                                       uint32_t& count) const;
  [[nodiscard]] bool readChapter(HalFile& file, uint32_t count, uint32_t index,
                                 txt_chapter_index::Record& chapter) const;
  [[nodiscard]] bool findChapterForOffset(HalFile& file, uint32_t count, uint32_t sourceOffset,
                                          uint32_t& chapterIndex) const;
};
