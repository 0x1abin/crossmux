#pragma once

#include <string>
#include <utility>
#include <vector>

class BookMetadataCache {
 public:
  std::vector<std::pair<std::string, bool>> entries;
  void createSpineEntry(const std::string& href, const bool linear = true) { entries.emplace_back(href, linear); }
};
