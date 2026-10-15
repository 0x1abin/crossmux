#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <string>
#include <vector>

class CssParser;

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
  std::string getLanguage() const { return "en"; }
  CssParser* getCssParser() const { return css; }
  template <typename Output>
  bool readItemContentsToStream(const std::string&, Output&, size_t, bool = false) const {
    return false;
  }

  bool extractItemToFile(const std::string&, const std::string&) const { return false; }
};
