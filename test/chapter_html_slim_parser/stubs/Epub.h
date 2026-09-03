#pragma once

#include <cstddef>
#include <string>

class Epub {
 public:
  template <typename Output>
  bool readItemContentsToStream(const std::string&, Output&, size_t, bool = false) const {
    return false;
  }

  bool extractItemToFile(const std::string&, const std::string&) const { return false; }
};
