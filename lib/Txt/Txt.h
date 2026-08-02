#pragma once

#include <HalStorage.h>

#include <memory>
#include <string>

#include "TxtChapterIndex.h"
#include "TxtEncoding.h"

class Txt {
  std::string filepath;
  std::string cacheBasePath;
  std::string cachePath;
  bool loaded = false;
  size_t fileSize = 0;

 public:
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
