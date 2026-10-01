#pragma once

#include <string>

#include "Epub.h"

namespace serialization {
constexpr size_t MAX_TEXT_BYTES = 8192;
constexpr size_t MAX_PATH_BYTES = 4096;

inline void writeString(HalFile&, const std::string&) {}
inline bool readString(HalFile&, std::string& out, size_t = MAX_TEXT_BYTES) {
  out.clear();
  return false;
}

}  // namespace serialization
