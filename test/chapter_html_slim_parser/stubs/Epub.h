#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
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
  static inline bool enableGifProbeForTest = false;
  std::vector<uint8_t> imageBytes;
  mutable size_t headerProbes = 0;
  mutable size_t fullExtractions = 0;
  template <typename Output>
  bool readItemContentsToStream(const std::string&, Output& output, size_t, bool allowEarlyStop = false) const {
    if (imageBytes.empty()) return false;
    if (allowEarlyStop)
      ++headerProbes;
    else
      ++fullExtractions;
    const size_t written = output.write(imageBytes.data(), imageBytes.size());
    return written == imageBytes.size() || allowEarlyStop;
  }

  bool extractItemToFile(const std::string&, const std::string&) const { return false; }
};
