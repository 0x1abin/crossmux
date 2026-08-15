#pragma once

#include <cstddef>

class HalFile {
 public:
  int read(void*, size_t) { return 0; }
  size_t write(const void*, size_t) { return 0; }
  size_t position() const { return 0; }
  size_t size() const { return 0; }
};
