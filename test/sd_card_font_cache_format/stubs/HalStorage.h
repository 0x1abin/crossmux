#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace font_cache_test {
inline std::array<uint8_t, 64> sourceHeader = {'C', 'P', 'F', 'O', 'N', 'T', 0, 0, 4, 0, 0, 0, 1};
inline size_t sourceSize = 64;
inline bool sourceExists = true;
}  // namespace font_cache_test

class HalFile {
 public:
  size_t fileSize() const { return font_cache_test::sourceSize; }
  int read(void* output, size_t length) {
    using namespace font_cache_test;
    length = std::min(length, sourceSize - position_);
    std::memset(output, 0, length);
    if (position_ < sourceHeader.size()) {
      std::memcpy(output, sourceHeader.data() + position_, std::min(length, sourceHeader.size() - position_));
    }
    position_ += length;
    return static_cast<int>(length);
  }

 private:
  size_t position_ = 0;
};

struct TestStorage {
  bool openFileForRead(const char*, const char*, HalFile&) const { return font_cache_test::sourceExists; }
};
inline TestStorage Storage;
