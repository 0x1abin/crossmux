#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

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
uint32_t hashAppId(const char* value, size_t len);
bool hasUsablePclts(const char* value);

struct RemoteProgress {
  char chapterUid[64] = {};
  float percent = 0.0f;
  uint32_t appIdHash = 0;
  uint32_t chapterOffset = 0;
  uint32_t updateTime = 0;
  bool hasChapterOffset = false;
  bool hasUpdateTime = false;
  bool hasAppId = false;
};

class RemoteProgressParser {
 public:
  explicit RemoteProgressParser(const char* requestedBookId);

  bool reset();
  bool feed(const uint8_t* data, size_t len);
  bool complete() const;
  int errorCode() const { return errorCode_; }
  const RemoteProgress& progress() const { return progress_; }

 private:
  static constexpr size_t kMaxObjectDepth = 8;

  enum class Field : uint8_t {
    None,
    BookId,
    Progress,
    ChapterUid,
    ChapterOffset,
    UpdateTime,
    AppId,
    ErrorCode,
  };

  struct Candidate {
    char bookId[64] = {};
    char chapterUid[64] = {};
    float progress = 0.0f;
    uint32_t appIdHash = 0;
    uint32_t chapterOffset = 0;
    uint32_t updateTime = 0;
    bool hasBookId = false;
    bool hasProgress = false;
    bool hasChapterOffset = false;
    bool hasUpdateTime = false;
    bool hasAppId = false;
  };

  static JsonCallbacks callbacks(RemoteProgressParser* parser);
  static void onKey(void* raw, const char* key, size_t len);
  static void onValue(void* raw, const char* value, size_t len);
  static void onObjectStart(void* raw);
  static void onObjectEnd(void* raw);
  static void onArrayStart(void* raw);
  static void onArrayEnd(void* raw);

  Candidate* currentCandidate();
  void acceptCandidate(const Candidate& candidate);

  const char* requestedBookId_;
  StreamingJsonParser parser_;
  Candidate candidates_[kMaxObjectDepth] = {};
  RemoteProgress progress_;
  Field field_ = Field::None;
  uint8_t objectDepth_ = 0;
  uint8_t containerDepth_ = 0;
  uint8_t bestDepth_ = UINT8_MAX;
  int bestScore_ = -1;
  int errorCode_ = 0;
  bool rootClosed_ = false;
};

class PsvtsExtractor {
 public:
  PsvtsExtractor(char* out, size_t outSize, const char* key = "psvts")
      : out_(out), outSize_(outSize), key_(key), keyLength_(key ? strlen(key) : 0) {}

  bool reset();
  bool feed(const uint8_t* data, size_t len);
  bool complete() const { return state_ == State::Complete; }

 private:
  enum class State : uint8_t { SearchKey, ExpectColon, ExpectQuote, ReadValue, Complete, Invalid };

  void resumeSearch(uint8_t value);

  char* out_;
  size_t outSize_;
  const char* key_;
  size_t keyLength_;
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
