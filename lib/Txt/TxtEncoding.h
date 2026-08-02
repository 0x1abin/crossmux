#pragma once

#include <cstddef>
#include <cstdint>

namespace txt_encoding {

enum class Encoding : uint8_t { Unknown = 0, Utf8 = 1, Gbk = 2 };

struct TranscodeResult {
  size_t rawLength;
  size_t utf8Length;
};

bool isSerializedValueValid(uint8_t value);
bool isSupported(Encoding encoding);
Encoding detect(const uint8_t* data, size_t length, bool finalChunk);

// Uses one buffer for both source and destination. The caller must provide
// enough capacity for rawLength + the UTF-8 output.
TranscodeResult transcodeGbkInPlace(uint8_t* buffer, size_t rawLength, size_t capacity, bool finalChunk);

// Maps a consumed UTF-8 prefix back to bytes in the original GBK stream.
size_t gbkSourceLength(const uint8_t* utf8, size_t utf8Length);

}  // namespace txt_encoding
