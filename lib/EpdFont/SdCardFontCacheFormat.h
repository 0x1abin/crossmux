#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sd_card_font_cache_format {

constexpr size_t HEADER_AREA_SIZE = 4096;
constexpr uint16_t VERSION = 1;
constexpr uint8_t MAGIC[8] = {'C', 'P', 'S', 'D', 'F', 'C', '1', '\0'};

struct Header {
  uint8_t magic[8];
  uint16_t version;
  uint16_t headerSize;
  uint32_t payloadSize;
  uint32_t contentHash;
  uint32_t payloadCrc;
  uint32_t headerCrc;
  char sourcePath[128];
};
static_assert(sizeof(Header) == 156, "SD-card font cache header layout changed");

inline uint32_t crc32Update(uint32_t crc, const void* data, size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  return crc;
}

inline uint32_t headerCrc(const Header& header) {
  Header copy = header;
  copy.headerCrc = 0;
  return crc32Update(UINT32_MAX, &copy, sizeof(copy)) ^ UINT32_MAX;
}

inline bool isHeaderValid(const Header& header, size_t payloadCapacity) {
  return memcmp(header.magic, MAGIC, sizeof(MAGIC)) == 0 && header.version == VERSION &&
         header.headerSize == sizeof(Header) && header.payloadSize > 0 && header.payloadSize <= payloadCapacity &&
         memchr(header.sourcePath, '\0', sizeof(header.sourcePath)) != nullptr && header.headerCrc == headerCrc(header);
}

inline bool containsPayloadRange(size_t payloadSize, size_t offset, size_t length) {
  return offset <= payloadSize && length <= payloadSize - offset;
}

}  // namespace sd_card_font_cache_format
