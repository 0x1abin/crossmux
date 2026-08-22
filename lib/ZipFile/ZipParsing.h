#pragma once

#include <cstddef>
#include <cstdint>

namespace zipParsing {

struct EocdFields {
  uint16_t totalEntries = 0;
  uint32_t centralDirOffset = 0;
  uint32_t centralDirSize = 0;
};

inline uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

inline uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

inline bool rangeWithin(const uint64_t offset, const uint64_t length, const uint64_t total) {
  return offset <= total && length <= total - offset;
}

inline bool findEocd(const uint8_t* buffer, const size_t length, const size_t archiveSize, EocdFields& out) {
  if (!buffer || length < 22 || length > archiveSize) return false;
  for (size_t i = length - 22 + 1; i-- > 0;) {
    if (readLe32(buffer + i) != 0x06054b50) continue;
    const uint16_t commentLength = readLe16(buffer + i + 20);
    if (!rangeWithin(i, 22u + commentLength, length)) continue;
    if (readLe16(buffer + i + 4) != 0 || readLe16(buffer + i + 6) != 0 ||
        readLe16(buffer + i + 8) != readLe16(buffer + i + 10)) {
      continue;  // Multi-disk ZIPs are not supported.
    }
    const uint32_t centralDirSize = readLe32(buffer + i + 12);
    const uint32_t centralDirOffset = readLe32(buffer + i + 16);
    const uint64_t eocdOffset = static_cast<uint64_t>(archiveSize - length) + i;
    if (!rangeWithin(centralDirOffset, centralDirSize, eocdOffset)) continue;
    out.totalEntries = readLe16(buffer + i + 10);
    out.centralDirOffset = centralDirOffset;
    out.centralDirSize = centralDirSize;
    return true;
  }
  return false;
}

inline bool checkedOutputSize(const uint32_t uncompressedSize, const bool trailingNullByte, size_t& out) {
  if (trailingNullByte && uncompressedSize == UINT32_MAX) return false;
  out = static_cast<size_t>(uncompressedSize) + (trailingNullByte ? 1u : 0u);
  return true;
}

}  // namespace zipParsing
