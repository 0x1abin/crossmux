#pragma once

#include <cstddef>
#include <cstdint>

namespace WeReadProtocol {

using Md5Function = bool (*)(const uint8_t* data, size_t len, char out[33]);
using ByteSink = bool (*)(void* ctx, const uint8_t* data, size_t len);

enum class ChapterResponse : uint8_t { Content, AuthenticationRequired, Retryable, Error };
enum class ImageType : uint8_t { None, Jpeg, Png };

ChapterResponse classifyChapterResponse(int status, bool emptyObject);
bool isEmptyJsonObject(const uint8_t* data, size_t len);
bool mergeRuntimeCookie(char* header, size_t headerSize, const char* name, size_t nameLen, const char* value,
                        size_t valueLen);
bool isAllowedXhtmlTag(const char* name);
bool extractImageAttributes(const char* tag, char* source, size_t sourceSize, char* alt, size_t altSize);
ImageType normalizeImageUrl(const char* source, char* output, size_t outputSize);
uint32_t parseUint32OrZero(const char* value, size_t len);

class PsvtsExtractor {
 public:
  PsvtsExtractor(char* out, size_t outSize) : out_(out), outSize_(outSize) {}

  bool reset();
  bool feed(const uint8_t* data, size_t len);
  bool complete() const { return state_ == State::Complete; }

 private:
  enum class State : uint8_t { SearchKey, ExpectColon, ExpectQuote, ReadValue, Complete, Invalid };

  void resumeSearch(uint8_t value);

  char* out_;
  size_t outSize_;
  size_t keyOffset_ = 0;
  size_t valueLength_ = 0;
  State state_ = State::SearchKey;
};

class XhtmlTagProbe {
 public:
  bool reset();
  bool feed(const uint8_t* data, size_t len);
  bool complete() const { return state_ == State::Complete; }

 private:
  enum class State : uint8_t { SearchOpen, ReadName, SkipTag, Complete };

  char name_[16] = {};
  size_t nameLength_ = 0;
  State state_ = State::SearchOpen;
};

bool encodeId(const char* value, Md5Function md5, char* out, size_t outSize);
bool matchesMd5(const char* expected, size_t expectedLen, const char* actual, size_t actualLen);
bool signQuery(const char* query, char* out, size_t outSize);
bool urlEncode(const char* value, char* out, size_t outSize);

// StreamingJsonParser intentionally passes \uXXXX through literally. Decode
// those escapes into UTF-8 while copying into a bounded record field.
size_t decodeJsonString(const char* value, size_t len, char* out, size_t outSize);

class JsonStringDecoder {
 public:
  JsonStringDecoder(ByteSink sink, void* ctx) : sink_(sink), ctx_(ctx) {}

  void reset();
  bool feed(const char* data, size_t len);
  bool finish();

 private:
  bool drain(bool final);
  bool emit(const uint8_t* data, size_t len);
  bool emitCodepoint(uint32_t codepoint);
  void consume(size_t len);

  ByteSink sink_;
  void* ctx_;
  uint8_t pending_[12] = {};
  uint8_t pendingLen_ = 0;
  bool failed_ = false;
};

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
