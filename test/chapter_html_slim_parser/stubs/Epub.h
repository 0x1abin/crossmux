#pragma once

#include <HalStorage.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class CssParser;

// Stand-in for the real cache type: Section.cpp only reads the fields below, and these
// host tests must not pull in the storage/serialization code behind the real header.
class BookMetadataCache {
 public:
  struct TocEntry {
    std::string title;
    std::string href;
    std::string anchor;
    uint8_t level = 0;
    int16_t spineIndex = -1;
  };
};

class Epub {
 public:
  std::string sourcePath = "test.epub";
  const std::string& getPath() const { return sourcePath; }
  std::string cachePath;
  CssParser* css = nullptr;
  struct Item {
    std::string href = "chapter.xhtml";
    std::string anchor;
    int spineIndex = 0;
  };
  const std::string& getCachePath() const { return cachePath; }
  Item getSpineItem(int) const { return {}; }
  std::vector<Item> tocItems;
  Item getTocItem(int index) const { return tocItems.at(index); }
  int getTocIndexForSpineIndex(int spineIndex) const {
    for (size_t i = 0; i < tocItems.size(); ++i) {
      if (tocItems[i].spineIndex == spineIndex) return static_cast<int>(i);
    }
    return -1;
  }
  int getTocItemsCount() const { return static_cast<int>(tocItems.size()); }
  // Section's sub-chapter table reads the TOC this way instead of per-index access.
  bool forEachTocItem(const std::function<bool(int, const BookMetadataCache::TocEntry&)>& fn,
                      const int fromIndex = 0) const {
    for (size_t i = static_cast<size_t>(std::max(0, fromIndex)); i < tocItems.size(); ++i) {
      BookMetadataCache::TocEntry entry;
      entry.title = tocItems[i].href;
      entry.href = tocItems[i].href;
      entry.anchor = tocItems[i].anchor;
      entry.spineIndex = static_cast<int16_t>(tocItems[i].spineIndex);
      if (!fn(static_cast<int>(i), entry)) return true;
    }
    return true;
  }
  std::string getLanguage() const { return "en"; }
  CssParser* getCssParser() const { return css; }
  template <typename Output>
  bool readItemContentsToStream(const std::string&, Output&, size_t, bool = false) const {
    return false;
  }

  bool extractItemToFile(const std::string&, const std::string&) const { return false; }
};
