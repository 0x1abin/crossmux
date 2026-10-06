#pragma once

#include <string>

#include "CoverStub.h"

class Txt {
 public:
  Txt(std::string path, const std::string&) : path(std::move(path)) {}

  bool load() const { return path.find("load-fail") == std::string::npos; }
  void setupCacheDir() const { Storage.mkdir("/.crosspoint/txt"); }
  std::string getCoverBmpPath() const { return "/.crosspoint/txt/cover.bmp"; }
  bool generateCoverBmp() const {
    ++cover_stub::fullCoverGenerations;
    return cover_stub::writeBmp(getCoverBmpPath());
  }
  std::string getTitle() const { return "Text title"; }

 private:
  std::string path;
};
