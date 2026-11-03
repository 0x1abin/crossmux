#pragma once

#include <HalStorage.h>

#include <cstdint>

namespace txt_progress {
constexpr uint32_t MAGIC = 0x4D545854;  // "TXTM"
constexpr uint8_t VERSION = 1;
struct Header {
  uint32_t magic = MAGIC;
  uint32_t sourceSize = 0;
  uint32_t count = 0;
  uint16_t recordSize = 12;
  uint8_t version = VERSION;
  uint8_t encoding = 0;
};
struct Record {
  uint32_t source = 0;
  uint32_t visible = 0;
  uint8_t width = 0;
  uint8_t step = 0;
  uint16_t reserved = 0;
};
static_assert(sizeof(Header) == 16 && sizeof(Record) == 12);

enum class LegacyResult : uint8_t { Absent, Restored, Failed };
LegacyResult readLegacySource(const char* cachePath, uint32_t sourceSize, uint32_t& sourceOffset);
bool resolve(HalFile& mapping, uint32_t sourceSize, uint32_t sourceOffset, uint32_t& visibleOffset);
bool sourceForVisible(HalFile& mapping, uint32_t sourceSize, uint32_t visibleOffset, uint32_t& sourceOffset);
}  // namespace txt_progress
