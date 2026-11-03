#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace font_cache_test {
inline std::array<uint8_t, 8192> flash{};
inline size_t slotSize = flash.size();
inline bool slotValid = true;
inline bool slotSafe = true;
inline unsigned writes = 0;
inline unsigned erases = 0;
}  // namespace font_cache_test

class HalOtaSlot {
 public:
  static constexpr size_t ERASE_SIZE = 4096;
  static HalOtaSlot inactive() { return {}; }
  bool valid() const { return font_cache_test::slotValid; }
  size_t size() const { return font_cache_test::slotSize; }
  bool safeForScratchWrite() const { return font_cache_test::slotSafe; }
  bool read(size_t offset, void* data, size_t length) const {
    if (!contains(offset, length)) return false;
    std::memcpy(data, font_cache_test::flash.data() + offset, length);
    return true;
  }
  bool erase(size_t offset, size_t length) const {
    ++font_cache_test::erases;
    if (!contains(offset, length)) return false;
    std::memset(font_cache_test::flash.data() + offset, 0xff, length);
    return true;
  }
  bool write(size_t offset, const void* data, size_t length) const {
    ++font_cache_test::writes;
    if (!contains(offset, length)) return false;
    std::memcpy(font_cache_test::flash.data() + offset, data, length);
    return true;
  }

 private:
  bool contains(size_t offset, size_t length) const {
    return valid() && offset <= font_cache_test::flash.size() && length <= font_cache_test::flash.size() - offset;
  }
};
