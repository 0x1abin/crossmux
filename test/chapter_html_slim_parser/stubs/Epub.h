#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <string>

class CssParser;

class Epub {
 public:
  std::string cachePath;
  CssParser* css = nullptr;
  struct Item {
    std::string href = "chapter.xhtml";
    std::string anchor;
    int spineIndex = 0;
  };
  const std::string& getCachePath() const { return cachePath; }
  Item getSpineItem(int) const { return {}; }
  Item getTocItem(int) const { return {}; }
  int getTocIndexForSpineIndex(int) const { return -1; }
  int getTocItemsCount() const { return 0; }
  std::string getLanguage() const { return "en"; }
  CssParser* getCssParser() const { return css; }
  template <typename Output>
  bool readItemContentsToStream(const std::string&, Output&, size_t, bool = false) const {
    return false;
  }

  bool extractItemToFile(const std::string&, const std::string&) const { return false; }
};
