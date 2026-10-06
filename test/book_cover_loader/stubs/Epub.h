#pragma once

#include <string>

#include "CoverStub.h"

class Epub {
 public:
  Epub(std::string path, const std::string&) : path(std::move(path)) {}

  bool load(bool buildIfMissing, bool) const { return buildIfMissing && path.find("load-fail") == std::string::npos; }
  void setupCacheDir() const { Storage.mkdir("/.crosspoint/epub"); }
  bool hasCoverOverride() const {
    HalFile file;
    uint8_t prefix[8] = {};
    if (!Storage.openFileForRead("TEST", "/.crosspoint/epub/cover.override", file) ||
        file.read(prefix, sizeof(prefix)) != static_cast<int>(sizeof(prefix))) {
      return false;
    }
    static constexpr uint8_t png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return memcmp(prefix, png, sizeof(png)) == 0 || (prefix[0] == 0xFF && prefix[1] == 0xD8 && prefix[2] == 0xFF);
  }
  std::string getThumbBmpPath(int height) const { return "/.crosspoint/epub/thumb_" + std::to_string(height) + ".bmp"; }
  std::string getCoverBmpPath(bool = false) const { return "/.crosspoint/epub/cover.bmp"; }
  bool generateThumbBmp(int height) const {
    ++cover_stub::epubThumbnailGenerations;
    const std::string output = getThumbBmpPath(height);
    if (path.find("coverless") == std::string::npos) return cover_stub::writeBmp(output);
    if (hasCoverOverride()) {
      ++cover_stub::overrideConversions;
      return cover_stub::writeBmp(output);
    }
    return cover_stub::writeEmpty(output) && false;
  }
  bool generateCoverBmp(bool = false) const {
    ++cover_stub::fullCoverGenerations;
    if (path.find("coverless") != std::string::npos) {
      if (!hasCoverOverride()) return false;
      ++cover_stub::overrideConversions;
    }
    return cover_stub::writeBmp(getCoverBmpPath());
  }
  const std::string& getTitle() const {
    static const std::string title = "EPUB title";
    return title;
  }
  const std::string& getAuthor() const {
    static const std::string author = "EPUB author";
    return author;
  }

 private:
  std::string path;
};
