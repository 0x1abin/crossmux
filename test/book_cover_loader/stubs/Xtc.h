#pragma once

#include <string>

#include "CoverStub.h"

class Xtc {
 public:
  Xtc(std::string path, const std::string&) : path(std::move(path)) {}

  bool load() const { return path.find("load-fail") == std::string::npos; }
  void setupCacheDir() const { Storage.mkdir("/.crosspoint/xtc"); }
  std::string getThumbBmpPath(int height) const { return "/.crosspoint/xtc/thumb_" + std::to_string(height) + ".bmp"; }
  std::string getCoverBmpPath() const { return "/.crosspoint/xtc/cover.bmp"; }
  bool generateThumbBmp(int height) const { return cover_stub::writeBmp(getThumbBmpPath(height)); }
  bool generateCoverBmp() const {
    ++cover_stub::fullCoverGenerations;
    return cover_stub::writeBmp(getCoverBmpPath());
  }
  std::string getTitle() const { return "XTC title"; }
  std::string getAuthor() const { return "XTC author"; }

 private:
  std::string path;
};
