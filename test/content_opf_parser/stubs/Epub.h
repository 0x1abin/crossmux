#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class HalFile {
 public:
  explicit operator bool() const { return open_; }
  void close() {
    EXPECT_TRUE(initialized_) << "Closing a default HalFile asserts on device";
    open_ = false;
  }
  bool seek(size_t position) {
    position_ = position;
    return true;
  }
  size_t position() const { return position_; }
  int available() const { return data_ ? static_cast<int>(data_->size() - position_) : 0; }
  size_t write(const void* data, const size_t size) {
    if (!data_) return 0;
    if (position_ + size > data_->size()) data_->resize(position_ + size);
    memcpy(data_->data() + position_, data, size);
    position_ += size;
    return size;
  }
  int read(void* data, const size_t size) {
    if (!data_ || position_ > data_->size() || size > data_->size() - position_) return 0;
    memcpy(data, data_->data() + position_, size);
    position_ += size;
    return static_cast<int>(size);
  }
  void markOpen(std::vector<uint8_t>& data) {
    initialized_ = true;
    open_ = true;
    position_ = 0;
    data_ = &data;
  }

 private:
  bool open_ = false;
  bool initialized_ = false;
  size_t position_ = 0;
  std::vector<uint8_t>* data_ = nullptr;
};

struct TestStorage {
  int writeOpens = 0;
  int readOpens = 0;
  std::vector<uint8_t> data;

  bool openFileForWrite(const char*, const std::string&, HalFile& file) {
    writeOpens++;
    data.clear();
    file.markOpen(data);
    return true;
  }
  bool openFileForRead(const char*, const std::string&, HalFile& file) {
    readOpens++;
    file.markOpen(data);
    return true;
  }
  bool exists(const char*) const { return false; }
  bool remove(const char*) { return true; }
};

inline TestStorage Storage;
