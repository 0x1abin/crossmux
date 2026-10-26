#pragma once

#include <string>

#include "Epub.h"

namespace serialization {
constexpr size_t MAX_TEXT_BYTES = 8192;
constexpr size_t MAX_PATH_BYTES = 4096;

inline void writeString(HalFile& file, const std::string& text) {
  const uint32_t size = static_cast<uint32_t>(text.size());
  file.write(&size, sizeof(size));
  file.write(text.data(), size);
}
inline bool readString(HalFile& file, std::string& out, const size_t maxSize = MAX_TEXT_BYTES) {
  uint32_t size = 0;
  if (file.read(&size, sizeof(size)) != static_cast<int>(sizeof(size)) || size > maxSize) return false;
  out.resize(size);
  return file.read(out.data(), size) == static_cast<int>(size);
}

}  // namespace serialization
