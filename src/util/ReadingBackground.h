#pragma once

#include <cstddef>
#include <cstdint>

class GfxRenderer;

namespace readingBackground {

constexpr uint32_t CACHE_MAGIC = 0x47425243;  // "CRBG"
constexpr uint16_t CACHE_VERSION = 1;
constexpr uint16_t ORIENTATION_COUNT = 4;
constexpr const char* CACHE_DIR = "/.crosspoint/background";
constexpr const char* CACHE_PATH = "/.crosspoint/background/reading_bg.bin";

#pragma pack(push, 1)
struct CacheHeader {
  uint32_t magic = CACHE_MAGIC;
  uint16_t version = CACHE_VERSION;
  uint16_t orientationCount = ORIENTATION_COUNT;
  uint16_t displayWidth = 0;
  uint16_t displayHeight = 0;
  uint32_t frameSize = 0;
  uint16_t reserved = 0;
};
#pragma pack(pop)
static_assert(sizeof(CacheHeader) == 18);

constexpr uint64_t frameOffset(const uint8_t orientation, const uint32_t frameSize) {
  return sizeof(CacheHeader) + static_cast<uint64_t>(orientation) * frameSize;
}

constexpr bool isValidOrientation(const uint8_t orientation) { return orientation < ORIENTATION_COUNT; }

constexpr uint64_t cacheFileSize(const uint32_t frameSize) { return frameOffset(ORIENTATION_COUNT, frameSize); }

constexpr bool isValidHeader(const CacheHeader& header, const uint16_t displayWidth, const uint16_t displayHeight,
                             const uint32_t frameSize, const uint64_t fileSize) {
  return header.magic == CACHE_MAGIC && header.version == CACHE_VERSION &&
         header.orientationCount == ORIENTATION_COUNT && header.displayWidth == displayWidth &&
         header.displayHeight == displayHeight && header.frameSize == frameSize && header.reserved == 0 &&
         fileSize == cacheFileSize(frameSize);
}

bool createCache(GfxRenderer& renderer, const char* bitmapPath);
bool createCacheFromPng(GfxRenderer& renderer, const char* pngPath);
bool load(GfxRenderer& renderer);

}  // namespace readingBackground
