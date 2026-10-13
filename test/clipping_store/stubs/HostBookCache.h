#pragma once
#include <HalStorage.h>

#include <functional>
struct HostBookCache {
  std::string path;
  HostBookCache(const std::string& book, const char* type)
      : path(std::string("/.crosspoint/") + type + std::to_string(std::hash<std::string>{}(book))) {}
  std::string getCachePath() const { return path; }
  bool clearCache() const { return !Storage.exists(path.c_str()) || Storage.removeDir(path.c_str()); }
};
