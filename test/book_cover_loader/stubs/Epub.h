#pragma once

#include <string>

#include "CoverStub.h"

class Epub {
 public:
  Epub(std::string path, const std::string&) : path(std::move(path)) {}

  bool load(bool buildIfMissing, bool) const { return buildIfMissing && path.find("load-fail") == std::string::npos; }
  void setupCacheDir() const { Storage.mkdir("/.crosspoint/epub"); }
  std::string getThumbBmpPath(int height) const { return "/.crosspoint/epub/thumb_" + std::to_string(height) + ".bmp"; }
  std::string getCoverBmpPath(bool = false) const { return "/.crosspoint/epub/cover.bmp"; }
  bool generateThumbBmp(int height) const {
    ++cover_stub::epubThumbnailGenerations;
    const std::string output = getThumbBmpPath(height);
    return path.find("coverless") == std::string::npos ? cover_stub::writeBmp(output)
                                                       : (cover_stub::writeEmpty(output) && false);
  }
  bool generateCoverBmp(bool = false) const {
    ++cover_stub::fullCoverGenerations;
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
