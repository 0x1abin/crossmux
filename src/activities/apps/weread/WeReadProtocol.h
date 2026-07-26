#pragma once

#include <cstddef>
#include <cstdint>

namespace WeReadProtocol {

using Md5Function = bool (*)(const uint8_t* data, size_t len, char out[33]);
using ByteSink = bool (*)(void* ctx, const uint8_t* data, size_t len);

enum class ChapterResponse : uint8_t { Content, Unavailable, Error };

ChapterResponse classifyChapterResponse(int status, bool emptyObject);
bool encodeId(const char* value, Md5Function md5, char* out, size_t outSize);
bool matchesMd5(const char* expected, size_t expectedLen, const char* actual, size_t actualLen);
bool signQuery(const char* query, char* out, size_t outSize);
bool urlEncode(const char* value, char* out, size_t outSize);

// StreamingJsonParser intentionally passes \uXXXX through literally. Decode
// those escapes into UTF-8 while copying into a bounded record field.
size_t decodeJsonString(const char* value, size_t len, char* out, size_t outSize);

// Computes the pairs used by WeRead's reversible character shuffle. `tail`
// contains the final min(4, ceil(encodedLength/10)) bytes in file order.
size_t swapPositions(size_t encodedLength, const uint8_t* tail, size_t tailLen, uint32_t out[10]);

class Base64UrlDecoder {
 public:
  Base64UrlDecoder(ByteSink sink, void* ctx) : sink_(sink), ctx_(ctx) {}

  bool feed(const uint8_t* data, size_t len);
  bool finish();

 private:
  bool emit(size_t count);

  ByteSink sink_;
  void* ctx_;
  uint8_t quartet_[4] = {};
  uint8_t quartetLen_ = 0;
  bool failed_ = false;
};

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len);

}  // namespace WeReadProtocol
