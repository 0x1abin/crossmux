#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

class HalFile {
 public:
  HalFile() = default;
  HalFile(const uint8_t* data, const size_t size) : data_(data), size_(size) {}

  int read(void* output, const size_t count) {
    if (!data_ || position_ > size_) return 0;
    const size_t readSize = std::min(count, size_ - position_);
    if (readSize > 0) {
      memcpy(output, data_ + position_, readSize);
      position_ += readSize;
    }
    return static_cast<int>(readSize);
  }

  int read() {
    uint8_t value = 0;
    return read(&value, 1) == 1 ? value : -1;
  }

  bool seek(const size_t position) {
    if (position > size_) return false;
    position_ = position;
    return true;
  }

  bool seekCur(const int64_t offset) {
    if (offset < 0) return false;
    const auto forward = static_cast<size_t>(offset);
    return forward <= size_ - position_ && seek(position_ + forward);
  }

  explicit operator bool() const { return data_ != nullptr; }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t position_ = 0;
};
