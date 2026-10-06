#pragma once

#include <HalStorage.h>

#include <array>
#include <string>

namespace cover_stub {
inline int epubThumbnailGenerations = 0;
inline int fullCoverGenerations = 0;
inline int overrideConversions = 0;

inline bool writeBmp(const std::string& path) {
  std::array<uint8_t, 66> bytes{};
  bytes[0] = 'B';
  bytes[1] = 'M';
  bytes[2] = static_cast<uint8_t>(bytes.size());
  bytes[10] = 62;
  bytes[14] = 40;
  bytes[18] = 1;
  bytes[22] = 1;
  bytes[26] = 1;
  bytes[28] = 1;
  bytes[34] = 4;
  bytes[46] = 2;
  bytes[58] = 0xFF;
  bytes[59] = 0xFF;
  bytes[60] = 0xFF;
  HalFile file;
  return Storage.openFileForWrite("TEST", path, file) && file.write(bytes.data(), bytes.size()) == bytes.size();
}

inline bool writeEmpty(const std::string& path) {
  HalFile file;
  return Storage.openFileForWrite("TEST", path, file);
}
}  // namespace cover_stub
