#include "WeReadClient.h"

#if defined(ENABLE_CHINESE_VERSION) && !defined(__EMSCRIPTEN__)

#include <Arduino.h>
#include <HalClock.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <StreamingJsonParser.h>
#include <mbedtls/sha256.h>
#include <strings.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "WeReadProtocol.h"
#include "util/BookCacheUtils.h"
#include "util/TimeUtils.h"

namespace WeReadClient {
namespace {

constexpr const char* kHost = "https://weread.qq.com";
constexpr const char* kOrigin = "https://weread.qq.com";
constexpr const char* kDefaultReferer = "https://weread.qq.com/";
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/135.0.0.0 "
    "Safari/537.36 Edg/135.0.0.0";
constexpr int kRequestTimeoutMs = 20000;
constexpr unsigned long kLoginTimeoutMs = 240000;
constexpr unsigned long kLoginPollMs = 2000;
constexpr unsigned long kShardPaceMs = 400;
constexpr unsigned long kClockSyncTimeoutMs = 12000;
constexpr unsigned long kNetworkRetryBaseMs = 1000;
constexpr size_t kTransferBufferSize = 1024;
constexpr size_t kMaxImageBytes = 4 * 1024 * 1024;
// Local decode/package work uses at most one 1 KB transfer buffer at a time.
// Keep wider headroom for SD internals and the event-driven progress render.
constexpr size_t kBookSessionMinFreeHeap = 20 * 1024;
constexpr size_t kBookSessionMinLargestBlock = 8 * 1024;

void logMemory([[maybe_unused]] const char* phase) {
  LOG_DBG("WR", "%s: free=%u largest=%u stack=%u", phase, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void logJobComplete() {
  LOG_INF("WR", "job complete");
  logMemory("job complete");
}

struct ResponseSink {
  void* ctx;
  bool (*reset)(void* ctx);
  bool (*write)(void* ctx, const uint8_t* data, size_t len);
  bool (*finish)(void* ctx);
  Error writeError;
};

bool noOpFinish(void*) { return true; }

bool equalsIgnoreCase(const char* left, const char* right) {
  if (!left || !right) return false;
  while (*left && *right) {
    if (std::tolower(static_cast<unsigned char>(*left)) != std::tolower(static_cast<unsigned char>(*right))) {
      return false;
    }
    ++left;
    ++right;
  }
  return *left == '\0' && *right == '\0';
}

bool md5Hex(const uint8_t* data, const size_t len, char out[33]) {
  MD5Builder md5;
  md5.begin();
  md5.add(data, len);
  md5.calculate();
  const String value = md5.toString();
  if (value.length() != 32) return false;
  memcpy(out, value.c_str(), 32);
  out[32] = '\0';
  return true;
}

bool appendText(char* out, const size_t outSize, size_t& position, const char* value, const size_t length) {
  if (!out || !value || position + length >= outSize) return false;
  memcpy(out + position, value, length);
  position += length;
  out[position] = '\0';
  return true;
}

bool appendText(char* out, const size_t outSize, size_t& position, const char* value) {
  return value && appendText(out, outSize, position, value, strlen(value));
}

bool appendUnsigned(char* out, const size_t outSize, size_t& position, const uint64_t value) {
  char number[24];
  const int length = snprintf(number, sizeof(number), "%llu", static_cast<unsigned long long>(value));
  return length > 0 && static_cast<size_t>(length) < sizeof(number) &&
         appendText(out, outSize, position, number, static_cast<size_t>(length));
}

bool appendUrlEncodedPrefix(char* out, const size_t outSize, size_t& position, const char* value,
                            const size_t maxCodepoints) {
  if (!value) return false;
  static constexpr char kHex[] = "0123456789ABCDEF";
  size_t codepoints = 0;
  const auto* cursor = reinterpret_cast<const uint8_t*>(value);
  while (*cursor && codepoints < maxCodepoints) {
    size_t width = 1;
    if ((*cursor & 0xE0) == 0xC0) {
      width = 2;
    } else if ((*cursor & 0xF0) == 0xE0) {
      width = 3;
    } else if ((*cursor & 0xF8) == 0xF0) {
      width = 4;
    } else if (*cursor >= 0x80) {
      return false;
    }
    for (size_t i = 1; i < width; ++i) {
      if ((cursor[i] & 0xC0) != 0x80) return false;
    }
    for (size_t i = 0; i < width; ++i) {
      const uint8_t c = cursor[i];
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
          c == '.' || c == '~') {
        const char plain = static_cast<char>(c);
        if (!appendText(out, outSize, position, &plain, 1)) return false;
      } else {
        const char encoded[] = {'%', kHex[c >> 4], kHex[c & 0x0F]};
        if (!appendText(out, outSize, position, encoded, sizeof(encoded))) return false;
      }
    }
    cursor += width;
    ++codepoints;
  }
  return true;
}

bool appendJsonPrefix(char* out, const size_t outSize, size_t& position, const char* value,
                      const size_t maxCodepoints) {
  if (!value) return false;
  size_t codepoints = 0;
  const auto* cursor = reinterpret_cast<const uint8_t*>(value);
  while (*cursor && codepoints < maxCodepoints) {
    size_t width = 1;
    if ((*cursor & 0xE0) == 0xC0) {
      width = 2;
    } else if ((*cursor & 0xF0) == 0xE0) {
      width = 3;
    } else if ((*cursor & 0xF8) == 0xF0) {
      width = 4;
    } else if (*cursor >= 0x80) {
      return false;
    }
    for (size_t i = 1; i < width; ++i) {
      if ((cursor[i] & 0xC0) != 0x80) return false;
    }
    if (width == 1 && (*cursor == '"' || *cursor == '\\')) {
      const char escaped[] = {'\\', static_cast<char>(*cursor)};
      if (!appendText(out, outSize, position, escaped, sizeof(escaped))) return false;
    } else if (width == 1 && *cursor < 0x20) {
      // Chapter titles are metadata, so dropping control bytes is safer than
      // spending payload space on invisible JSON escapes.
    } else if (!appendText(out, outSize, position, reinterpret_cast<const char*>(cursor), width)) {
      return false;
    }
    cursor += width;
    ++codepoints;
  }
  return true;
}

bool makeWebAppId(char* out, const size_t outSize) {
  if (!out || outSize == 0) return false;
  char prefix[13] = {};
  size_t prefixLength = 0;
  const char* cursor = kUserAgent;
  while (*cursor && prefixLength < 12) {
    while (*cursor == ' ') ++cursor;
    const char* start = cursor;
    while (*cursor && *cursor != ' ') ++cursor;
    if (cursor == start) break;
    prefix[prefixLength++] = static_cast<char>('0' + ((cursor - start) % 10));
  }
  uint32_t hash = 0;
  for (const auto* p = reinterpret_cast<const uint8_t*>(kUserAgent); *p; ++p) {
    hash = (131U * hash + *p) & 0x7fffffffU;
  }
  const int length =
      snprintf(out, outSize, "wb%.*sh%u", static_cast<int>(prefixLength), prefix, static_cast<unsigned>(hash));
  return length > 0 && static_cast<size_t>(length) < outSize;
}

bool sha256Hex(const char* value, char* out, const size_t outSize) {
  if (!value || !out || outSize < 65) return false;
  uint8_t digest[32];
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  const bool ok = mbedtls_sha256_starts(&context, 0) == 0 &&
                  mbedtls_sha256_update(&context, reinterpret_cast<const uint8_t*>(value), strlen(value)) == 0 &&
                  mbedtls_sha256_finish(&context, digest) == 0;
  mbedtls_sha256_free(&context);
  if (!ok) return false;
  for (size_t i = 0; i < sizeof(digest); ++i) snprintf(out + i * 2, outSize - i * 2, "%02x", digest[i]);
  out[64] = '\0';
  return true;
}

bool mergeSessionCookies(const WeReadStore::Session* session, char* cookie, const size_t cookieSize) {
  if (!session) return true;
  if (session->vid[0] &&
      !WeReadProtocol::mergeRuntimeCookie(cookie, cookieSize, "wr_vid", 6, session->vid, strlen(session->vid))) {
    return false;
  }
  if (session->skey[0] &&
      !WeReadProtocol::mergeRuntimeCookie(cookie, cookieSize, "wr_skey", 7, session->skey, strlen(session->skey))) {
    return false;
  }
  return !session->rt[0] ||
         WeReadProtocol::mergeRuntimeCookie(cookie, cookieSize, "wr_rt", 5, session->rt, strlen(session->rt));
}

bool absorbSetCookie(WeReadStore::Session* session, const char* headerName, const char* headerValue, char* cookie,
                     const size_t cookieSize) {
  if (!equalsIgnoreCase(headerName, "set-cookie") || !headerValue) return true;
  while (std::isspace(static_cast<unsigned char>(*headerValue))) ++headerValue;
  const char* equals = strchr(headerValue, '=');
  if (!equals) return true;
  const char* nameEnd = equals;
  while (nameEnd > headerValue && std::isspace(static_cast<unsigned char>(nameEnd[-1]))) --nameEnd;
  const size_t nameLen = static_cast<size_t>(nameEnd - headerValue);
  if (nameLen < 3 || memcmp(headerValue, "wr_", 3) != 0) return true;

  const char* value = equals + 1;
  while (std::isspace(static_cast<unsigned char>(*value))) ++value;
  const char* end = strchr(equals + 1, ';');
  if (!end) end = headerValue + strlen(headerValue);
  while (end > value && std::isspace(static_cast<unsigned char>(end[-1]))) --end;
  const size_t valueLen = static_cast<size_t>(end - value);
  if (!WeReadProtocol::mergeRuntimeCookie(cookie, cookieSize, headerValue, nameLen, value, valueLen)) {
    LOG_ERR("WR", "runtime cookie rejected");
    return false;
  }

  bool persistent = true;
  if (session && nameLen == 6 && memcmp(headerValue, "wr_vid", 6) == 0) {
    persistent = session->setCookie("wr_vid", value, valueLen);
  } else if (session && nameLen == 7 && memcmp(headerValue, "wr_skey", 7) == 0) {
    persistent = session->setCookie("wr_skey", value, valueLen);
  } else if (session && nameLen == 5 && memcmp(headerValue, "wr_rt", 5) == 0) {
    persistent = session->setCookie("wr_rt", value, valueLen);
  }
  if (!persistent) {
    LOG_ERR("WR", "runtime cookie rejected: name=%.*s", static_cast<int>(nameLen), headerValue);
    return false;
  }
  LOG_DBG("WR", "runtime cookie accepted: name=%.*s", static_cast<int>(nameLen), headerValue);
  return true;
}

Error requestOnce(const char* method, const char* path, const uint8_t* body, const size_t bodySize,
                  WeReadStore::Session* session, const char* referer, ResponseSink& sink, int& status, char* cookie,
                  const size_t cookieSize, char* url, const size_t urlSize, uint8_t* readBuffer,
                  const size_t readBufferSize, WeReadHttpClient::Session* reusableSession = nullptr) {
  if (!method || !path || !cookie || cookieSize == 0 || !url || urlSize == 0 || !readBuffer || readBufferSize == 0) {
    return Error::Protocol;
  }
  WeReadHttpClient::Header headers[6] = {};
  size_t headerCount = 0;
  headers[headerCount++] = {"User-Agent", kUserAgent};
  headers[headerCount++] = {"Accept", "application/json, text/plain, */*"};
  headers[headerCount++] = {"Origin", kOrigin};
  headers[headerCount++] = {"Referer", referer ? referer : kDefaultReferer};
  if (bodySize > 0) headers[headerCount++] = {"Content-Type", "application/json;charset=UTF-8"};
  if (!mergeSessionCookies(session, cookie, cookieSize)) return Error::Protocol;
  if (cookie[0]) headers[headerCount++] = {"Cookie", cookie};

  WeReadHttpClient::RequestOptions options;
  options.method = method;
  options.body = body;
  options.bodySize = bodySize;
  options.headers = headers;
  options.headerCount = headerCount;
  options.timeoutMs = kRequestTimeoutMs;
  options.readBuffer = readBuffer;
  options.readBufferSize = readBufferSize;

  if (!sink.reset(sink.ctx)) return Error::SdCard;
  const int urlLength = snprintf(url, urlSize, "%s%s", kHost, path);
  if (urlLength <= 0 || static_cast<size_t>(urlLength) >= urlSize) return Error::Protocol;
  const auto onData = [&sink](const uint8_t* data, const size_t len) { return sink.write(sink.ctx, data, len); };
  bool cookiesOk = true;
  const auto onHeader = [session, cookie, cookieSize, &cookiesOk](const char* name, const char* value) {
    cookiesOk = absorbSetCookie(session, name, value, cookie, cookieSize) && cookiesOk;
  };
  const auto result = reusableSession
                          ? WeReadHttpClient::request(*reusableSession, url, options, onData, onHeader, status)
                          : WeReadHttpClient::request(url, options, onData, onHeader, status);
  if (result == WeReadHttpClient::Result::Ok) {
    if (!sink.finish(sink.ctx)) return Error::SdCard;
    return cookiesOk ? Error::Ok : Error::Protocol;
  }
  if (result == WeReadHttpClient::Result::Aborted) return sink.writeError;
  return Error::Network;
}

enum class SimpleField : uint8_t {
  None,
  Uid,
  Succeed,
  Vid,
  Token,
  LogicCode,
  ErrorCode,
  Success,
  SyncKey,
};

struct SimpleJsonContext {
  StreamingJsonParser* parser = nullptr;
  SimpleField field = SimpleField::None;
  char uid[128] = {};
  char vid[128] = {};
  char token[384] = {};
  char logicCode[64] = {};
  int errorCode = 0;
  bool succeed = false;
  bool hasSyncKey = false;
  bool rootClosed = false;
  size_t bytesReceived = 0;
  int depth = 0;
};

void simpleKey(void* raw, const char* key, size_t) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  if (strcmp(key, "uid") == 0) {
    ctx.field = SimpleField::Uid;
  } else if (strcmp(key, "succeed") == 0) {
    ctx.field = SimpleField::Succeed;
  } else if (strcmp(key, "webLoginVid") == 0 || strcmp(key, "vid") == 0 || strcmp(key, "userVid") == 0 ||
             strcmp(key, "user_vid") == 0) {
    ctx.field = SimpleField::Vid;
  } else if (strcmp(key, "accessToken") == 0) {
    ctx.field = SimpleField::Token;
  } else if (strcmp(key, "logicCode") == 0) {
    ctx.field = SimpleField::LogicCode;
  } else if (strcmp(key, "errcode") == 0 || strcmp(key, "errCode") == 0) {
    ctx.field = SimpleField::ErrorCode;
  } else if (strcmp(key, "succ") == 0) {
    ctx.field = SimpleField::Success;
  } else if (strcmp(key, "synckey") == 0) {
    ctx.field = SimpleField::SyncKey;
  } else {
    ctx.field = SimpleField::None;
  }
}

void copyDecoded(const char* value, const size_t len, char* dest, const size_t capacity) {
  WeReadProtocol::decodeJsonString(value, len, dest, capacity);
}

void simpleString(void* raw, const char* value, const size_t len) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  switch (ctx.field) {
    case SimpleField::Uid:
      copyDecoded(value, len, ctx.uid, sizeof(ctx.uid));
      break;
    case SimpleField::Vid:
      copyDecoded(value, len, ctx.vid, sizeof(ctx.vid));
      break;
    case SimpleField::Token:
      copyDecoded(value, len, ctx.token, sizeof(ctx.token));
      break;
    case SimpleField::LogicCode:
      copyDecoded(value, len, ctx.logicCode, sizeof(ctx.logicCode));
      break;
    case SimpleField::ErrorCode:
      ctx.errorCode = atoi(value);
      break;
    case SimpleField::Succeed:
    case SimpleField::Success:
      ctx.succeed = strcmp(value, "1") == 0 || strcmp(value, "true") == 0;
      break;
    case SimpleField::SyncKey:
      ctx.hasSyncKey = len > 0;
      break;
    case SimpleField::None:
      break;
  }
  ctx.field = SimpleField::None;
}

void simpleNumber(void* raw, const char* value, const size_t len) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  if (ctx.field == SimpleField::Uid) {
    copyDecoded(value, len, ctx.uid, sizeof(ctx.uid));
  } else if (ctx.field == SimpleField::Vid) {
    copyDecoded(value, len, ctx.vid, sizeof(ctx.vid));
  } else if (ctx.field == SimpleField::ErrorCode) {
    ctx.errorCode = atoi(value);
  } else if (ctx.field == SimpleField::Succeed || ctx.field == SimpleField::Success) {
    ctx.succeed = atoi(value) != 0;
  } else if (ctx.field == SimpleField::SyncKey) {
    ctx.hasSyncKey = len > 0;
  }
  ctx.field = SimpleField::None;
}

void simpleBool(void* raw, const bool value) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  if (ctx.field == SimpleField::Succeed || ctx.field == SimpleField::Success) ctx.succeed = value;
  ctx.field = SimpleField::None;
}

void simpleObjectStart(void* raw) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  ++ctx.depth;
  ctx.field = SimpleField::None;
}

void simpleObjectEnd(void* raw) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  if (ctx.depth == 1) ctx.rootClosed = true;
  if (ctx.depth > 0) --ctx.depth;
  ctx.field = SimpleField::None;
}

void simpleArrayStart(void* raw) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  ++ctx.depth;
  ctx.field = SimpleField::None;
}

void simpleArrayEnd(void* raw) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  if (ctx.depth > 0) --ctx.depth;
  ctx.field = SimpleField::None;
}

JsonCallbacks simpleCallbacks(SimpleJsonContext* ctx) {
  return {ctx,     simpleKey,         simpleString,    simpleNumber,     simpleBool,
          nullptr, simpleObjectStart, simpleObjectEnd, simpleArrayStart, simpleArrayEnd,
          nullptr};
}

bool resetSimple(void* raw) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  ctx.field = SimpleField::None;
  ctx.uid[0] = '\0';
  ctx.vid[0] = '\0';
  ctx.token[0] = '\0';
  ctx.logicCode[0] = '\0';
  ctx.errorCode = 0;
  ctx.succeed = false;
  ctx.hasSyncKey = false;
  ctx.rootClosed = false;
  ctx.bytesReceived = 0;
  ctx.depth = 0;
  ctx.parser->reset();
  return true;
}

bool feedSimple(void* raw, const uint8_t* data, const size_t len) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  ctx.bytesReceived += len;
  ctx.parser->feed(reinterpret_cast<const char*>(data), len);
  return !ctx.parser->hasError();
}

bool resetRemoteProgress(void* raw) { return static_cast<WeReadProtocol::RemoteProgressParser*>(raw)->reset(); }

bool feedRemoteProgress(void* raw, const uint8_t* data, const size_t len) {
  return static_cast<WeReadProtocol::RemoteProgressParser*>(raw)->feed(data, len);
}

enum class ShelfField : uint8_t { None, Books, BookId, Title, Author, ReadUpdateTime, ErrorCode };

struct ShelfJsonContext {
  StreamingJsonParser* parser = nullptr;
  WeReadStore::IndexWriter writer;
  WeReadStore::ShelfRecord current;
  ShelfField field = ShelfField::None;
  int depth = 0;
  int booksDepth = -1;
  int bookDepth = -1;
  int errorCode = 0;
  bool inBooks = false;
  bool inBook = false;
  bool rootClosed = false;
  bool writeFailed = false;
};

void shelfKey(void* raw, const char* key, size_t) {
  auto& ctx = *static_cast<ShelfJsonContext*>(raw);
  if (strcmp(key, "books") == 0) {
    ctx.field = ShelfField::Books;
  } else if (strcmp(key, "bookId") == 0) {
    ctx.field = ShelfField::BookId;
  } else if (strcmp(key, "title") == 0) {
    ctx.field = ShelfField::Title;
  } else if (strcmp(key, "author") == 0) {
    ctx.field = ShelfField::Author;
  } else if (strcmp(key, "readUpdateTime") == 0) {
    ctx.field = ShelfField::ReadUpdateTime;
  } else if (strcmp(key, "errcode") == 0 || strcmp(key, "errCode") == 0) {
    ctx.field = ShelfField::ErrorCode;
  } else {
    ctx.field = ShelfField::None;
  }
}

void shelfValue(void* raw, const char* value, const size_t len) {
  auto& ctx = *static_cast<ShelfJsonContext*>(raw);
  if (ctx.inBook && ctx.depth == ctx.bookDepth) {
    switch (ctx.field) {
      case ShelfField::BookId:
        copyDecoded(value, len, ctx.current.bookId, sizeof(ctx.current.bookId));
        break;
      case ShelfField::Title:
        copyDecoded(value, len, ctx.current.title, sizeof(ctx.current.title));
        break;
      case ShelfField::Author:
        copyDecoded(value, len, ctx.current.author, sizeof(ctx.current.author));
        break;
      case ShelfField::ReadUpdateTime:
        ctx.current.readUpdateTime = WeReadProtocol::parseUint32OrZero(value, len);
        break;
      case ShelfField::None:
      case ShelfField::Books:
      case ShelfField::ErrorCode:
        break;
    }
  }
  if (ctx.field == ShelfField::ErrorCode) ctx.errorCode = atoi(value);
  ctx.field = ShelfField::None;
}

void shelfObjectStart(void* raw) {
  auto& ctx = *static_cast<ShelfJsonContext*>(raw);
  ++ctx.depth;
  if (ctx.inBooks && ctx.depth == ctx.booksDepth + 1) {
    memset(&ctx.current, 0, sizeof(ctx.current));
    ctx.bookDepth = ctx.depth;
    ctx.inBook = true;
  }
  ctx.field = ShelfField::None;
}

void shelfObjectEnd(void* raw) {
  auto& ctx = *static_cast<ShelfJsonContext*>(raw);
  if (ctx.inBook && ctx.depth == ctx.bookDepth) {
    if (ctx.current.bookId[0] && !ctx.writer.append(&ctx.current)) ctx.writeFailed = true;
    ctx.inBook = false;
    ctx.bookDepth = -1;
  }
  if (ctx.depth == 1) ctx.rootClosed = true;
  if (ctx.depth > 0) --ctx.depth;
  ctx.field = ShelfField::None;
}

void shelfArrayStart(void* raw) {
  auto& ctx = *static_cast<ShelfJsonContext*>(raw);
  ++ctx.depth;
  if (ctx.field == ShelfField::Books) {
    ctx.inBooks = true;
    ctx.booksDepth = ctx.depth;
  }
  ctx.field = ShelfField::None;
}

void shelfArrayEnd(void* raw) {
  auto& ctx = *static_cast<ShelfJsonContext*>(raw);
  if (ctx.inBooks && ctx.depth == ctx.booksDepth) {
    ctx.inBooks = false;
    ctx.booksDepth = -1;
  }
  if (ctx.depth > 0) --ctx.depth;
  ctx.field = ShelfField::None;
}

JsonCallbacks shelfCallbacks(ShelfJsonContext* ctx) {
  return {ctx,     shelfKey,         shelfValue,     shelfValue,      nullptr,
          nullptr, shelfObjectStart, shelfObjectEnd, shelfArrayStart, shelfArrayEnd,
          nullptr};
}

bool resetShelf(void* raw) {
  auto& ctx = *static_cast<ShelfJsonContext*>(raw);
  ctx.writer.abort();
  if (!ctx.writer.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord))) {
    return false;
  }
  memset(&ctx.current, 0, sizeof(ctx.current));
  ctx.field = ShelfField::None;
  ctx.depth = 0;
  ctx.booksDepth = -1;
  ctx.bookDepth = -1;
  ctx.errorCode = 0;
  ctx.inBooks = false;
  ctx.inBook = false;
  ctx.rootClosed = false;
  ctx.writeFailed = false;
  ctx.parser->reset();
  return true;
}

bool feedShelf(void* raw, const uint8_t* data, const size_t len) {
  auto& ctx = *static_cast<ShelfJsonContext*>(raw);
  ctx.parser->feed(reinterpret_cast<const char*>(data), len);
  return !ctx.parser->hasError() && !ctx.writeFailed;
}

enum class TocField : uint8_t { None, Chapters, ChapterUid, Title, WordCount, ChapterIdx, Paid, ErrorCode };

struct TocJsonContext {
  StreamingJsonParser* parser = nullptr;
  WeReadStore::IndexWriter writer;
  WeReadStore::TocRecord current;
  std::string path;
  TocField field = TocField::None;
  int depth = 0;
  int chaptersDepth = -1;
  int chapterDepth = -1;
  int errorCode = 0;
  bool inChapters = false;
  bool inChapter = false;
  bool rootClosed = false;
  bool writeFailed = false;
};

void tocKey(void* raw, const char* key, size_t) {
  auto& ctx = *static_cast<TocJsonContext*>(raw);
  if (strcmp(key, "updated") == 0 || strcmp(key, "chapterInfos") == 0) {
    ctx.field = TocField::Chapters;
  } else if (strcmp(key, "chapterUid") == 0) {
    ctx.field = TocField::ChapterUid;
  } else if (strcmp(key, "title") == 0) {
    ctx.field = TocField::Title;
  } else if (strcmp(key, "wordCount") == 0) {
    ctx.field = TocField::WordCount;
  } else if (strcmp(key, "chapterIdx") == 0) {
    ctx.field = TocField::ChapterIdx;
  } else if (strcmp(key, "paid") == 0) {
    ctx.field = TocField::Paid;
  } else if (strcmp(key, "errcode") == 0 || strcmp(key, "errCode") == 0) {
    ctx.field = TocField::ErrorCode;
  } else {
    ctx.field = TocField::None;
  }
}

void tocValue(void* raw, const char* value, const size_t len) {
  auto& ctx = *static_cast<TocJsonContext*>(raw);
  if (ctx.inChapter) {
    switch (ctx.field) {
      case TocField::ChapterUid:
        copyDecoded(value, len, ctx.current.chapterUid, sizeof(ctx.current.chapterUid));
        break;
      case TocField::Title:
        copyDecoded(value, len, ctx.current.title, sizeof(ctx.current.title));
        break;
      case TocField::WordCount:
        ctx.current.wordCount = WeReadProtocol::parseUint32OrZero(value, len);
        break;
      case TocField::ChapterIdx:
        ctx.current.chapterIdx = static_cast<uint32_t>(strtoul(value, nullptr, 10));
        break;
      case TocField::Paid:
        ctx.current.paid = static_cast<uint8_t>(atoi(value) != 0);
        break;
      case TocField::None:
      case TocField::Chapters:
      case TocField::ErrorCode:
        break;
    }
  }
  if (ctx.field == TocField::ErrorCode) ctx.errorCode = atoi(value);
  ctx.field = TocField::None;
}

void tocBool(void* raw, const bool value) {
  auto& ctx = *static_cast<TocJsonContext*>(raw);
  if (ctx.inChapter && ctx.field == TocField::Paid) ctx.current.paid = static_cast<uint8_t>(value);
  ctx.field = TocField::None;
}

void tocObjectStart(void* raw) {
  auto& ctx = *static_cast<TocJsonContext*>(raw);
  ++ctx.depth;
  if (ctx.inChapters && ctx.depth == ctx.chaptersDepth + 1) {
    memset(&ctx.current, 0, sizeof(ctx.current));
    ctx.chapterDepth = ctx.depth;
    ctx.inChapter = true;
  }
  ctx.field = TocField::None;
}

void tocObjectEnd(void* raw) {
  auto& ctx = *static_cast<TocJsonContext*>(raw);
  if (ctx.inChapter && ctx.depth == ctx.chapterDepth) {
    if (ctx.current.chapterUid[0] && !ctx.writer.append(&ctx.current)) ctx.writeFailed = true;
    ctx.inChapter = false;
    ctx.chapterDepth = -1;
  }
  if (ctx.depth == 1) ctx.rootClosed = true;
  if (ctx.depth > 0) --ctx.depth;
  ctx.field = TocField::None;
}

void tocArrayStart(void* raw) {
  auto& ctx = *static_cast<TocJsonContext*>(raw);
  ++ctx.depth;
  if (ctx.field == TocField::Chapters && !ctx.inChapters) {
    ctx.inChapters = true;
    ctx.chaptersDepth = ctx.depth;
  }
  ctx.field = TocField::None;
}

void tocArrayEnd(void* raw) {
  auto& ctx = *static_cast<TocJsonContext*>(raw);
  if (ctx.inChapters && ctx.depth == ctx.chaptersDepth) {
    ctx.inChapters = false;
    ctx.chaptersDepth = -1;
  }
  if (ctx.depth > 0) --ctx.depth;
  ctx.field = TocField::None;
}

JsonCallbacks tocCallbacks(TocJsonContext* ctx) {
  return {ctx,          tocKey,        tocValue,    tocValue, tocBool, nullptr, tocObjectStart,
          tocObjectEnd, tocArrayStart, tocArrayEnd, nullptr};
}

bool resetToc(void* raw) {
  auto& ctx = *static_cast<TocJsonContext*>(raw);
  ctx.writer.abort();
  if (!ctx.writer.begin(ctx.path, WeReadStore::kTocMagic, sizeof(WeReadStore::TocRecord))) return false;
  memset(&ctx.current, 0, sizeof(ctx.current));
  ctx.field = TocField::None;
  ctx.depth = 0;
  ctx.chaptersDepth = -1;
  ctx.chapterDepth = -1;
  ctx.errorCode = 0;
  ctx.inChapters = false;
  ctx.inChapter = false;
  ctx.rootClosed = false;
  ctx.writeFailed = false;
  ctx.parser->reset();
  return true;
}

bool feedToc(void* raw, const uint8_t* data, const size_t len) {
  auto& ctx = *static_cast<TocJsonContext*>(raw);
  ctx.parser->feed(reinterpret_cast<const char*>(data), len);
  return !ctx.parser->hasError() && !ctx.writeFailed;
}

enum class DetailField : uint8_t {
  None,
  Title,
  Author,
  Intro,
  Cover,
  Publisher,
  Category,
  Categories,
  CategoryTitle,
  TotalWords,
  NewRating,
  NewRatingCount,
  ErrorCode,
};

struct DetailJsonContext {
  StreamingJsonParser* parser = nullptr;
  WeReadProtocol::JsonStringDecoder* introDecoder = nullptr;
  WeReadStore::BookDetailWriter writer;
  WeReadStore::BookDetailHeader header;
  const WeReadStore::ShelfRecord* book = nullptr;
  const std::string* bookDir = nullptr;
  DetailField field = DetailField::None;
  int depth = 0;
  int categoriesDepth = -1;
  int errorCode = 0;
  uint8_t introBuffer[256] = {};
  size_t introBufferLength = 0;
  bool rootClosed = false;
  bool introChunking = false;
  bool writeFailed = false;
};

bool writeDetailIntro(void* raw, const uint8_t* data, const size_t len) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  if (ctx.introBufferLength + len > sizeof(ctx.introBuffer) &&
      !ctx.writer.appendIntro(ctx.introBuffer, ctx.introBufferLength)) {
    ctx.writeFailed = true;
    return false;
  }
  if (ctx.introBufferLength + len > sizeof(ctx.introBuffer)) ctx.introBufferLength = 0;
  if (len > sizeof(ctx.introBuffer) - ctx.introBufferLength) {
    ctx.writeFailed = true;
    return false;
  }
  memcpy(ctx.introBuffer + ctx.introBufferLength, data, len);
  ctx.introBufferLength += len;
  return true;
}

bool finishDetail(void* raw) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  if (ctx.writeFailed) return false;
  if (ctx.introBufferLength > 0 && !ctx.writer.appendIntro(ctx.introBuffer, ctx.introBufferLength)) {
    ctx.writeFailed = true;
    return false;
  }
  ctx.introBufferLength = 0;
  return true;
}

void detailKey(void* raw, const char* key, size_t) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  if (ctx.depth == 1) {
    if (strcmp(key, "title") == 0) {
      ctx.field = DetailField::Title;
    } else if (strcmp(key, "author") == 0) {
      ctx.field = DetailField::Author;
    } else if (strcmp(key, "intro") == 0) {
      ctx.field = DetailField::Intro;
    } else if (strcmp(key, "cover") == 0) {
      ctx.field = DetailField::Cover;
    } else if (strcmp(key, "publisher") == 0) {
      ctx.field = DetailField::Publisher;
    } else if (strcmp(key, "category") == 0) {
      ctx.field = DetailField::Category;
    } else if (strcmp(key, "categories") == 0) {
      ctx.field = DetailField::Categories;
    } else if (strcmp(key, "totalWords") == 0 || strcmp(key, "wordCount") == 0) {
      ctx.field = DetailField::TotalWords;
    } else if (strcmp(key, "newRating") == 0) {
      ctx.field = DetailField::NewRating;
    } else if (strcmp(key, "newRatingCount") == 0) {
      ctx.field = DetailField::NewRatingCount;
    } else if (strcmp(key, "errcode") == 0 || strcmp(key, "errCode") == 0) {
      ctx.field = DetailField::ErrorCode;
    } else {
      ctx.field = DetailField::None;
    }
    return;
  }
  ctx.field = ctx.categoriesDepth >= 0 && ctx.depth == ctx.categoriesDepth + 1 && strcmp(key, "title") == 0
                  ? DetailField::CategoryTitle
                  : DetailField::None;
}

void detailString(void* raw, const char* value, const size_t len) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  switch (ctx.field) {
    case DetailField::Title:
      copyDecoded(value, len, ctx.header.title, sizeof(ctx.header.title));
      break;
    case DetailField::Author:
      copyDecoded(value, len, ctx.header.author, sizeof(ctx.header.author));
      break;
    case DetailField::Intro:
      ctx.introDecoder->reset();
      if (!ctx.introDecoder->feed(value, len) || !ctx.introDecoder->finish()) ctx.writeFailed = true;
      break;
    case DetailField::Cover:
      copyDecoded(value, len, ctx.header.coverUrl, sizeof(ctx.header.coverUrl));
      {
        char normalized[sizeof(ctx.header.coverUrl)] = {};
        if (WeReadProtocol::normalizeImageUrl(ctx.header.coverUrl, normalized, sizeof(normalized)) ==
            WeReadProtocol::ImageType::None) {
          ctx.header.coverUrl[0] = '\0';
        } else {
          memcpy(ctx.header.coverUrl, normalized, sizeof(ctx.header.coverUrl));
        }
      }
      break;
    case DetailField::Publisher:
      copyDecoded(value, len, ctx.header.publisher, sizeof(ctx.header.publisher));
      break;
    case DetailField::Category:
      copyDecoded(value, len, ctx.header.category, sizeof(ctx.header.category));
      break;
    case DetailField::CategoryTitle:
      if (!ctx.header.category[0]) copyDecoded(value, len, ctx.header.category, sizeof(ctx.header.category));
      break;
    case DetailField::ErrorCode:
      ctx.errorCode = atoi(value);
      break;
    case DetailField::TotalWords:
      ctx.header.totalWords = static_cast<uint32_t>(std::min<unsigned long>(strtoul(value, nullptr, 10), UINT32_MAX));
      break;
    case DetailField::NewRating: {
      const unsigned long rating = strtoul(value, nullptr, 10);
      ctx.header.newRating = rating <= 1000 ? static_cast<uint16_t>(rating) : 0;
      break;
    }
    case DetailField::NewRatingCount:
      ctx.header.newRatingCount =
          static_cast<uint32_t>(std::min<unsigned long>(strtoul(value, nullptr, 10), UINT32_MAX));
      break;
    case DetailField::None:
    case DetailField::Categories:
      break;
  }
  ctx.field = DetailField::None;
}

void detailStringChunk(void* raw, const char* value, const size_t len, const bool final) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  if (ctx.field != DetailField::Intro) {
    if (final) ctx.field = DetailField::None;
    return;
  }
  if (!ctx.introChunking) {
    ctx.introDecoder->reset();
    ctx.introChunking = true;
  }
  if (!ctx.introDecoder->feed(value, len) || (final && !ctx.introDecoder->finish())) ctx.writeFailed = true;
  if (final) {
    ctx.introChunking = false;
    ctx.field = DetailField::None;
  }
}

void detailNumber(void* raw, const char* value, size_t) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  const unsigned long parsed = strtoul(value, nullptr, 10);
  switch (ctx.field) {
    case DetailField::TotalWords:
      ctx.header.totalWords = static_cast<uint32_t>(std::min<unsigned long>(parsed, UINT32_MAX));
      break;
    case DetailField::NewRating:
      ctx.header.newRating = parsed <= 1000 ? static_cast<uint16_t>(parsed) : 0;
      break;
    case DetailField::NewRatingCount:
      ctx.header.newRatingCount = static_cast<uint32_t>(std::min<unsigned long>(parsed, UINT32_MAX));
      break;
    case DetailField::ErrorCode:
      ctx.errorCode = atoi(value);
      break;
    case DetailField::None:
    case DetailField::Title:
    case DetailField::Author:
    case DetailField::Intro:
    case DetailField::Cover:
    case DetailField::Publisher:
    case DetailField::Category:
    case DetailField::Categories:
    case DetailField::CategoryTitle:
      break;
  }
  ctx.field = DetailField::None;
}

void detailObjectStart(void* raw) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  ++ctx.depth;
  ctx.field = DetailField::None;
}

void detailObjectEnd(void* raw) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  if (ctx.depth == 1) ctx.rootClosed = true;
  if (ctx.depth > 0) --ctx.depth;
  ctx.field = DetailField::None;
}

void detailArrayStart(void* raw) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  ++ctx.depth;
  if (ctx.field == DetailField::Categories) ctx.categoriesDepth = ctx.depth;
  ctx.field = DetailField::None;
}

void detailArrayEnd(void* raw) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  if (ctx.depth == ctx.categoriesDepth) ctx.categoriesDepth = -1;
  if (ctx.depth > 0) --ctx.depth;
  ctx.field = DetailField::None;
}

JsonCallbacks detailCallbacks(DetailJsonContext* ctx) {
  return {ctx,
          detailKey,
          detailString,
          detailNumber,
          nullptr,
          nullptr,
          detailObjectStart,
          detailObjectEnd,
          detailArrayStart,
          detailArrayEnd,
          detailStringChunk};
}

bool resetDetail(void* raw) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  ctx.writer.abort();
  if (!ctx.book || !ctx.bookDir || !ctx.writer.begin(*ctx.bookDir)) return false;
  ctx.header = {};
  memcpy(ctx.header.title, ctx.book->title, sizeof(ctx.header.title));
  ctx.header.title[sizeof(ctx.header.title) - 1] = '\0';
  memcpy(ctx.header.author, ctx.book->author, sizeof(ctx.header.author));
  ctx.header.author[sizeof(ctx.header.author) - 1] = '\0';
  ctx.field = DetailField::None;
  ctx.depth = 0;
  ctx.categoriesDepth = -1;
  ctx.errorCode = 0;
  ctx.introBufferLength = 0;
  ctx.rootClosed = false;
  ctx.introChunking = false;
  ctx.writeFailed = false;
  ctx.introDecoder->reset();
  ctx.parser->reset();
  return true;
}

bool feedDetail(void* raw, const uint8_t* data, const size_t len) {
  auto& ctx = *static_cast<DetailJsonContext*>(raw);
  ctx.parser->feed(reinterpret_cast<const char*>(data), len);
  return !ctx.parser->hasError() && !ctx.writeFailed;
}

struct FileSink {
  enum class Failure : uint8_t { None, SdCard, TooLarge, Cancelled };

  HalFile file;
  const std::string* path = nullptr;
  size_t size = 0;
  size_t maxSize = SIZE_MAX;
  bool* cancelRequested = nullptr;
  Failure failure = Failure::None;
  uint8_t prefix[8] = {};
  size_t prefixSize = 0;
};

bool resetFile(void* raw) {
  auto& sink = *static_cast<FileSink*>(raw);
  if (!sink.path) return false;
  if (sink.file.isOpen()) sink.file.close();
  if (Storage.exists(sink.path->c_str())) Storage.remove(sink.path->c_str());
  sink.size = 0;
  sink.failure = FileSink::Failure::None;
  sink.prefixSize = 0;
  return Storage.openFileForWrite("WR", *sink.path, sink.file);
}

bool writeFile(void* raw, const uint8_t* data, const size_t len) {
  auto& sink = *static_cast<FileSink*>(raw);
  if (sink.cancelRequested && *sink.cancelRequested) {
    sink.failure = FileSink::Failure::Cancelled;
    return false;
  }
  if (len > sink.maxSize - sink.size) {
    sink.failure = FileSink::Failure::TooLarge;
    return false;
  }
  const size_t prefixBytes = std::min(len, sizeof(sink.prefix) - sink.prefixSize);
  if (prefixBytes > 0) {
    memcpy(sink.prefix + sink.prefixSize, data, prefixBytes);
    sink.prefixSize += prefixBytes;
  }
  if (sink.file.write(data, len) != len) {
    sink.failure = FileSink::Failure::SdCard;
    return false;
  }
  sink.size += len;
  return true;
}

bool finishFile(void* raw) {
  auto& sink = *static_cast<FileSink*>(raw);
  sink.file.flush();
  sink.file.close();
  return true;
}

bool resetPsvts(void* raw) { return static_cast<WeReadProtocol::PsvtsExtractor*>(raw)->reset(); }

bool extractPsvts(void* raw, const uint8_t* data, const size_t len) {
  return static_cast<WeReadProtocol::PsvtsExtractor*>(raw)->feed(data, len);
}

struct ReaderContextSink {
  WeReadProtocol::PsvtsExtractor psvts;
  WeReadProtocol::PsvtsExtractor pclts;
  WeReadProtocol::PsvtsExtractor token;

  ReaderContextSink(char* psvtsOut, const size_t psvtsSize, char* pcltsOut, const size_t pcltsSize, char* tokenOut,
                    const size_t tokenSize)
      : psvts(psvtsOut, psvtsSize), pclts(pcltsOut, pcltsSize, "pclts"), token(tokenOut, tokenSize, "token") {}
};

bool resetReaderContext(void* raw) {
  auto& context = *static_cast<ReaderContextSink*>(raw);
  return context.psvts.reset() && context.pclts.reset() && context.token.reset();
}

bool extractReaderContext(void* raw, const uint8_t* data, const size_t len) {
  auto& context = *static_cast<ReaderContextSink*>(raw);
  return context.psvts.feed(data, len) && context.pclts.feed(data, len) && context.token.feed(data, len);
}

bool isSafeProtocolToken(const char* value) {
  if (!value || !value[0]) return false;
  for (const auto* p = reinterpret_cast<const uint8_t*>(value); *p; ++p) {
    if (!std::isalnum(*p) && *p != '-' && *p != '_') return false;
  }
  return true;
}

bool isWereadUrl(const char* url) {
  WeReadHttpClient::HttpsUrlView parts;
  if (!WeReadHttpClient::parseHttpsUrl(url, parts)) return false;
  static constexpr char kDomain[] = "weread.qq.com";
  const size_t domainLength = sizeof(kDomain) - 1;
  if (parts.hostLength == domainLength) {
    return strncasecmp(parts.host, kDomain, domainLength) == 0;
  }
  return parts.hostLength > domainLength && parts.host[parts.hostLength - domainLength - 1] == '.' &&
         strncasecmp(parts.host + parts.hostLength - domainLength, kDomain, domainLength) == 0;
}

bool resolveRedirectUrl(const char* current, const char* location, char* output, const size_t outputSize) {
  if (!current || !location || !output || outputSize == 0) return false;
  while (std::isspace(static_cast<unsigned char>(*location))) ++location;
  const char* end = location + strlen(location);
  while (end > location && std::isspace(static_cast<unsigned char>(end[-1]))) --end;
  const char* fragment = static_cast<const char*>(memchr(location, '#', static_cast<size_t>(end - location)));
  if (fragment) end = fragment;
  if (end == location) return false;

  int written = -1;
  if (static_cast<size_t>(end - location) >= 8 && strncmp(location, "https://", 8) == 0) {
    if (static_cast<size_t>(end - location) >= outputSize) return false;
    memcpy(output, location, static_cast<size_t>(end - location));
    output[end - location] = '\0';
  } else if (end - location >= 2 && location[0] == '/' && location[1] == '/') {
    written = snprintf(output, outputSize, "https:%.*s", static_cast<int>(end - location), location);
  } else if (location[0] == '/') {
    WeReadHttpClient::HttpsUrlView parts;
    if (!WeReadHttpClient::parseHttpsUrl(current, parts)) return false;
    written = snprintf(output, outputSize, "https://%.*s%.*s", static_cast<int>(parts.hostLength), parts.host,
                       static_cast<int>(end - location), location);
  } else {
    return false;
  }
  if (written >= 0 && (written == 0 || static_cast<size_t>(written) >= outputSize)) return false;
  WeReadHttpClient::HttpsUrlView verified;
  return WeReadHttpClient::parseHttpsUrl(output, verified);
}

WeReadProtocol::ImageType imageTypeFromHref(const char* href) {
  if (!href) return WeReadProtocol::ImageType::None;
  const size_t length = strlen(href);
  if (length > 4 && strcasecmp(href + length - 4, ".jpg") == 0) return WeReadProtocol::ImageType::Jpeg;
  if (length > 4 && strcasecmp(href + length - 4, ".png") == 0) return WeReadProtocol::ImageType::Png;
  return WeReadProtocol::ImageType::None;
}

bool validImageRecord(const WeReadStore::ImageRecord& record) {
  if (!memchr(record.href, '\0', sizeof(record.href)) || !memchr(record.url, '\0', sizeof(record.url)) ||
      strncmp(record.href, "images/", 7) != 0 || strstr(record.href, "..") ||
      imageTypeFromHref(record.href) == WeReadProtocol::ImageType::None) {
    return false;
  }
  WeReadHttpClient::HttpsUrlView parts;
  return WeReadHttpClient::parseHttpsUrl(record.url, parts);
}

bool validImageWorkRecord(const WeReadStore::ImageWorkRecord& record) {
  switch (record.state) {
    case WeReadStore::ImageWorkState::Pending:
      return record.attempts < 2 && validImageRecord(record.image);
    case WeReadStore::ImageWorkState::Complete:
    case WeReadStore::ImageWorkState::Skipped:
      return validImageRecord(record.image);
  }
  return false;
}

bool validImageFile(const std::string& path, const WeReadProtocol::ImageType type) {
  HalFile file;
  uint8_t prefix[8] = {};
  if (type == WeReadProtocol::ImageType::None || !Storage.openFileForRead("WR", path, file) || file.fileSize64() == 0 ||
      file.fileSize64() > kMaxImageBytes) {
    return false;
  }
  const size_t wanted = type == WeReadProtocol::ImageType::Png ? sizeof(prefix) : 3;
  if (file.read(prefix, wanted) != static_cast<int>(wanted)) return false;
  static constexpr uint8_t kPng[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  return type == WeReadProtocol::ImageType::Png ? memcmp(prefix, kPng, sizeof(kPng)) == 0
                                                : prefix[0] == 0xFF && prefix[1] == 0xD8 && prefix[2] == 0xFF;
}

const char* imageMediaType(const WeReadProtocol::ImageType type) {
  return type == WeReadProtocol::ImageType::Png ? "image/png" : "image/jpeg";
}

const char* coverSourceName(const WeReadProtocol::ImageType type) {
  return type == WeReadProtocol::ImageType::Png ? "cover.source.png" : "cover.source.jpg";
}

const char* coverEntryName(const WeReadProtocol::ImageType type) {
  return type == WeReadProtocol::ImageType::Png ? "cover.png" : "cover.jpg";
}

WeReadProtocol::ImageType findCoverSource(const std::string& bookDir, std::string& path) {
  // Cold path: reuse one bounded (< 100-byte) path string while cover bytes stay on SD.
  path.reserve(bookDir.size() + sizeof("/cover.source.png"));
  path = bookDir;
  path += "/cover.source.png";
  if (validImageFile(path, WeReadProtocol::ImageType::Png)) return WeReadProtocol::ImageType::Png;
  path.resize(bookDir.size());
  path += "/cover.source.jpg";
  if (validImageFile(path, WeReadProtocol::ImageType::Jpeg)) return WeReadProtocol::ImageType::Jpeg;
  path.clear();
  return WeReadProtocol::ImageType::None;
}

bool makeReaderReferer(const char* bookId, const char* chapterUid, std::string& referer) {
  char encodedBook[128];
  char encodedChapter[128];
  if (!WeReadProtocol::encodeId(bookId, md5Hex, encodedBook, sizeof(encodedBook)) ||
      !WeReadProtocol::encodeId(chapterUid, md5Hex, encodedChapter, sizeof(encodedChapter))) {
    return false;
  }
  referer = std::string(kHost) + "/web/reader/" + encodedBook + "k" + encodedChapter;
  return true;
}

bool makeContentBody(const char* bookId, const char* chapterUid, const char* psvts, char* scratch,
                     const size_t scratchSize, size_t& bodySize) {
  uint32_t timestamp = TimeUtils::getCurrentValidTimestamp();
  if (timestamp == 0 || !isSafeProtocolToken(psvts)) return false;

  char encodedBook[128];
  char encodedChapter[128];
  char timestampText[16];
  char encodedTimestamp[128];
  snprintf(timestampText, sizeof(timestampText), "%u", static_cast<unsigned>(timestamp));
  if (!WeReadProtocol::encodeId(bookId, md5Hex, encodedBook, sizeof(encodedBook)) ||
      !WeReadProtocol::encodeId(chapterUid, md5Hex, encodedChapter, sizeof(encodedChapter)) ||
      !WeReadProtocol::encodeId(timestampText, md5Hex, encodedTimestamp, sizeof(encodedTimestamp))) {
    return false;
  }
  if (strcmp(encodedTimestamp, psvts) == 0) {
    ++timestamp;
    snprintf(timestampText, sizeof(timestampText), "%u", static_cast<unsigned>(timestamp));
    if (!WeReadProtocol::encodeId(timestampText, md5Hex, encodedTimestamp, sizeof(encodedTimestamp))) {
      return false;
    }
  }

  const uint32_t randomValue = static_cast<uint32_t>(random(0, 10000));
  const uint32_t requestRandom = randomValue * randomValue;
  char encodedPsvts[256];
  if (!WeReadProtocol::urlEncode(psvts, encodedPsvts, sizeof(encodedPsvts))) return false;

  const int queryLen = snprintf(scratch, scratchSize, "b=%s&c=%s&ct=%u&pc=%s&prevChapter=false&ps=%s&r=%u&sc=1&st=0",
                                encodedBook, encodedChapter, static_cast<unsigned>(timestamp), encodedTimestamp,
                                encodedPsvts, static_cast<unsigned>(requestRandom));
  if (queryLen <= 0 || static_cast<size_t>(queryLen) >= scratchSize) return false;

  char signature[24];
  if (!WeReadProtocol::signQuery(scratch, signature, sizeof(signature))) return false;
  const int jsonLen = snprintf(scratch, scratchSize,
                               "{\"b\":\"%s\",\"c\":\"%s\",\"r\":%u,\"ct\":%u,\"ps\":\"%s\",\"pc\":\"%s\","
                               "\"sc\":1,\"prevChapter\":\"false\",\"st\":0,\"s\":\"%s\"}",
                               encodedBook, encodedChapter, static_cast<unsigned>(requestRandom),
                               static_cast<unsigned>(timestamp), psvts, encodedTimestamp, signature);
  if (jsonLen <= 0 || static_cast<size_t>(jsonLen) >= scratchSize) return false;
  bodySize = static_cast<size_t>(jsonLen);
  return true;
}

bool appendEncodedId(char* out, const size_t outSize, size_t& position, char* work, const size_t workSize,
                     const char* value) {
  return WeReadProtocol::encodeId(value, md5Hex, work, workSize) && appendText(out, outSize, position, work);
}

bool appendProgressQuery(char* out, const size_t outSize, char* work, const size_t workSize, const char* bookId,
                         const WeReadStore::TocRecord& chapter, const uint32_t chapterOffset, const uint32_t progress,
                         const uint32_t now, const char* psvts, const char* pclts, const char* token, const bool report,
                         const uint64_t timestampMs, const uint32_t randomNumber) {
  size_t position = 0;
  out[0] = '\0';
  if (!makeWebAppId(work, workSize) || !appendText(out, outSize, position, "appId=") ||
      !appendText(out, outSize, position, work) || !appendText(out, outSize, position, "&b=") ||
      !appendEncodedId(out, outSize, position, work, workSize, bookId) || !appendText(out, outSize, position, "&c=") ||
      !appendEncodedId(out, outSize, position, work, workSize, chapter.chapterUid) ||
      !appendText(out, outSize, position, "&ci=") || !appendUnsigned(out, outSize, position, chapter.chapterIdx) ||
      !appendText(out, outSize, position, "&co=") || !appendUnsigned(out, outSize, position, chapterOffset) ||
      !appendText(out, outSize, position, "&ct=") || !appendUnsigned(out, outSize, position, now) ||
      !appendText(out, outSize, position, "&pc=")) {
    return false;
  }
  if (WeReadProtocol::hasUsablePclts(pclts)) {
    if (!appendText(out, outSize, position, pclts)) return false;
  } else {
    char nowText[16];
    snprintf(nowText, sizeof(nowText), "%u", static_cast<unsigned>(now));
    if (!appendEncodedId(out, outSize, position, work, workSize, nowText)) return false;
  }
  if (!appendText(out, outSize, position, "&pr=") || !appendUnsigned(out, outSize, position, progress) ||
      !appendText(out, outSize, position, "&ps=") || !appendUrlEncodedPrefix(out, outSize, position, psvts, SIZE_MAX)) {
    return false;
  }
  if (report) {
    if (!appendText(out, outSize, position, "&rn=") || !appendUnsigned(out, outSize, position, randomNumber) ||
        !appendText(out, outSize, position, "&rt=0&sg=")) {
      return false;
    }
    const int sourceLength = snprintf(work, workSize, "%llu%u%s", static_cast<unsigned long long>(timestampMs),
                                      static_cast<unsigned>(randomNumber), token);
    if (sourceLength <= 0 || static_cast<size_t>(sourceLength) >= workSize || !sha256Hex(work, work, workSize) ||
        !appendText(out, outSize, position, work)) {
      return false;
    }
  }
  if (!appendText(out, outSize, position, "&sm=") ||
      !appendUrlEncodedPrefix(out, outSize, position, chapter.title, 20)) {
    return false;
  }
  return !report || (appendText(out, outSize, position, "&ts=") && appendUnsigned(out, outSize, position, timestampMs));
}

bool makeProgressBody(const char* bookId, const WeReadStore::TocRecord& chapter, const uint32_t chapterOffset,
                      const float localFraction, const char* psvts, const char* pclts, const char* readerToken,
                      const bool report, char* body, const size_t bodySize, char* work, const size_t workSize,
                      size_t& written) {
  static constexpr char kDefaultReaderToken[] = "3c5c8717f3daf09iop3423zafeqoi";
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  if (now == 0 || !isSafeProtocolToken(bookId) || !isSafeProtocolToken(chapter.chapterUid) ||
      !isSafeProtocolToken(psvts)) {
    return false;
  }
  const char* token = readerToken && readerToken[0] ? readerToken : kDefaultReaderToken;
  if (!isSafeProtocolToken(token) || (pclts && pclts[0] && !isSafeProtocolToken(pclts))) return false;
  const float clampedFraction = std::max(0.0f, std::min(1.0f, localFraction));
  const uint32_t progress = static_cast<uint32_t>(clampedFraction * 100.0f);
  const uint32_t randomNumber = report ? static_cast<uint32_t>(random(0, 1000)) : 0;
  const uint64_t timestampMs =
      report ? static_cast<uint64_t>(now) * 1000ULL + static_cast<uint32_t>(random(0, 1000)) : 0;

  if (!appendProgressQuery(body, bodySize, work, workSize, bookId, chapter, chapterOffset, progress, now, psvts, pclts,
                           token, report, timestampMs, randomNumber)) {
    return false;
  }
  char signature[24];
  if (!WeReadProtocol::signQuery(body, signature, sizeof(signature))) return false;

  size_t position = 0;
  body[0] = '\0';
  if (!makeWebAppId(work, workSize) || !appendText(body, bodySize, position, "{\"appId\":\"") ||
      !appendText(body, bodySize, position, work) || !appendText(body, bodySize, position, "\",\"b\":\"") ||
      !appendEncodedId(body, bodySize, position, work, workSize, bookId) ||
      !appendText(body, bodySize, position, "\",\"c\":\"") ||
      !appendEncodedId(body, bodySize, position, work, workSize, chapter.chapterUid) ||
      !appendText(body, bodySize, position, "\",\"ci\":") ||
      !appendUnsigned(body, bodySize, position, chapter.chapterIdx) ||
      !appendText(body, bodySize, position, ",\"co\":") || !appendUnsigned(body, bodySize, position, chapterOffset) ||
      !appendText(body, bodySize, position, ",\"sm\":\"") ||
      !appendJsonPrefix(body, bodySize, position, chapter.title, 20) ||
      !appendText(body, bodySize, position, "\",\"pr\":") || !appendUnsigned(body, bodySize, position, progress) ||
      !appendText(body, bodySize, position, ",\"ct\":") || !appendUnsigned(body, bodySize, position, now) ||
      !appendText(body, bodySize, position, ",\"ps\":\"") || !appendText(body, bodySize, position, psvts) ||
      !appendText(body, bodySize, position, "\",\"pc\":\"")) {
    return false;
  }
  if (WeReadProtocol::hasUsablePclts(pclts)) {
    if (!appendText(body, bodySize, position, pclts)) return false;
  } else {
    char nowText[16];
    snprintf(nowText, sizeof(nowText), "%u", static_cast<unsigned>(now));
    if (!appendEncodedId(body, bodySize, position, work, workSize, nowText)) return false;
  }
  if (!appendText(body, bodySize, position, "\"")) return false;
  if (report) {
    if (!appendText(body, bodySize, position, ",\"rt\":0,\"ts\":") ||
        !appendUnsigned(body, bodySize, position, timestampMs) || !appendText(body, bodySize, position, ",\"rn\":") ||
        !appendUnsigned(body, bodySize, position, randomNumber)) {
      return false;
    }
    const int sourceLength = snprintf(work, workSize, "%llu%u%s", static_cast<unsigned long long>(timestampMs),
                                      static_cast<unsigned>(randomNumber), token);
    if (sourceLength <= 0 || static_cast<size_t>(sourceLength) >= workSize || !sha256Hex(work, work, workSize) ||
        !appendText(body, bodySize, position, ",\"sg\":\"") || !appendText(body, bodySize, position, work) ||
        !appendText(body, bodySize, position, "\"")) {
      return false;
    }
  }
  if (!appendText(body, bodySize, position, ",\"s\":\"") || !appendText(body, bodySize, position, signature) ||
      !appendText(body, bodySize, position, "\"}")) {
    return false;
  }
  written = position;
  return true;
}

bool readPrefix(const std::string& path, uint8_t* out, const size_t len) {
  HalFile file;
  return Storage.openFileForRead("WR", path, file) && file.read(out, len) == static_cast<int>(len);
}

bool containsAllowedXhtmlTag(const std::string& path, uint8_t* buffer, const size_t bufferSize, bool& contains) {
  contains = false;
  if (!buffer || bufferSize == 0) return false;
  HalFile file;
  if (!Storage.openFileForRead("WR", path, file)) return false;
  WeReadProtocol::XhtmlTagProbe probe;
  probe.reset();
  while (file.available() && !probe.complete()) {
    const int got = file.read(buffer, bufferSize);
    if (got <= 0 || !probe.feed(buffer, static_cast<size_t>(got))) return false;
  }
  contains = probe.complete();
  return true;
}

bool smallFileContains(const std::string& path, const char* needle) {
  HalFile file;
  if (!needle || !needle[0] || !Storage.openFileForRead("WR", path, file) || file.fileSize() > 2048) return false;
  char buffer[256] = {};
  size_t carry = 0;
  while (file.available()) {
    const int got = file.read(buffer + carry, sizeof(buffer) - 1 - carry);
    if (got <= 0) break;
    buffer[carry + static_cast<size_t>(got)] = '\0';
    if (strstr(buffer, needle)) return true;
    const size_t keep = std::min(strlen(needle) - 1, carry + static_cast<size_t>(got));
    memmove(buffer, buffer + carry + static_cast<size_t>(got) - keep, keep);
    carry = keep;
  }
  return false;
}

bool smallFileIsEmptyObject(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("WR", path, file) || file.fileSize64() > 16) return false;
  const size_t size = file.fileSize();
  uint8_t body[16];
  return file.read(body, size) == static_cast<int>(size) && WeReadProtocol::isEmptyJsonObject(body, size);
}

bool validateShard(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("WR", path, file) || file.fileSize64() <= 32) return false;
  char expected[33] = {};
  if (file.read(expected, 32) != 32) return false;

  MD5Builder md5;
  md5.begin();
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kTransferBufferSize);
  if (!buffer) {
    LOG_ERR("WR", "OOM: %u-byte MD5 buffer", static_cast<unsigned>(kTransferBufferSize));
    return false;
  }
  while (file.available()) {
    const int got = file.read(buffer.get(), kTransferBufferSize);
    if (got <= 0) return false;
    md5.add(buffer.get(), static_cast<size_t>(got));
  }
  md5.calculate();
  const String actual = md5.toString();
  return WeReadProtocol::matchesMd5(expected, 32, actual.c_str(), actual.length());
}

bool copyShardBody(const std::string& path, HalFile& output, bool& skipFirst, uint8_t* buffer) {
  HalFile input;
  if (!Storage.openFileForRead("WR", path, input) || !input.seek(32)) return false;
  while (input.available()) {
    const int got = input.read(buffer, kTransferBufferSize);
    if (got <= 0) return false;
    size_t offset = 0;
    if (skipFirst) {
      offset = 1;
      skipFirst = false;
      if (got == 1) continue;
    }
    if (output.write(buffer + offset, static_cast<size_t>(got) - offset) != static_cast<size_t>(got) - offset) {
      return false;
    }
  }
  return true;
}

bool reverseSwaps(const std::string& encodedPath) {
  HalFile file = Storage.open(encodedPath.c_str(), O_RDWR);
  if (!file || file.fileSize64() > UINT32_MAX) return false;
  const size_t length = file.fileSize();
  if (length < 4) return false;
  const size_t tailLen = std::min<size_t>(4, (length + 9) / 10);
  uint8_t tail[4] = {};
  if (!file.seek(length - tailLen) || file.read(tail, tailLen) != static_cast<int>(tailLen)) return false;
  uint32_t positions[10] = {};
  const size_t count = WeReadProtocol::swapPositions(length, tail, tailLen, positions);
  if (count == 0 || (count & 1U)) return false;

  for (size_t pair = count; pair >= 2; pair -= 2) {
    for (int delta = 1; delta >= 0; --delta) {
      const size_t left = positions[pair - 1] + static_cast<size_t>(delta);
      const size_t right = positions[pair - 2] + static_cast<size_t>(delta);
      if (left >= length || right >= length) continue;
      uint8_t leftByte = 0;
      uint8_t rightByte = 0;
      if (!file.seek(left) || file.read(&leftByte, 1) != 1 || !file.seek(right) || file.read(&rightByte, 1) != 1 ||
          !file.seek(left) || file.write(&rightByte, 1) != 1 || !file.seek(right) || file.write(&leftByte, 1) != 1) {
        return false;
      }
    }
    if (pair == 2) break;
  }
  file.flush();
  return true;
}

bool decoderSink(void* raw, const uint8_t* data, const size_t len) {
  auto* file = static_cast<HalFile*>(raw);
  return file->write(data, len) == len;
}

bool combineAndDecode(const std::string* shards, const size_t shardCount, const std::string& bookDir,
                      std::string& decodedPath) {
  const std::string encodedPath = bookDir + "/encoded.part";
  decodedPath = bookDir + "/decoded.part";
  if (Storage.exists(encodedPath.c_str())) Storage.remove(encodedPath.c_str());
  if (Storage.exists(decodedPath.c_str())) Storage.remove(decodedPath.c_str());

  HalFile encoded;
  if (!Storage.openFileForWrite("WR", encodedPath, encoded)) return false;
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kTransferBufferSize);
  if (!buffer) {
    LOG_ERR("WR", "OOM: %u-byte shard buffer", static_cast<unsigned>(kTransferBufferSize));
    return false;
  }
  bool skipFirst = true;
  for (size_t i = 0; i < shardCount; ++i) {
    if (!copyShardBody(shards[i], encoded, skipFirst, buffer.get())) return false;
  }
  encoded.flush();
  encoded.close();
  if (!reverseSwaps(encodedPath)) return false;

  HalFile input;
  HalFile output;
  if (!Storage.openFileForRead("WR", encodedPath, input) || !Storage.openFileForWrite("WR", decodedPath, output)) {
    return false;
  }
  WeReadProtocol::Base64UrlDecoder decoder(decoderSink, &output);
  while (input.available()) {
    const int got = input.read(buffer.get(), kTransferBufferSize);
    if (got <= 0 || !decoder.feed(buffer.get(), static_cast<size_t>(got))) return false;
  }
  if (!decoder.finish()) return false;
  output.flush();
  output.close();
  Storage.remove(encodedPath.c_str());
  return true;
}

bool writeXmlText(HalFile& output, const char* text) {
  if (!text) return true;
  for (const auto* p = reinterpret_cast<const uint8_t*>(text); *p; ++p) {
    const char* escaped = nullptr;
    switch (*p) {
      case '&':
        escaped = "&amp;";
        break;
      case '<':
        escaped = "&lt;";
        break;
      case '>':
        escaped = "&gt;";
        break;
      case '"':
        escaped = "&quot;";
        break;
      case '\'':
        escaped = "&apos;";
        break;
      default:
        if (output.write(*p) != 1) return false;
        continue;
    }
    if (output.write(reinterpret_cast<const uint8_t*>(escaped), strlen(escaped)) != strlen(escaped)) return false;
  }
  return true;
}

bool writeLiteral(HalFile& output, const char* text) {
  return output.write(reinterpret_cast<const uint8_t*>(text), strlen(text)) == strlen(text);
}

void decodeBasicHtmlEntities(char* text) {
  if (!text) return;
  char* read = text;
  char* write = text;
  while (*read) {
    struct Entity {
      const char* encoded;
      char decoded;
    };
    static constexpr Entity kEntities[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    bool replaced = false;
    for (const auto& entity : kEntities) {
      const size_t length = strlen(entity.encoded);
      if (strncmp(read, entity.encoded, length) != 0) continue;
      *write++ = entity.decoded;
      read += length;
      replaced = true;
      break;
    }
    if (!replaced) *write++ = *read++;
  }
  *write = '\0';
}

struct XhtmlSanitizer {
  HalFile* output = nullptr;
  WeReadStore::IndexWriter* imageWriter = nullptr;
  char* tag = nullptr;
  size_t tagCapacity = 0;
  uint32_t chapterIndex = 0;
  bool plainText = false;
  bool inTag = false;
  bool inEntity = false;
  bool skip = false;
  bool skipHead = false;
  bool tagOverflow = false;
  size_t tagLen = 0;
  char entity[24] = {};
  size_t entityLen = 0;
};

bool emitEntity(XhtmlSanitizer& sanitizer) {
  sanitizer.entity[sanitizer.entityLen] = '\0';
  const char* replacement = nullptr;
  if (strcmp(sanitizer.entity, "amp") == 0) replacement = "&amp;";
  if (strcmp(sanitizer.entity, "lt") == 0) replacement = "&lt;";
  if (strcmp(sanitizer.entity, "gt") == 0) replacement = "&gt;";
  if (strcmp(sanitizer.entity, "quot") == 0) replacement = "&quot;";
  if (strcmp(sanitizer.entity, "apos") == 0) replacement = "&apos;";
  if (strcmp(sanitizer.entity, "nbsp") == 0) replacement = "&#160;";
  bool numericEntity = sanitizer.entity[0] == '#' && sanitizer.entity[1] != '\0';
  const bool hexEntity = sanitizer.entity[1] == 'x' || sanitizer.entity[1] == 'X';
  if (hexEntity && sanitizer.entity[2] == '\0') numericEntity = false;
  for (size_t i = hexEntity ? 2 : 1; numericEntity && sanitizer.entity[i]; ++i) {
    numericEntity = hexEntity ? std::isxdigit(static_cast<unsigned char>(sanitizer.entity[i]))
                              : std::isdigit(static_cast<unsigned char>(sanitizer.entity[i]));
  }
  if (numericEntity) {
    char numeric[32];
    const int written = snprintf(numeric, sizeof(numeric), "&%s;", sanitizer.entity);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(numeric)) return false;
    replacement = numeric;
    const bool ok = writeLiteral(*sanitizer.output, replacement);
    sanitizer.entityLen = 0;
    sanitizer.inEntity = false;
    return ok;
  }
  bool ok = true;
  if (replacement) {
    ok = writeLiteral(*sanitizer.output, replacement);
  } else {
    ok = writeLiteral(*sanitizer.output, "&amp;") && writeLiteral(*sanitizer.output, sanitizer.entity) &&
         writeLiteral(*sanitizer.output, ";");
  }
  sanitizer.entityLen = 0;
  sanitizer.inEntity = false;
  return ok;
}

bool emitSanitizedTextByte(XhtmlSanitizer& sanitizer, const uint8_t value) {
  if (sanitizer.skip || sanitizer.skipHead) return true;
  if (sanitizer.plainText) {
    if (value == '\r') return true;
    if (value == '\n') return writeLiteral(*sanitizer.output, "<br/>");
  }
  if (sanitizer.inEntity) {
    if (value == ';') return emitEntity(sanitizer);
    if (sanitizer.entityLen + 1 >= sizeof(sanitizer.entity) || value == '<' || value == '&' || std::isspace(value)) {
      if (!writeLiteral(*sanitizer.output, "&amp;") ||
          sanitizer.output->write(reinterpret_cast<const uint8_t*>(sanitizer.entity), sanitizer.entityLen) !=
              sanitizer.entityLen) {
        return false;
      }
      sanitizer.entityLen = 0;
      sanitizer.inEntity = false;
    } else {
      sanitizer.entity[sanitizer.entityLen++] = static_cast<char>(value);
      return true;
    }
  }
  if (value == '&') {
    sanitizer.inEntity = true;
    sanitizer.entityLen = 0;
    return true;
  }
  if (value < 0x20 && value != '\t' && value != '\n') return true;
  if (value == '<') return writeLiteral(*sanitizer.output, "&lt;");
  if (value == '>') return writeLiteral(*sanitizer.output, "&gt;");
  return sanitizer.output->write(value) == 1;
}

bool processTag(XhtmlSanitizer& sanitizer) {
  if (sanitizer.tagOverflow || !sanitizer.tag || sanitizer.tagCapacity == 0) {
    sanitizer.tagLen = 0;
    sanitizer.inTag = false;
    sanitizer.tagOverflow = false;
    return true;
  }
  sanitizer.tag[sanitizer.tagLen] = '\0';
  const char* tagEnd = sanitizer.tag + sanitizer.tagLen;
  while (tagEnd > sanitizer.tag && std::isspace(static_cast<unsigned char>(tagEnd[-1]))) --tagEnd;
  const bool selfClosing = tagEnd > sanitizer.tag && tagEnd[-1] == '/';
  const char* cursor = sanitizer.tag;
  while (*cursor && std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  bool closing = false;
  if (*cursor == '/') {
    closing = true;
    ++cursor;
  }
  char name[24] = {};
  size_t len = 0;
  while (*cursor && !std::isspace(static_cast<unsigned char>(*cursor)) && *cursor != '/' && *cursor != '>' &&
         len + 1 < sizeof(name)) {
    name[len++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*cursor++)));
  }
  name[len] = '\0';
  sanitizer.tagLen = 0;
  sanitizer.inTag = false;
  if (!name[0] || name[0] == '!' || name[0] == '?') return true;

  if (strcmp(name, "head") == 0) {
    sanitizer.skipHead = !closing && !selfClosing;
    return true;
  }
  if (strcmp(name, "script") == 0 || strcmp(name, "style") == 0) {
    sanitizer.skip = !closing && !selfClosing;
    return true;
  }
  if (!closing && strcmp(name, "img") == 0 && !sanitizer.skip && !sanitizer.skipHead) {
    WeReadStore::ImageRecord record;
    char alt[256] = {};
    const bool extracted =
        WeReadProtocol::extractImageAttributes(sanitizer.tag, record.url, sizeof(record.url), alt, sizeof(alt));
    const WeReadProtocol::ImageType type =
        extracted ? WeReadProtocol::normalizeImageUrl(record.url, sanitizer.tag, sizeof(record.url))
                  : WeReadProtocol::ImageType::None;
    decodeBasicHtmlEntities(alt);
    if (type != WeReadProtocol::ImageType::None) {
      memcpy(record.url, sanitizer.tag, strlen(sanitizer.tag) + 1);
      const char* extension = type == WeReadProtocol::ImageType::Jpeg ? "jpg" : "png";
      const int hrefLen = snprintf(record.href, sizeof(record.href), "images/ch%06u-%u.%s",
                                   static_cast<unsigned>(sanitizer.chapterIndex),
                                   static_cast<unsigned>(sanitizer.imageWriter->count()), extension);
      if (hrefLen <= 0 || static_cast<size_t>(hrefLen) >= sizeof(record.href) ||
          !sanitizer.imageWriter->append(&record) || !writeLiteral(*sanitizer.output, "<img src=\"") ||
          !writeLiteral(*sanitizer.output, record.href)) {
        return false;
      }
      if (alt[0] && (!writeLiteral(*sanitizer.output, "\" alt=\"") || !writeXmlText(*sanitizer.output, alt))) {
        return false;
      }
      return writeLiteral(*sanitizer.output, "\"/>");
    }
    return !alt[0] || writeXmlText(*sanitizer.output, alt);
  }
  if (sanitizer.skip || sanitizer.skipHead || strcmp(name, "html") == 0 || strcmp(name, "body") == 0 ||
      !WeReadProtocol::isAllowedXhtmlTag(name)) {
    return true;
  }
  if (!writeLiteral(*sanitizer.output, "<")) return false;
  if (closing && !writeLiteral(*sanitizer.output, "/")) return false;
  if (!writeLiteral(*sanitizer.output, name)) return false;
  if (!closing && (selfClosing || strcmp(name, "br") == 0 || strcmp(name, "hr") == 0) &&
      !writeLiteral(*sanitizer.output, "/")) {
    return false;
  }
  return writeLiteral(*sanitizer.output, ">");
}

bool sanitizeToXhtml(const std::string& inputPath, const std::string& outputPath, const std::string& imageIndexPath,
                     const uint32_t chapterIndex, const char* title, const bool plainText, uint8_t* readBuffer,
                     const size_t readBufferSize, char* tagBuffer, const size_t tagBufferSize) {
  HalFile input;
  if (!readBuffer || readBufferSize == 0 || !tagBuffer || tagBufferSize < sizeof(WeReadStore::ImageRecord::url) ||
      !Storage.openFileForRead("WR", inputPath, input)) {
    return false;
  }
  const std::string partPath = outputPath + ".part";
  if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
  if (Storage.exists(imageIndexPath.c_str()) && !Storage.remove(imageIndexPath.c_str())) return false;
  HalFile output;
  if (!Storage.openFileForWrite("WR", partPath, output)) return false;
  WeReadStore::IndexWriter imageWriter;
  if (!imageWriter.begin(imageIndexPath, WeReadStore::kImageMagic, sizeof(WeReadStore::ImageRecord))) return false;

  const bool written = [&]() {
    if (!writeLiteral(output,
                      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                      "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>") ||
        !writeXmlText(output, title) || !writeLiteral(output, "</title></head><body>") ||
        (plainText && !writeLiteral(output, "<p>"))) {
      return false;
    }

    XhtmlSanitizer sanitizer{&output, &imageWriter, tagBuffer, tagBufferSize, chapterIndex, plainText};
    while (input.available()) {
      const int got = input.read(readBuffer, readBufferSize);
      if (got <= 0) return false;
      for (int i = 0; i < got; ++i) {
        const uint8_t value = readBuffer[i];
        if (!plainText && sanitizer.inTag) {
          if (value == '>') {
            if (!processTag(sanitizer)) return false;
          } else if (sanitizer.tagLen + 1 < sanitizer.tagCapacity) {
            sanitizer.tag[sanitizer.tagLen++] = static_cast<char>(value);
          } else {
            sanitizer.tagOverflow = true;
          }
          continue;
        }
        if (!plainText && value == '<') {
          if (sanitizer.inEntity && !emitEntity(sanitizer)) return false;
          sanitizer.inTag = true;
          sanitizer.tagOverflow = false;
          sanitizer.tagLen = 0;
          continue;
        }
        if (!emitSanitizedTextByte(sanitizer, value)) return false;
      }
    }
    if (sanitizer.inEntity && !emitEntity(sanitizer)) return false;
    return (!plainText || writeLiteral(output, "</p>")) && writeLiteral(output, "</body></html>");
  }();
  if (!written) {
    imageWriter.abort();
    output.close();
    Storage.remove(partPath.c_str());
    return false;
  }
  output.flush();
  output.close();
  if (!WeReadStore::atomicReplace(partPath, outputPath) || !imageWriter.finish()) {
    Storage.remove(partPath.c_str());
    return false;
  }
  return true;
}

Error writePackageFiles(const std::string& bookDir, const WeReadStore::ShelfRecord& book, const std::string& tocPath,
                        const uint32_t chapterCount, const uint32_t firstChapter, const uint32_t lastChapter,
                        const WeReadStore::ImagePolicy imagePolicy, const std::string& workPath,
                        const WeReadProtocol::ImageType coverType, std::string& navPath, std::string& opfPath) {
  navPath = bookDir + "/nav.part";
  opfPath = bookDir + "/content.part";
  if (Storage.exists(navPath.c_str())) Storage.remove(navPath.c_str());
  if (Storage.exists(opfPath.c_str())) Storage.remove(opfPath.c_str());

  HalFile nav;
  HalFile opf;
  HalFile toc;
  uint32_t verifiedCount = 0;
  if (!Storage.openFileForWrite("WR", navPath, nav) || !Storage.openFileForWrite("WR", opfPath, opf) ||
      !WeReadStore::openToc(tocPath, toc, verifiedCount) || verifiedCount != chapterCount) {
    return Error::SdCard;
  }
  if (!writeLiteral(nav,
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                    "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
                    "xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>") ||
      !writeXmlText(nav, book.title) || !writeLiteral(nav, "</title></head><body><nav epub:type=\"toc\"><ol>") ||
      !writeLiteral(opf,
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                    "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" "
                    "unique-identifier=\"book-id\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
                    "<dc:identifier id=\"book-id\">") ||
      !writeXmlText(opf, book.bookId) || !writeLiteral(opf, "</dc:identifier><dc:title>") ||
      !writeXmlText(opf, book.title) || !writeLiteral(opf, "</dc:title><dc:creator>") ||
      !writeXmlText(opf, book.author) ||
      !writeLiteral(opf,
                    "</dc:creator><dc:language>zh-CN</dc:language></metadata><manifest>"
                    "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" "
                    "properties=\"nav\"/>")) {
    return Error::SdCard;
  }
  if (coverType != WeReadProtocol::ImageType::None &&
      (!writeLiteral(opf, "<item id=\"cover-image\" href=\"") || !writeLiteral(opf, coverEntryName(coverType)) ||
       !writeLiteral(opf, "\" media-type=\"") || !writeLiteral(opf, imageMediaType(coverType)) ||
       !writeLiteral(opf, "\" properties=\"cover-image\"/>"))) {
    return Error::SdCard;
  }

  for (uint32_t i = firstChapter; i <= lastChapter; ++i) {
    WeReadStore::TocRecord record;
    if (!WeReadStore::readTocRecord(toc, i, record)) return Error::SdCard;
    char filename[32];
    char item[192];
    snprintf(filename, sizeof(filename), "ch%06u.xhtml", static_cast<unsigned>(i));
    const int navLen = snprintf(item, sizeof(item), "<li><a href=\"%s\">", filename);
    if (navLen <= 0 || static_cast<size_t>(navLen) >= sizeof(item) || !writeLiteral(nav, item) ||
        !writeXmlText(nav, record.title) || !writeLiteral(nav, "</a></li>")) {
      return Error::SdCard;
    }
    const int opfLen =
        snprintf(item, sizeof(item), "<item id=\"ch%06u\" href=\"%s\" media-type=\"application/xhtml+xml\"/>",
                 static_cast<unsigned>(i), filename);
    if (opfLen <= 0 || static_cast<size_t>(opfLen) >= sizeof(item) || !writeLiteral(opf, item)) {
      return Error::SdCard;
    }
  }
  if (imagePolicy == WeReadStore::ImagePolicy::Embed) {
    HalFile images;
    uint32_t imageCount = 0;
    if (!WeReadStore::openImageWorkIndex(workPath, images, imageCount)) return Error::Integrity;
    for (uint32_t image = 0; image < imageCount; ++image) {
      WeReadStore::ImageWorkRecord record;
      if (!WeReadStore::readImageWorkRecord(images, image, record) || !validImageWorkRecord(record) ||
          record.state == WeReadStore::ImageWorkState::Pending) {
        return Error::Integrity;
      }
      if (record.state != WeReadStore::ImageWorkState::Complete) continue;
      char item[256];
      const int length = snprintf(item, sizeof(item), "<item id=\"img%06u\" href=\"%s\" media-type=\"%s\"/>",
                                  static_cast<unsigned>(image), record.image.href,
                                  imageMediaType(imageTypeFromHref(record.image.href)));
      if (length <= 0 || static_cast<size_t>(length) >= sizeof(item) || !writeLiteral(opf, item)) {
        return Error::SdCard;
      }
    }
  }
  if (!writeLiteral(nav, "</ol></nav></body></html>") || !writeLiteral(opf, "</manifest><spine>")) {
    return Error::SdCard;
  }
  for (uint32_t i = firstChapter; i <= lastChapter; ++i) {
    char item[64];
    snprintf(item, sizeof(item), "<itemref idref=\"ch%06u\"/>", static_cast<unsigned>(i));
    if (!writeLiteral(opf, item)) return Error::SdCard;
  }
  if (!writeLiteral(opf, "</spine></package>")) return Error::SdCard;
  nav.flush();
  opf.flush();
  return Error::Ok;
}

Error packageBook(const WeReadStore::ShelfRecord& book, const std::string& bookDir, const std::string& tocPath,
                  const uint32_t chapterCount, const uint32_t firstChapter, const uint32_t lastChapter,
                  const WeReadStore::ImagePolicy imagePolicy, const std::string& workPath, uint8_t* buffer,
                  const size_t bufferSize, const std::string& finalPartPath) {
  std::string navPath;
  std::string opfPath;
  std::string coverSourcePath;
  const WeReadProtocol::ImageType coverType = findCoverSource(bookDir, coverSourcePath);
  const Error packageFilesError = writePackageFiles(bookDir, book, tocPath, chapterCount, firstChapter, lastChapter,
                                                    imagePolicy, workPath, coverType, navPath, opfPath);
  if (packageFilesError != Error::Ok) return packageFilesError;

  const std::string centralPath = bookDir + "/central.part";
  WeReadStore::StoreOnlyZipWriter zip;
  if (!zip.begin(finalPartPath, centralPath, buffer, bufferSize)) return Error::SdCard;
  static constexpr char kMimetype[] = "application/epub+zip";
  static constexpr char kContainer[] =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
      "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
      "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
  if (!zip.addBuffer("mimetype", reinterpret_cast<const uint8_t*>(kMimetype), strlen(kMimetype)) ||
      !zip.addBuffer("META-INF/container.xml", reinterpret_cast<const uint8_t*>(kContainer), strlen(kContainer)) ||
      !zip.addFile("OEBPS/content.opf", opfPath) || !zip.addFile("OEBPS/nav.xhtml", navPath)) {
    zip.abort();
    return Error::SdCard;
  }
  if (coverType != WeReadProtocol::ImageType::None &&
      !zip.addFile(coverType == WeReadProtocol::ImageType::Png ? "OEBPS/cover.png" : "OEBPS/cover.jpg",
                   coverSourcePath)) {
    zip.abort();
    return Error::SdCard;
  }
  HalFile toc;
  uint32_t count = 0;
  if (!WeReadStore::openToc(tocPath, toc, count) || count != chapterCount) {
    zip.abort();
    return Error::SdCard;
  }
  for (uint32_t i = firstChapter; i <= lastChapter; ++i) {
    WeReadStore::TocRecord record;
    if (!WeReadStore::readTocRecord(toc, i, record)) {
      zip.abort();
      return Error::SdCard;
    }
    char entryName[48];
    snprintf(entryName, sizeof(entryName), "OEBPS/ch%06u.xhtml", static_cast<unsigned>(i));
    if (!zip.addFile(entryName, WeReadStore::chapterPath(bookDir, i))) {
      zip.abort();
      return Error::SdCard;
    }
  }
  if (imagePolicy == WeReadStore::ImagePolicy::Embed) {
    HalFile images;
    uint32_t imageCount = 0;
    if (!WeReadStore::openImageWorkIndex(workPath, images, imageCount)) {
      zip.abort();
      return Error::Integrity;
    }
    for (uint32_t image = 0; image < imageCount; ++image) {
      WeReadStore::ImageWorkRecord record;
      if (!WeReadStore::readImageWorkRecord(images, image, record) || !validImageWorkRecord(record) ||
          record.state == WeReadStore::ImageWorkState::Pending) {
        zip.abort();
        return Error::Integrity;
      }
      if (record.state != WeReadStore::ImageWorkState::Complete) continue;
      const std::string sourcePath = bookDir + "/" + record.image.href;
      char entryName[96];
      const int length = snprintf(entryName, sizeof(entryName), "OEBPS/%s", record.image.href);
      if (length <= 0 || static_cast<size_t>(length) >= sizeof(entryName) || !zip.addFile(entryName, sourcePath)) {
        zip.abort();
        return Error::SdCard;
      }
    }
  }
  if (!zip.finish() || !WeReadStore::looksLikeZip(finalPartPath)) return Error::Integrity;
  Storage.remove(navPath.c_str());
  Storage.remove(opfPath.c_str());
  return Error::Ok;
}

void cleanupTransient(const std::string& bookDir, const std::string& finalPartPath) {
  static constexpr const char* kNames[] = {"/shard0.part",  "/shard1.part",     "/shard3.part", "/encoded.part",
                                           "/decoded.part", "/central.part",    "/nav.part",    "/content.part",
                                           "/images.work",  "/images.work.part"};
  for (const char* name : kNames) {
    const std::string path = bookDir + name;
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  }
  if (!finalPartPath.empty() && Storage.exists(finalPartPath.c_str())) Storage.remove(finalPartPath.c_str());
}

void cleanupDetailTransient(const std::string& bookDir) {
  static constexpr const char* kNames[] = {"/detail.bin.part", "/cover.bmp.part", "/cover.source.jpg.part",
                                           "/cover.source.png.part"};
  for (const char* name : kNames) {
    const std::string path = bookDir + name;
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  }
}

}  // namespace

bool Operation::active() const {
  switch (phase_) {
    case Phase::Idle:
    case Phase::Complete:
    case Phase::Cancelled:
    case Phase::Failed:
      return false;
    case Phase::LoginUid:
    case Phase::LoginPollWait:
    case Phase::LoginPoll:
    case Phase::SyncShelf:
    case Phase::Renew:
    case Phase::PrepareDetail:
    case Phase::FetchDetail:
    case Phase::FetchCover:
    case Phase::ConvertCover:
    case Phase::PrepareDownload:
    case Phase::FetchToc:
    case Phase::PrepareProgressSync:
    case Phase::FetchProgress:
    case Phase::DecideProgress:
    case Phase::FetchProgressReader:
    case Phase::SendProgressEnter:
    case Phase::SendProgressReport:
    case Phase::VerifyProgress:
    case Phase::OpenToc:
    case Phase::AwaitChapterRange:
    case Phase::LoadChapter:
    case Phase::SyncClock:
    case Phase::FetchReader:
    case Phase::FetchPrimary:
    case Phase::FetchText0:
    case Phase::FetchText1:
    case Phase::FetchEpub1:
    case Phase::FetchEpub3:
    case Phase::DecodeText:
    case Phase::DecodeEpub:
    case Phase::AdvanceChapter:
    case Phase::PrepareImages:
    case Phase::DownloadImages:
    case Phase::PackageBook:
      return true;
  }
  return false;
}

void Operation::reset() {
  bookSession_.reset();
  if (kind_ == Kind::Download && active() && !bookDir_.empty()) {
    cleanupTransient(bookDir_, finalPartPath_);
  }
  if (kind_ == Kind::Detail && !bookDir_.empty()) cleanupDetailTransient(bookDir_);
  if (tocFile_.isOpen()) tocFile_.close();
  phase_ = Phase::Idle;
  resumePhase_ = Phase::Idle;
  kind_ = Kind::Sync;
  error_ = Error::Ok;
  progressStage_ = ProgressStage::Chapters;
  options_ = {};
  progressSyncInput_ = {};
  progressSyncMode_ = ProgressSyncMode::Compare;
  progressSyncResult_ = {};
  session_.clear();
  book_ = {};
  chapter_ = {};
  chapterCount_ = 0;
  firstChapterIndex_ = 0;
  lastChapterIndex_ = 0;
  chapterIndex_ = 0;
  progressCompleted_ = 0;
  progressTotal_ = 0;
  progressChapterOffset_ = 0;
  imageWorkCount_ = 0;
  imageWorkCursor_ = 0;
  imageDownloaded_ = 0;
  imageCached_ = 0;
  imageSkipped_ = 0;
  imageRedirects_ = 0;
  imageFilesCreated_ = 0;
  imageBytes_ = 0;
  requestAttempt_ = 0;
  progressVerifyAttempts_ = 0;
  chapterResponseAttempts_ = 0;
  coverAttempts_ = 0;
  coverRedirects_ = 0;
  coverState_ = WeReadStore::ImageWorkState::Pending;
  cancelRequested_ = false;
  renewalAttempted_ = false;
  loginRecoveryAttempted_ = false;
  loginConfirmed_ = false;
  loginStartedAt_ = 0;
  nextActionAt_ = 0;
  lastShardRequestAt_ = 0;
  imagePhaseStartedAt_ = 0;
  responseStatus_ = 0;
  progressUploadStartedAt_ = 0;
  previousVid_[0] = '\0';
  loginUid_[0] = '\0';
  psvts_[0] = '\0';
  initialProgressFraction_ = 0.0f;
  initialProgressValid_ = false;
  imageHost_[0] = '\0';
  coverType_ = WeReadProtocol::ImageType::None;
  // Shelf sync and download are separate jobs on the same account.
  // startLogin() clears runtime cookies before an account can change.
  url_[0] = '\0';
  referer_.clear();
  bookDir_.clear();
  tocPath_.clear();
  outputPath_.clear();
  finalPartPath_.clear();
}

bool Operation::begin(const Kind kind, const WeReadStore::ShelfRecord* book, const DownloadOptions options) {
  reset();
  if (kind == Kind::ProgressSync) {
    error_ = Error::Protocol;
    phase_ = Phase::Failed;
    return false;
  }
  kind_ = kind;
  if (kind != Kind::Sync) {
    if (!book || !isSafeProtocolToken(book->bookId)) {
      error_ = Error::Protocol;
      phase_ = Phase::Failed;
      return false;
    }
    if (kind == Kind::Download) {
      switch (options.imagePolicy) {
        case WeReadStore::ImagePolicy::Embed:
        case WeReadStore::ImagePolicy::Exclude:
          break;
        default:
          error_ = Error::Protocol;
          phase_ = Phase::Failed;
          return false;
      }
      options_ = options;
    }
    book_ = *book;
  }
  WeReadStore::loadSession(session_);
  Phase first = Phase::SyncShelf;
  switch (kind) {
    case Kind::Sync:
      first = Phase::SyncShelf;
      break;
    case Kind::Detail:
      first = Phase::PrepareDetail;
      break;
    case Kind::Download:
      first = Phase::PrepareDownload;
      break;
    case Kind::ProgressSync:
      break;
  }
  if (session_.valid()) {
    phase_ = first;
  } else {
    startLogin(first);
  }
  logMemory("job start");
  return true;
}

bool Operation::beginProgressSync(const char* bookId, ProgressSyncInput input, const ProgressSyncMode mode) {
  reset();
  if (!isSafeProtocolToken(bookId) || !std::isfinite(input.localFraction)) {
    error_ = Error::Protocol;
    phase_ = Phase::Failed;
    return false;
  }
  kind_ = Kind::ProgressSync;
  strncpy(book_.bookId, bookId, sizeof(book_.bookId) - 1);
  progressSyncInput_ = input;
  progressSyncMode_ = mode;
  progressSyncInput_.localFraction = std::max(0.0f, std::min(1.0f, input.localFraction));
  WeReadStore::loadSession(session_);
  if (!session_.valid()) {
    error_ = Error::SessionExpired;
    phase_ = Phase::Failed;
    return false;
  }
  if (session_.rt[0]) {
    renewalAttempted_ = true;
    resumePhase_ = Phase::PrepareProgressSync;
    phase_ = Phase::Renew;
  } else {
    phase_ = Phase::PrepareProgressSync;
  }
  logMemory("progress sync start");
  return true;
}

bool Operation::setChapterRange(const uint32_t first, const uint32_t last) {
  if (phase_ != Phase::AwaitChapterRange || !validChapterRange(first, last, chapterCount_)) return false;
  firstChapterIndex_ = first;
  lastChapterIndex_ = last;
  chapterIndex_ = first;
  progressCompleted_ = 0;
  progressTotal_ = chapterRangeCount(first, last, chapterCount_);
  psvts_[0] = '\0';
  phase_ = Phase::LoadChapter;
  return true;
}

bool Operation::readChapter(const uint32_t index, WeReadStore::TocRecord& record) {
  return phase_ == Phase::AwaitChapterRange && index < chapterCount_ && tocFile_.isOpen() &&
         WeReadStore::readTocRecord(tocFile_, index, record);
}

void Operation::cancel() {
  if (active()) cancelRequested_ = true;
}

Operation::Event Operation::cancelNow() {
  bookSession_.reset();
  if (tocFile_.isOpen()) tocFile_.close();
  if (kind_ == Kind::Download && !bookDir_.empty()) {
    cleanupTransient(bookDir_, finalPartPath_);
  }
  if (kind_ == Kind::Detail && !bookDir_.empty()) cleanupDetailTransient(bookDir_);
  error_ = Error::Cancelled;
  phase_ = Phase::Cancelled;
  logMemory("job cancelled");
  return Event::Cancelled;
}

void Operation::startLogin(const Phase resume) {
  memcpy(previousVid_, session_.vid, sizeof(previousVid_));
  previousVid_[sizeof(previousVid_) - 1] = '\0';
  session_.clear();
  loginUid_[0] = '\0';
  cookie_[0] = '\0';
  url_[0] = '\0';
  loginConfirmed_ = false;
  loginStartedAt_ = millis();
  nextActionAt_ = 0;
  requestAttempt_ = 0;
  resumePhase_ = resume;
  phase_ = Phase::LoginUid;
}

void Operation::requestAuthentication(const Phase resume) {
  bookSession_.reset();
  resumePhase_ = resume;
  requestAttempt_ = 0;
  if (session_.rt[0] && !renewalAttempted_) {
    renewalAttempted_ = true;
    phase_ = Phase::Renew;
    return;
  }
  if (kind_ == Kind::ProgressSync) {
    error_ = Error::SessionExpired;
    phase_ = Phase::Failed;
    return;
  }
  if (!loginRecoveryAttempted_) {
    loginRecoveryAttempted_ = true;
    startLogin(resume);
    return;
  }
  error_ = Error::SessionExpired;
  phase_ = Phase::Failed;
}

Operation::Event Operation::fail(const Error error) {
  const Phase failedPhase = phase_;
  bookSession_.reset();
  error_ = error;
  if (tocFile_.isOpen()) tocFile_.close();
  if (kind_ == Kind::Download && !bookDir_.empty()) {
    const std::string chapterPart = WeReadStore::chapterPath(bookDir_, chapterIndex_) + ".part";
    const std::string imageIndexPart = WeReadStore::imageIndexPath(bookDir_, chapterIndex_) + ".part";
    if (Storage.exists(chapterPart.c_str())) Storage.remove(chapterPart.c_str());
    if (Storage.exists(imageIndexPart.c_str())) Storage.remove(imageIndexPart.c_str());
    cleanupTransient(bookDir_, finalPartPath_);
  } else if (kind_ == Kind::Detail && !bookDir_.empty()) {
    cleanupDetailTransient(bookDir_);
  }
  phase_ = Phase::Failed;
  LOG_ERR("WR", "job failed: phase=%u error=%u", static_cast<unsigned>(failedPhase), static_cast<unsigned>(error));
  logMemory("job failed");
  return Event::Failed;
}

Operation::Event Operation::handleRequestError(const Error error, const Phase retryPhase) {
  if (error == Error::Network && !WeReadHttpClient::networkReady()) return fail(error);
  if (error == Error::Network && requestAttempt_ < kMaxRequestAttempts - 1) {
    ++requestAttempt_;
    const unsigned long delayMs = kNetworkRetryBaseMs * requestAttempt_;
    nextActionAt_ = millis() + delayMs;
    phase_ = retryPhase;
    LOG_INF("WR", "network retry: phase=%u retry=%u/%u delay=%u", static_cast<unsigned>(retryPhase),
            static_cast<unsigned>(requestAttempt_), static_cast<unsigned>(kMaxRequestAttempts - 1),
            static_cast<unsigned>(delayMs));
    return Event::None;
  }
  return fail(error);
}

Operation::Event Operation::retryChapterResponse() {
  const Event event = chapterResponseRetryEvent(++chapterResponseAttempts_);
  if (event == Event::Failed) return fail(Error::Unavailable);
  psvts_[0] = '\0';
  requestAttempt_ = 0;
  nextActionAt_ = millis() + kNetworkRetryBaseMs * chapterResponseAttempts_;
  phase_ = chapterResponseRetryPhase();
  LOG_INF("WR", "chapter response retry: retry=%u/%u delay=%u", static_cast<unsigned>(chapterResponseAttempts_),
          static_cast<unsigned>(kMaxRequestAttempts - 1),
          static_cast<unsigned>(kNetworkRetryBaseMs * chapterResponseAttempts_));
  return event;
}

Operation::Event Operation::reauthenticateChapter() {
  psvts_[0] = '\0';
  requestAuthentication(Phase::LoadChapter);
  return phase_ == Phase::Failed ? fail(error_) : Event::None;
}

void Operation::requestSucceeded() {
  requestAttempt_ = 0;
  nextActionAt_ = 0;
  if (kind_ != Kind::ProgressSync) {
    renewalAttempted_ = false;
    loginRecoveryAttempted_ = false;
  }
}

void Operation::guardBookSession(const char* phase) {
  if (!bookSession_.reusable()) return;
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t largestBlock = ESP.getMaxAllocHeap();
  LOG_DBG("WR", "book TLS guard: phase=%s free=%u largest=%u stack=%u", phase ? phase : "?",
          static_cast<unsigned>(freeHeap), static_cast<unsigned>(largestBlock),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  if (freeHeap >= kBookSessionMinFreeHeap && largestBlock >= kBookSessionMinLargestBlock) return;
  LOG_INF("WR", "book TLS fallback: phase=%s requiredFree=%u requiredLargest=%u", phase ? phase : "?",
          static_cast<unsigned>(kBookSessionMinFreeHeap), static_cast<unsigned>(kBookSessionMinLargestBlock));
  bookSession_.reset();
}

bool Operation::preparePaths() {
  bookDir_ = WeReadStore::bookDirectory(book_.bookId);
  outputPath_ = WeReadStore::finalBookPath(book_);
  finalPartPath_ = outputPath_ + ".part";
  tocPath_ = bookDir_ + "/toc.bin";
  const std::string chaptersDir = bookDir_ + "/chapters";
  const std::string imagesDir = bookDir_ + "/images";
  return WeReadStore::ensureRoot() && Storage.ensureDirectoryExists(bookDir_.c_str()) &&
         Storage.ensureDirectoryExists(chaptersDir.c_str()) && Storage.ensureDirectoryExists(imagesDir.c_str()) &&
         Storage.ensureDirectoryExists("/WeRead");
}

bool Operation::waitForShardPace() {
  const unsigned long now = millis();
  if (lastShardRequestAt_ && now - lastShardRequestAt_ < kShardPaceMs) return false;
  lastShardRequestAt_ = now;
  return true;
}

Error Operation::fetchLoginUid() {
  SimpleJsonContext context;
  StreamingJsonParser parser(simpleCallbacks(&context));
  context.parser = &parser;
  ResponseSink sink{&context, resetSimple, feedSimple, noOpFinish, Error::Protocol};
  const Error error =
      requestOnce("GET", "/api/auth/getLoginUid", nullptr, 0, &session_, kDefaultReferer, sink, responseStatus_,
                  cookie_, sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_));
  if (error != Error::Ok) return error;
  if (responseStatus_ != 200 || !context.rootClosed || !context.uid[0]) return Error::LoginFailed;
  memcpy(loginUid_, context.uid, sizeof(loginUid_));
  loginUid_[sizeof(loginUid_) - 1] = '\0';
  const int len = snprintf(url_, sizeof(url_), "%s/web/confirm?uid=%s", kHost, loginUid_);
  return len > 0 && static_cast<size_t>(len) < sizeof(url_) ? Error::Ok : Error::Protocol;
}

Error Operation::pollLogin() {
  SimpleJsonContext context;
  StreamingJsonParser parser(simpleCallbacks(&context));
  context.parser = &parser;
  ResponseSink sink{&context, resetSimple, feedSimple, noOpFinish, Error::Protocol};
  char path[256];
  const int len = snprintf(path, sizeof(path), "/api/auth/getLoginInfo?uid=%s&otp=", loginUid_);
  if (len <= 0 || static_cast<size_t>(len) >= sizeof(path)) return Error::Protocol;
  const Error error = requestOnce("GET", path, nullptr, 0, &session_, kDefaultReferer, sink, responseStatus_, cookie_,
                                  sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_));
  if (error != Error::Ok) return error;
  if (responseStatus_ != 200 || !context.rootClosed) return Error::Network;
  if (!context.succeed) {
    if (context.logicCode[0] && strcmp(context.logicCode, "LOGIN_TIMEOUT") != 0) return Error::LoginFailed;
    return Error::Ok;
  }
  if (!context.vid[0] || !context.token[0] || !session_.setCookie("wr_vid", context.vid, strlen(context.vid)) ||
      !session_.setCookie("wr_skey", context.token, strlen(context.token))) {
    return Error::LoginFailed;
  }
  if ((!previousVid_[0] || strcmp(previousVid_, session_.vid) != 0) && !WeReadStore::clearShelf()) {
    return Error::SdCard;
  }
  if (!WeReadStore::saveSession(session_)) return Error::SdCard;
  loginConfirmed_ = true;
  return Error::Ok;
}

Error Operation::renewSession() {
  if (!session_.rt[0]) return Error::SessionExpired;
  SimpleJsonContext context;
  StreamingJsonParser parser(simpleCallbacks(&context));
  context.parser = &parser;
  ResponseSink sink{&context, resetSimple, feedSimple, noOpFinish, Error::Protocol};
  static constexpr char kRenewBody[] = "{\"rq\":\"%2Fweb%2Fbook%2Fread\",\"ql\":false}";
  const Error error = requestOnce("POST", "/web/login/renewal", reinterpret_cast<const uint8_t*>(kRenewBody),
                                  sizeof(kRenewBody) - 1, &session_, kDefaultReferer, sink, responseStatus_, cookie_,
                                  sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_));
  if (error != Error::Ok) return error;
  if (responseStatus_ != 200 || !context.rootClosed || context.errorCode != 0 || !context.succeed ||
      !session_.valid()) {
    WeReadStore::clearSession();
    return Error::SessionExpired;
  }
  return WeReadStore::saveSession(session_) ? Error::Ok : Error::SdCard;
}

Error Operation::syncShelfOnce() {
  ShelfJsonContext context;
  StreamingJsonParser parser(shelfCallbacks(&context));
  context.parser = &parser;
  ResponseSink sink{&context, resetShelf, feedShelf, noOpFinish, Error::Protocol};
  const Error error =
      requestOnce("GET", "/web/shelf/sync", nullptr, 0, &session_, kDefaultReferer, sink, responseStatus_, cookie_,
                  sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_));
  if (error != Error::Ok) {
    context.writer.abort();
    return error;
  }
  if (context.errorCode == -2012) {
    context.writer.abort();
    return Error::SessionExpired;
  }
  if (responseStatus_ != 200 || context.errorCode != 0 || !context.rootClosed || parser.hasError() ||
      context.writeFailed) {
    context.writer.abort();
    return Error::Protocol;
  }
  if (!context.writer.finish()) return Error::SdCard;
  switch (WeReadStore::sortShelfByRecent()) {
    case WeReadStore::ShelfSortResult::Ok:
      break;
    case WeReadStore::ShelfSortResult::OutOfMemory:
      return Error::OutOfMemory;
    case WeReadStore::ShelfSortResult::StorageError:
      return Error::SdCard;
  }
  logMemory("shelf parsed");
  return WeReadStore::saveSession(session_) ? Error::Ok : Error::SdCard;
}

Error Operation::fetchDetailOnce() {
  DetailJsonContext context;
  context.book = &book_;
  context.bookDir = &bookDir_;
  WeReadProtocol::JsonStringDecoder decoder(writeDetailIntro, &context);
  context.introDecoder = &decoder;
  StreamingJsonParser parser(detailCallbacks(&context));
  context.parser = &parser;
  ResponseSink sink{&context, resetDetail, feedDetail, finishDetail, Error::SdCard};

  char encodedBookId[192];
  if (!WeReadProtocol::urlEncode(book_.bookId, encodedBookId, sizeof(encodedBookId))) return Error::Protocol;
  referer_ = "/web/book/info?bookId=";
  referer_ += encodedBookId;
  const Error error =
      requestOnce("GET", referer_.c_str(), nullptr, 0, &session_, kDefaultReferer, sink, responseStatus_, cookie_,
                  sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_), &bookSession_);
  if (error != Error::Ok) {
    context.writer.abort();
    return error;
  }
  if (context.errorCode == -2012) {
    context.writer.abort();
    return Error::SessionExpired;
  }
  if (responseStatus_ != 200 || context.errorCode != 0 || !context.rootClosed || parser.hasError() ||
      context.writeFailed || !context.header.title[0]) {
    context.writer.abort();
    return Error::Protocol;
  }
  if (!context.writer.finish(context.header)) return Error::SdCard;
  coverType_ = WeReadProtocol::normalizeImageUrl(context.header.coverUrl, url_, sizeof(url_));
  if (coverType_ == WeReadProtocol::ImageType::None) url_[0] = '\0';
  logMemory("detail parsed");
  return WeReadStore::saveSession(session_) ? Error::Ok : Error::SdCard;
}

Error Operation::fetchTocOnce() {
  TocJsonContext context;
  context.path = tocPath_;
  StreamingJsonParser parser(tocCallbacks(&context));
  context.parser = &parser;
  ResponseSink sink{&context, resetToc, feedToc, noOpFinish, Error::Protocol};
  const int bodySize =
      snprintf(reinterpret_cast<char*>(ioBuffer_), sizeof(ioBuffer_), "{\"bookIds\":[\"%s\"]}", book_.bookId);
  if (bodySize <= 0 || static_cast<size_t>(bodySize) >= sizeof(ioBuffer_)) return Error::Protocol;
  const Error error = requestOnce("POST", "/web/book/chapterInfos", ioBuffer_, static_cast<size_t>(bodySize), &session_,
                                  kDefaultReferer, sink, responseStatus_, cookie_, sizeof(cookie_), url_, sizeof(url_),
                                  ioBuffer_, sizeof(ioBuffer_), &bookSession_);
  if (error != Error::Ok) {
    context.writer.abort();
    return error;
  }
  if (context.errorCode == -2012) {
    context.writer.abort();
    return Error::SessionExpired;
  }
  if (responseStatus_ != 200 || context.errorCode != 0 || !context.rootClosed || parser.hasError() ||
      context.writeFailed || context.writer.count() == 0) {
    context.writer.abort();
    return Error::Protocol;
  }
  return context.writer.finish() ? Error::Ok : Error::SdCard;
}

Error Operation::fetchProgressOnce(const bool bypassCache) {
  WeReadProtocol::RemoteProgressParser parser(book_.bookId);
  ResponseSink sink{&parser, resetRemoteProgress, feedRemoteProgress, noOpFinish, Error::Protocol};
  if (!WeReadProtocol::urlEncode(book_.bookId, url_, sizeof(url_))) return Error::Protocol;
  referer_ = "/web/book/getProgress?bookId=";
  referer_ += url_;
  if (bypassCache) {
    const uint64_t cacheKey = static_cast<uint64_t>(TimeUtils::getCurrentValidTimestamp()) * 1000ULL + millis() % 1000;
    char suffix[32];
    const int length = snprintf(suffix, sizeof(suffix), "&_=%llu", static_cast<unsigned long long>(cacheKey));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(suffix)) return Error::Protocol;
    referer_ += suffix;
  }
  const Error error =
      requestOnce("GET", referer_.c_str(), nullptr, 0, &session_, kDefaultReferer, sink, responseStatus_, cookie_,
                  sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_), &bookSession_);
  if (error != Error::Ok) return error;
  if (responseStatus_ == 401 || responseStatus_ == 403 || parser.errorCode() == -2012) {
    return Error::SessionExpired;
  }
  if (responseStatus_ != 200 || parser.errorCode() != 0 || !parser.complete()) {
    return Error::Protocol;
  }
  progressSyncResult_.remote = parser.progress();
  return WeReadStore::saveSession(session_) ? Error::Ok : Error::SdCard;
}

float Operation::normalizedRemoteProgress() const {
  const auto& remote = progressSyncResult_.remote;
  float remoteFraction = remote.percent / 100.0f;
  if (remote.hasChapterOffset) {
    float mapped = 0.0f;
    if (WeReadStore::mapChapterToFraction(tocPath_, remote.chapterUid, remote.chapterOffset, mapped)) {
      remoteFraction = mapped;
    }
  }
  return std::max(0.0f, std::min(1.0f, remoteFraction));
}

bool Operation::sameRemotePosition() const {
  const auto& remote = progressSyncResult_.remote;
  if (remote.hasChapterOffset && remote.chapterUid[0] && chapter_.chapterUid[0]) {
    return strcmp(remote.chapterUid, chapter_.chapterUid) == 0 && remote.chapterOffset == progressChapterOffset_;
  }
  const uint32_t localMillionths =
      static_cast<uint32_t>(std::max(0.0f, std::min(1.0f, progressSyncInput_.localFraction)) * 1000000.0f + 0.5f);
  const uint32_t remoteMillionths = static_cast<uint32_t>(normalizedRemoteProgress() * 1000000.0f + 0.5f);
  return localMillionths == remoteMillionths;
}

bool Operation::remoteAppIdMatchesLocal() const {
  const auto& remote = progressSyncResult_.remote;
  if (!remote.hasAppId) return false;
  char localAppId[64];
  return makeWebAppId(localAppId, sizeof(localAppId)) &&
         remote.appIdHash == WeReadProtocol::hashAppId(localAppId, strlen(localAppId));
}

void Operation::persistInitialProgress() {
  const bool saved = initialProgressValid_ && initialProgressFraction_ > 0.0f
                         ? WeReadStore::saveInitialProgress(book_.bookId, initialProgressFraction_)
                         : WeReadStore::clearInitialProgress(book_.bookId);
  if (saved) return;
  LOG_ERR("WR", "Failed to update initial progress for %s", book_.bookId);
  WeReadStore::clearInitialProgress(book_.bookId);
}

Error Operation::decideProgress() {
  const float remoteFraction = normalizedRemoteProgress();
  progressSyncResult_.remote.percent = remoteFraction * 100.0f;
  const bool preciseLocal =
      progressSyncInput_.hasLocalTocIndex &&
      WeReadStore::mapPageToChapter(tocPath_, progressSyncInput_.localTocIndex, progressSyncInput_.localPageNumber,
                                    progressSyncInput_.localPageCount, chapter_, progressChapterOffset_,
                                    progressSyncInput_.localFraction);
  if (!preciseLocal && !WeReadStore::mapFractionToChapter(tocPath_, progressSyncInput_.localFraction, chapter_,
                                                          progressChapterOffset_)) {
    return Error::Unavailable;
  }
  LOG_INF("WR", "local progress mapping: precise=%u toc=%u chapter=%s offset=%u fraction=%lu",
          static_cast<unsigned>(preciseLocal), static_cast<unsigned>(progressSyncInput_.localTocIndex),
          chapter_.chapterUid, static_cast<unsigned>(progressChapterOffset_),
          static_cast<unsigned long>(progressSyncInput_.localFraction * 1000000.0f + 0.5f));
  const bool samePosition = sameRemotePosition();
  const ProgressAction action = progressAction(progressSyncMode_, samePosition);
  LOG_INF("WR", "progress decision: mode=%u same=%u action=%u", static_cast<unsigned>(progressSyncMode_),
          static_cast<unsigned>(samePosition), static_cast<unsigned>(action));
  switch (action) {
    case ProgressAction::AlreadySynced:
      progressSyncResult_.outcome = ProgressSyncOutcome::AlreadySynced;
      return Error::Ok;
    case ProgressAction::SelectDirection:
      progressSyncResult_.outcome = ProgressSyncOutcome::SelectionRequired;
      return Error::Ok;
    case ProgressAction::ApplyRemote:
      progressSyncResult_.outcome = ProgressSyncOutcome::ApplyRemote;
      return Error::Ok;
    case ProgressAction::UploadLocal:
      if (!makeReaderReferer(book_.bookId, chapter_.chapterUid, referer_)) return Error::Unavailable;
      progressUploadStartedAt_ = TimeUtils::getCurrentValidTimestamp();
      return progressUploadStartedAt_ == 0 ? Error::Clock : Error::Ok;
  }
  return Error::Protocol;
}

Error Operation::fetchProgressReaderOnce() {
  const size_t hostLength = strlen(kHost);
  if (referer_.compare(0, hostLength, kHost) != 0) return Error::Protocol;
  // Image downloads and login are inactive here; reuse their fixed scratch buffers.
  ReaderContextSink context(psvts_, sizeof(psvts_), imageHost_, sizeof(imageHost_), previousVid_, sizeof(previousVid_));
  ResponseSink sink{&context, resetReaderContext, extractReaderContext, noOpFinish, Error::Protocol};
  const Error error =
      requestOnce("GET", referer_.c_str() + hostLength, nullptr, 0, &session_, referer_.c_str(), sink, responseStatus_,
                  cookie_, sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_), &bookSession_);
  if (error != Error::Ok) return error;
  if (responseStatus_ == 401 || responseStatus_ == 403) return Error::SessionExpired;
  if (responseStatus_ != 200 || !context.psvts.complete() || !isSafeProtocolToken(psvts_)) return Error::Protocol;
  if (imageHost_[0] && !isSafeProtocolToken(imageHost_)) imageHost_[0] = '\0';
  if (previousVid_[0] && !isSafeProtocolToken(previousVid_)) previousVid_[0] = '\0';
  return Error::Ok;
}

Error Operation::sendProgressOnce(const bool report) {
  size_t bodySize = 0;
  if (!makeProgressBody(book_.bookId, chapter_, progressChapterOffset_, progressSyncInput_.localFraction, psvts_,
                        imageHost_, previousVid_, report, reinterpret_cast<char*>(ioBuffer_), sizeof(ioBuffer_), url_,
                        sizeof(url_), bodySize)) {
    return Error::Clock;
  }
  SimpleJsonContext context;
  StreamingJsonParser parser(simpleCallbacks(&context));
  context.parser = &parser;
  ResponseSink sink{&context, resetSimple, feedSimple, noOpFinish, Error::Protocol};
  const Error error =
      requestOnce("POST", "/web/book/read", ioBuffer_, bodySize, &session_, referer_.c_str(), sink, responseStatus_,
                  cookie_, sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_), &bookSession_);
  if (error != Error::Ok) return error;
  if (responseStatus_ == 401 || responseStatus_ == 403 || context.errorCode == -2012) {
    return Error::SessionExpired;
  }
  const bool emptyBody = context.bytesReceived == 0;
  if (responseStatus_ != 200 || context.errorCode != 0 || parser.hasError() ||
      (!emptyBody && (!context.rootClosed || (!context.succeed && !context.hasSyncKey)))) {
    return Error::Protocol;
  }
  return WeReadStore::saveSession(session_) ? Error::Ok : Error::SdCard;
}

Error Operation::fetchReaderOnce() {
  const size_t hostLength = strlen(kHost);
  if (referer_.compare(0, hostLength, kHost) != 0) return Error::Protocol;
  WeReadProtocol::PsvtsExtractor context(psvts_, sizeof(psvts_));
  ResponseSink sink{&context, resetPsvts, extractPsvts, noOpFinish, Error::Protocol};
  const Error error =
      requestOnce("GET", referer_.c_str() + hostLength, nullptr, 0, &session_, referer_.c_str(), sink, responseStatus_,
                  cookie_, sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_), &bookSession_);
  if (error != Error::Ok) return error;
  if (responseStatus_ == 401) return Error::SessionExpired;
  if (responseStatus_ == 403 || (responseStatus_ == 200 && !context.complete())) return Error::Unavailable;
  if (responseStatus_ != 200 || !isSafeProtocolToken(psvts_)) return Error::Protocol;
  return Error::Ok;
}

Error Operation::fetchShardOnce(const char* endpoint, const std::string& destination) {
  size_t bodySize = 0;
  if (!makeContentBody(book_.bookId, chapter_.chapterUid, psvts_, reinterpret_cast<char*>(ioBuffer_), sizeof(ioBuffer_),
                       bodySize)) {
    return Error::Clock;
  }
  FileSink context;
  context.path = &destination;
  ResponseSink sink{&context, resetFile, writeFile, finishFile, Error::SdCard};
  return requestOnce("POST", endpoint, ioBuffer_, bodySize, &session_, referer_.c_str(), sink, responseStatus_, cookie_,
                     sizeof(cookie_), url_, sizeof(url_), ioBuffer_, sizeof(ioBuffer_), &bookSession_);
}

Operation::Event Operation::finishWholeBook(const std::string& source) {
  bookSession_.reset();
  if (!wholeChapterRange(firstChapterIndex_, lastChapterIndex_, chapterCount_)) {
    return fail(Error::WholeBookOnly);
  }
  if (!WeReadStore::looksLikeZip(source)) return fail(Error::Integrity);
  WeReadStore::BookOptions previousOptions;
  const bool hadPreviousOptions = WeReadStore::loadBookOptions(bookDir_, previousOptions);
  const std::string optionsPath = WeReadStore::optionsPath(bookDir_);
  if (Storage.exists(optionsPath.c_str()) && !Storage.remove(optionsPath.c_str())) {
    return fail(Error::SdCard);
  }
  if (Storage.exists(finalPartPath_.c_str())) Storage.remove(finalPartPath_.c_str());
  if (!Storage.rename(source.c_str(), finalPartPath_.c_str()) ||
      !WeReadStore::atomicReplace(finalPartPath_, outputPath_)) {
    if (hadPreviousOptions && !WeReadStore::saveBookOptions(bookDir_, previousOptions)) {
      LOG_ERR("WR", "Failed to restore book options after whole EPUB replacement failure");
    }
    return fail(Error::SdCard);
  }
  cleanupTransient(bookDir_, "");
  if (!WeReadStore::saveSession(session_)) return fail(Error::SdCard);
  persistInitialProgress();
  if (tocFile_.isOpen()) tocFile_.close();
  phase_ = Phase::Complete;
  logJobComplete();
  return Event::Complete;
}

Error Operation::prepareImageWork() {
  const std::string workPath = WeReadStore::imageWorkPath(bookDir_);
  WeReadStore::IndexWriter writer;
  if (!writer.begin(workPath, WeReadStore::kImageWorkMagic, sizeof(WeReadStore::ImageWorkRecord))) {
    return Error::SdCard;
  }

  progressStage_ = ProgressStage::Images;
  progressCompleted_ = 0;
  progressTotal_ = 0;
  imageDownloaded_ = 0;
  imageCached_ = 0;
  imageSkipped_ = 0;
  imageRedirects_ = 0;
  imageFilesCreated_ = 0;
  imageBytes_ = 0;
  imagePhaseStartedAt_ = millis();
  for (uint32_t chapter = firstChapterIndex_; chapter <= lastChapterIndex_; ++chapter) {
    HalFile images;
    uint32_t imageCount = 0;
    if (!WeReadStore::openImageIndex(WeReadStore::imageIndexPath(bookDir_, chapter), images, imageCount)) {
      writer.abort();
      return Error::Integrity;
    }
    for (uint32_t image = 0; image < imageCount; ++image) {
      WeReadStore::ImageWorkRecord work;
      if (!WeReadStore::readImageRecord(images, image, work.image) || !validImageRecord(work.image)) {
        writer.abort();
        return Error::Integrity;
      }
      if (validImageFile(bookDir_ + "/" + work.image.href, imageTypeFromHref(work.image.href))) {
        work.state = WeReadStore::ImageWorkState::Complete;
        ++imageCached_;
        ++progressCompleted_;
      }
      if (!writer.append(&work)) {
        writer.abort();
        return Error::SdCard;
      }
      ++progressTotal_;
    }
  }
  if (!writer.finish()) return Error::SdCard;
  imageWorkCount_ = progressTotal_;
  imageWorkCursor_ = 0;
  imageHost_[0] = '\0';
  if (tocFile_.isOpen()) tocFile_.close();
  uint32_t verifiedCount = 0;
  if (!WeReadStore::openImageWorkIndexForUpdate(workPath, tocFile_, verifiedCount) ||
      verifiedCount != imageWorkCount_) {
    return Error::Integrity;
  }
  bookSession_.reset();
  bookSession_.clearStats();
  return Error::Ok;
}

Error Operation::requestImage(WeReadStore::ImageRecord& image, WeReadStore::ImageWorkState& state, uint8_t& attempts,
                              uint8_t& redirects, const bool trackProgress) {
  const WeReadProtocol::ImageType type = imageTypeFromHref(image.href);
  const std::string destination = bookDir_ + "/" + image.href;
  if (validImageFile(destination, type)) {
    state = WeReadStore::ImageWorkState::Complete;
    if (trackProgress) {
      ++imageCached_;
      ++progressCompleted_;
    }
    return Error::Ok;
  }
  const std::string partPath = destination + ".part";
  referer_ = image.url;

  FileSink file;
  file.path = &partPath;
  file.maxSize = kMaxImageBytes;
  file.cancelRequested = &cancelRequested_;

  WeReadHttpClient::Header headers[4] = {{"User-Agent", kUserAgent},
                                         {"Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8"},
                                         {"Referer", kDefaultReferer}};
  size_t headerCount = 3;
  cookie_[0] = '\0';
  if (isWereadUrl(referer_.c_str())) {
    if (!session_.cookieHeader(cookie_, sizeof(cookie_))) {
      return Error::Protocol;
    }
    headers[headerCount++] = {"Cookie", cookie_};
  }

  WeReadHttpClient::RequestOptions options;
  options.headers = headers;
  options.headerCount = headerCount;
  options.timeoutMs = kRequestTimeoutMs;
  options.readBuffer = ioBuffer_;
  options.readBufferSize = sizeof(ioBuffer_);

  bool hasLocation = false;
  bool locationValid = true;
  url_[0] = '\0';
  responseStatus_ = -1;
  const auto onData = [this, &file](const uint8_t* data, const size_t len) {
    if (responseStatus_ != 200) return true;
    if (!file.file.isOpen()) {
      if (!resetFile(&file)) {
        file.failure = FileSink::Failure::SdCard;
        return false;
      }
    }
    return writeFile(&file, data, len);
  };
  const auto onHeader = [this, &hasLocation, &locationValid](const char* name, const char* value) {
    if (!equalsIgnoreCase(name, "location") || !value) return;
    const size_t length = strlen(value);
    hasLocation = true;
    if (length >= sizeof(url_)) {
      locationValid = false;
      return;
    }
    memcpy(url_, value, length + 1);
  };
  const WeReadHttpClient::Result result =
      WeReadHttpClient::request(bookSession_, referer_.c_str(), options, onData, onHeader, responseStatus_);
  if (file.file.isOpen()) finishFile(&file);

  if (file.failure == FileSink::Failure::Cancelled) {
    if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
    return Error::Cancelled;
  }
  if (file.failure == FileSink::Failure::SdCard) {
    if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
    return Error::SdCard;
  }

  if (trackProgress && responseStatus_ == 200) {
    imageBytes_ += file.size;
    if (file.size > 0) ++imageFilesCreated_;
  }

  const auto recoverableFailure = [this, &state, &attempts, &partPath, trackProgress](const bool resetSession) {
    if (resetSession) bookSession_.reset();
    if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
    ++attempts;
    if (imageAttemptPending(attempts)) return;
    state = WeReadStore::ImageWorkState::Skipped;
    if (trackProgress) {
      ++imageSkipped_;
      ++progressCompleted_;
    }
  };

  if (result != WeReadHttpClient::Result::Ok || file.failure == FileSink::Failure::TooLarge) {
    recoverableFailure(true);
    return Error::Ok;
  }

  const bool redirected = responseStatus_ == 301 || responseStatus_ == 302 || responseStatus_ == 303 ||
                          responseStatus_ == 307 || responseStatus_ == 308;
  if (redirected) {
    if (!hasLocation || !locationValid || !imageRedirectAllowed(redirects) ||
        !resolveRedirectUrl(referer_.c_str(), url_, reinterpret_cast<char*>(ioBuffer_), sizeof(url_))) {
      recoverableFailure(false);
      return Error::Ok;
    }
    char destinationHost[128];
    if (!WeReadHttpClient::extractHttpsHost(reinterpret_cast<const char*>(ioBuffer_), destinationHost,
                                            sizeof(destinationHost))) {
      return Error::Protocol;
    }
    LOG_INF("WR", "image redirect: %s -> %s", imageHost_, destinationHost);
    memcpy(image.url, ioBuffer_, strlen(reinterpret_cast<const char*>(ioBuffer_)) + 1);
    attempts = 0;
    ++redirects;
    if (trackProgress) ++imageRedirects_;
    return Error::Ok;
  }

  if (responseStatus_ != 200) {
    recoverableFailure(false);
    return Error::Ok;
  }

  static constexpr uint8_t kPng[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  const bool validMagic =
      type == WeReadProtocol::ImageType::Png
          ? file.prefixSize >= sizeof(kPng) && memcmp(file.prefix, kPng, sizeof(kPng)) == 0
          : file.prefixSize >= 3 && file.prefix[0] == 0xFF && file.prefix[1] == 0xD8 && file.prefix[2] == 0xFF;
  if (!validMagic || file.size == 0) {
    recoverableFailure(false);
    return Error::Ok;
  }

  if (!WeReadStore::atomicReplace(partPath, destination)) return Error::SdCard;
  state = WeReadStore::ImageWorkState::Complete;
  if (trackProgress) {
    ++imageDownloaded_;
    ++progressCompleted_;
  }
  return Error::Ok;
}

Operation::Event Operation::fetchCover() {
  if (coverType_ == WeReadProtocol::ImageType::None || !url_[0]) {
    phase_ = Phase::Complete;
    return Event::Complete;
  }

  WeReadStore::ImageRecord image;
  const char* href = coverSourceName(coverType_);
  memcpy(image.href, href, strlen(href) + 1);
  memcpy(image.url, url_, strlen(url_) + 1);
  if (!WeReadHttpClient::extractHttpsHost(image.url, imageHost_, sizeof(imageHost_))) {
    phase_ = Phase::Complete;
    return Event::Complete;
  }

  const Error error = requestImage(image, coverState_, coverAttempts_, coverRedirects_, false);
  if (image.url[0]) memcpy(url_, image.url, strlen(image.url) + 1);
  if (error == Error::Cancelled) return cancelNow();
  if (error != Error::Ok) return fail(error);

  switch (coverState_) {
    case WeReadStore::ImageWorkState::Pending:
      return Event::None;
    case WeReadStore::ImageWorkState::Skipped:
      phase_ = Phase::Complete;
      logMemory("cover skipped");
      return Event::Complete;
    case WeReadStore::ImageWorkState::Complete:
      phase_ = Phase::ConvertCover;
      return Event::None;
  }
  return fail(Error::Protocol);
}

Operation::Event Operation::convertCover() {
  bookSession_.reset();
  logMemory("cover convert start");
  const std::string source = bookDir_ + "/" + coverSourceName(coverType_);
  const std::string final = WeReadStore::coverPath(bookDir_);
  const std::string part = final + ".part";
  if (Storage.exists(part.c_str())) Storage.remove(part.c_str());

  HalFile input;
  HalFile output;
  if (!Storage.openFileForRead("WR", source, input) || !Storage.openFileForWrite("WR", part, output)) {
    if (input.isOpen()) input.close();
    if (output.isOpen()) output.close();
    if (Storage.exists(part.c_str())) Storage.remove(part.c_str());
    if (Storage.exists(source.c_str())) Storage.remove(source.c_str());
    phase_ = Phase::Complete;
    return Event::Complete;
  }
  const bool converted = coverType_ == WeReadProtocol::ImageType::Png
                             ? PngToBmpConverter::pngFileToBmpStreamWithSize(input, output, 96, 140)
                             : JpegToBmpConverter::jpegFileToBmpStreamWithSize(input, output, 96, 140);
  input.close();
  output.close();
  if (!converted) {
    Storage.remove(part.c_str());
    Storage.remove(source.c_str());
    phase_ = Phase::Complete;
    logMemory("cover convert skipped");
    return Event::Complete;
  }
  if (!WeReadStore::atomicReplace(part, final)) return fail(Error::SdCard);
  const std::string alternate =
      bookDir_ + "/" +
      coverSourceName(coverType_ == WeReadProtocol::ImageType::Png ? WeReadProtocol::ImageType::Jpeg
                                                                   : WeReadProtocol::ImageType::Png);
  if (Storage.exists(alternate.c_str()) && !Storage.remove(alternate.c_str())) return fail(Error::SdCard);
  phase_ = Phase::Complete;
  logMemory("cover convert complete");
  return Event::Complete;
}

Operation::Event Operation::downloadNextImage() {
  WeReadStore::ImageWorkRecord selected;
  uint32_t selectedIndex = 0;
  bool found = false;
  if (!tocFile_.isOpen()) return fail(Error::Integrity);

  if (!imageHost_[0]) {
    for (uint32_t i = 0; i < imageWorkCount_; ++i) {
      WeReadStore::ImageWorkRecord record;
      if (!WeReadStore::readImageWorkRecord(tocFile_, i, record) || !validImageWorkRecord(record)) {
        return fail(Error::Integrity);
      }
      if (record.state != WeReadStore::ImageWorkState::Pending) continue;
      if (!WeReadHttpClient::extractHttpsHost(record.image.url, imageHost_, sizeof(imageHost_))) {
        return fail(Error::Integrity);
      }
      imageWorkCursor_ = 0;
      LOG_INF("WR", "image host batch: host=%s", imageHost_);
      break;
    }
  }

  while (imageHost_[0] && imageWorkCursor_ < imageWorkCount_) {
    const uint32_t current = imageWorkCursor_++;
    WeReadStore::ImageWorkRecord record;
    if (!WeReadStore::readImageWorkRecord(tocFile_, current, record) || !validImageWorkRecord(record)) {
      return fail(Error::Integrity);
    }
    if (record.state != WeReadStore::ImageWorkState::Pending) continue;
    char host[128];
    if (!WeReadHttpClient::extractHttpsHost(record.image.url, host, sizeof(host))) return fail(Error::Integrity);
    if (strcmp(host, imageHost_) != 0) continue;
    selected = record;
    selectedIndex = current;
    found = true;
    break;
  }

  if (found) {
    const Error error = requestImage(selected.image, selected.state, selected.attempts, selected.redirects, true);
    if (error == Error::Cancelled) return cancelNow();
    if (error != Error::Ok) return fail(error);
    if (!WeReadStore::updateImageWorkRecord(tocFile_, imageWorkCount_, selectedIndex, selected)) {
      return fail(Error::SdCard);
    }
    if (selected.state == WeReadStore::ImageWorkState::Skipped) {
      LOG_INF("WR", "image skipped: index=%u", static_cast<unsigned>(selectedIndex));
    } else if (selected.state == WeReadStore::ImageWorkState::Pending && selected.attempts > 0) {
      LOG_INF("WR", "image retry queued: index=%u retry=%u/1", static_cast<unsigned>(selectedIndex),
              static_cast<unsigned>(selected.attempts));
    }
    logMemory("image processed");
    return Event::None;
  }

  if (!imageHost_[0]) {
    LOG_INF("WR",
            "image phase complete: ms=%lu total=%u downloaded=%u cached=%u skipped=%u bytes=%llu redirects=%u "
            "files=%u tlsNew=%u tlsReused=%u",
            millis() - imagePhaseStartedAt_, static_cast<unsigned>(progressTotal_),
            static_cast<unsigned>(imageDownloaded_), static_cast<unsigned>(imageCached_),
            static_cast<unsigned>(imageSkipped_), static_cast<unsigned long long>(imageBytes_),
            static_cast<unsigned>(imageRedirects_), static_cast<unsigned>(imageFilesCreated_),
            static_cast<unsigned>(bookSession_.newConnections()), static_cast<unsigned>(bookSession_.reusedRequests()));
    bookSession_.reset();
    tocFile_.close();
    progressStage_ = ProgressStage::Packaging;
    progressCompleted_ = 0;
    progressTotal_ = 0;
    phase_ = Phase::PackageBook;
    return Event::None;
  }

  imageHost_[0] = '\0';
  imageWorkCursor_ = 0;
  return Event::None;
}

Operation::Event Operation::inspectPrimary() {
  const std::string raw0 = bookDir_ + "/shard0.part";
  if (smallFileContains(raw0, "-2012")) return reauthenticateChapter();
  switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileIsEmptyObject(raw0))) {
    case WeReadProtocol::ChapterResponse::Content:
      break;
    case WeReadProtocol::ChapterResponse::AuthenticationRequired:
      return reauthenticateChapter();
    case WeReadProtocol::ChapterResponse::Retryable:
      return retryChapterResponse();
    case WeReadProtocol::ChapterResponse::Error:
      return fail(Error::Protocol);
  }
  uint8_t prefix[4] = {};
  if (!readPrefix(raw0, prefix, sizeof(prefix))) return fail(Error::Integrity);
  if (prefix[0] == 'P' && prefix[1] == 'K' && prefix[2] == 3 && prefix[3] == 4) {
    return finishWholeBook(raw0);
  }
  if (smallFileContains(raw0, "\"bookId\"")) {
    phase_ = Phase::FetchText0;
    return Event::None;
  }
  if (!validateShard(raw0)) return fail(Error::Integrity);
  phase_ = Phase::FetchEpub1;
  return Event::None;
}

Operation::Event Operation::decodeChapter(const bool plainText) {
  const std::string raw0 = bookDir_ + "/shard0.part";
  const std::string raw1 = bookDir_ + "/shard1.part";
  const std::string raw3 = bookDir_ + "/shard3.part";
  const std::string shards[] = {raw0, raw1, raw3};
  std::string decoded;
  const size_t count = plainText ? 2 : 3;
  if (!combineAndDecode(shards, count, bookDir_, decoded)) return fail(Error::Integrity);
  const auto cleanup = [&]() {
    Storage.remove(decoded.c_str());
    Storage.remove(raw0.c_str());
    Storage.remove(raw1.c_str());
    if (!plainText) Storage.remove(raw3.c_str());
  };
  if (!plainText) {
    uint8_t prefix[4] = {};
    if (readPrefix(decoded, prefix, sizeof(prefix)) && prefix[0] == 'P' && prefix[1] == 'K' && prefix[2] == 3 &&
        prefix[3] == 4) {
      return finishWholeBook(decoded);
    }
  }
  uint64_t decodedBytes = 0;
  {
    HalFile decodedFile;
    if (Storage.openFileForRead("WR", decoded, decodedFile)) decodedBytes = decodedFile.fileSize64();
  }
  LOG_DBG("WR", "chapter decoded: index=%u paid=%u bytes=%llu", static_cast<unsigned>(chapterIndex_),
          static_cast<unsigned>(chapter_.paid), static_cast<unsigned long long>(decodedBytes));
  bool hasXhtmlTag = true;
  if (chapter_.paid && !plainText) {
    if (!containsAllowedXhtmlTag(decoded, ioBuffer_, sizeof(ioBuffer_), hasXhtmlTag)) {
      cleanup();
      return fail(Error::SdCard);
    }
  }
  if (shouldRetryPaidPreview(chapter_.paid != 0, plainText, hasXhtmlTag)) {
    LOG_INF("WR", "paid preview rejected: chapter=%u bytes=%llu", static_cast<unsigned>(chapterIndex_),
            static_cast<unsigned long long>(decodedBytes));
    cleanup();
    return retryChapterResponse();
  }
  const bool ok = sanitizeToXhtml(decoded, WeReadStore::chapterPath(bookDir_, chapterIndex_),
                                  WeReadStore::imageIndexPath(bookDir_, chapterIndex_), chapterIndex_, chapter_.title,
                                  plainText, reinterpret_cast<uint8_t*>(url_), sizeof(url_),
                                  reinterpret_cast<char*>(ioBuffer_), sizeof(ioBuffer_));
  cleanup();
  if (!ok) return fail(Error::SdCard);
  phase_ = Phase::AdvanceChapter;
  return Event::None;
}

Operation::Event Operation::step() {
  if (!active()) return Event::None;
  if (cancelRequested_) return cancelNow();
  if ((requestAttempt_ > 0 || chapterResponseAttempts_ > 0 || phase_ == Phase::VerifyProgress) && nextActionAt_ != 0 &&
      static_cast<long>(millis() - nextActionAt_) < 0) {
    return Event::None;
  }

  switch (phase_) {
    case Phase::Idle:
    case Phase::Complete:
    case Phase::Cancelled:
    case Phase::Failed:
    case Phase::AwaitChapterRange:
      return Event::None;

    case Phase::LoginUid: {
      const Error error = fetchLoginUid();
      if (error != Error::Ok) return handleRequestError(error, Phase::LoginUid);
      requestAttempt_ = 0;
      loginStartedAt_ = millis();
      nextActionAt_ = loginStartedAt_ + kLoginPollMs;
      phase_ = Phase::LoginPollWait;
      return Event::QrReady;
    }

    case Phase::LoginPollWait:
      if (millis() - loginStartedAt_ >= kLoginTimeoutMs) return fail(Error::LoginFailed);
      if (static_cast<long>(millis() - nextActionAt_) < 0) return Event::None;
      phase_ = Phase::LoginPoll;
      return Event::None;

    case Phase::LoginPoll: {
      const Error error = pollLogin();
      if (error != Error::Ok) {
        if (error != Error::Network) return fail(error);
        nextActionAt_ = millis() + kLoginPollMs;
        phase_ = Phase::LoginPollWait;
        return Event::None;
      }
      if (!loginConfirmed_) {
        nextActionAt_ = millis() + kLoginPollMs;
        phase_ = Phase::LoginPollWait;
        return Event::None;
      }
      requestAttempt_ = 0;
      phase_ = resumePhase_;
      return Event::Authenticated;
    }

    case Phase::Renew: {
      const Error error = renewSession();
      if (error == Error::Ok) {
        requestAttempt_ = 0;
        nextActionAt_ = 0;
        phase_ = resumePhase_;
        return Event::None;
      }
      if (error == Error::Network) return handleRequestError(error, Phase::Renew);
      if (error == Error::SdCard) return fail(error);
      WeReadStore::clearSession();
      if (kind_ == Kind::ProgressSync) return fail(Error::SessionExpired);
      if (loginRecoveryAttempted_) return fail(Error::SessionExpired);
      loginRecoveryAttempted_ = true;
      startLogin(resumePhase_);
      return Event::None;
    }

    case Phase::SyncShelf: {
      const Error error = syncShelfOnce();
      if (error == Error::SessionExpired) {
        requestAuthentication(Phase::SyncShelf);
        return phase_ == Phase::Failed ? fail(error_) : Event::None;
      }
      if (error != Error::Ok) return handleRequestError(error, Phase::SyncShelf);
      requestSucceeded();
      phase_ = Phase::Complete;
      logJobComplete();
      return Event::Complete;
    }

    case Phase::PrepareDetail: {
      bookDir_ = WeReadStore::bookDirectory(book_.bookId);
      if (!WeReadStore::ensureRoot() || !Storage.ensureDirectoryExists(bookDir_.c_str())) return fail(Error::SdCard);
      WeReadStore::BookDetailHeader cached;
      HalFile detail;
      if (WeReadStore::openBookDetail(bookDir_, cached, detail)) {
        std::string coverSource;
        const bool hasCoverSource = findCoverSource(bookDir_, coverSource) != WeReadProtocol::ImageType::None;
        if (!detailCoverPending(Storage.exists(WeReadStore::coverPath(bookDir_).c_str()), hasCoverSource,
                                cached.coverUrl[0])) {
          phase_ = Phase::Complete;
          logJobComplete();
          return Event::Complete;
        }
        coverType_ = WeReadProtocol::normalizeImageUrl(cached.coverUrl, url_, sizeof(url_));
        if (coverType_ == WeReadProtocol::ImageType::None) {
          phase_ = Phase::Complete;
          return Event::Complete;
        }
        requestAttempt_ = 0;
        chapterResponseAttempts_ = 0;
        phase_ = Phase::FetchCover;
        return detailCompletionEvent(true);
      }
      phase_ = Phase::FetchDetail;
      return Event::None;
    }

    case Phase::FetchDetail: {
      const Error error = fetchDetailOnce();
      if (error == Error::SessionExpired) {
        requestAuthentication(Phase::FetchDetail);
        return phase_ == Phase::Failed ? fail(error_) : Event::None;
      }
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchDetail);
      requestSucceeded();
      if (!url_[0] || coverType_ == WeReadProtocol::ImageType::None) {
        phase_ = Phase::Complete;
        logJobComplete();
        return detailCompletionEvent(false);
      }
      phase_ = Phase::FetchCover;
      return detailCompletionEvent(true);
    }

    case Phase::FetchCover: {
      const Event event = fetchCover();
      if (event == Event::Complete) logJobComplete();
      return event;
    }

    case Phase::ConvertCover: {
      const Event event = convertCover();
      if (event == Event::Complete) logJobComplete();
      return event;
    }

    case Phase::PrepareDownload: {
      if (!preparePaths()) return fail(Error::SdCard);
      phase_ = Phase::FetchToc;
      return Event::None;
    }

    case Phase::FetchToc: {
      const Error error = fetchTocOnce();
      if (error == Error::SessionExpired) {
        requestAuthentication(Phase::FetchToc);
        return phase_ == Phase::Failed ? fail(error_) : Event::None;
      }
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchToc);
      requestSucceeded();
      phase_ = (kind_ == Kind::ProgressSync || (kind_ == Kind::Download && strncmp(book_.bookId, "MP_WXS_", 7) != 0))
                   ? Phase::FetchProgress
                   : Phase::OpenToc;
      return Event::None;
    }

    case Phase::PrepareProgressSync: {
      bookDir_ = WeReadStore::bookDirectory(book_.bookId);
      tocPath_ = WeReadStore::tocPath(book_.bookId);
      if (!WeReadStore::ensureRoot() || !Storage.ensureDirectoryExists(bookDir_.c_str())) return fail(Error::SdCard);
      HalFile toc;
      uint32_t count = 0;
      phase_ = WeReadStore::openToc(tocPath_, toc, count) && count > 0 ? Phase::FetchProgress : Phase::FetchToc;
      return Event::None;
    }

    case Phase::FetchProgress: {
      const Error error = fetchProgressOnce(kind_ == Kind::ProgressSync);
      if (kind_ == Kind::Download) {
        if (error == Error::Ok) {
          initialProgressFraction_ = normalizedRemoteProgress();
          initialProgressValid_ = true;
          requestSucceeded();
          LOG_INF("WR", "prefetched initial progress: %.4f", initialProgressFraction_);
        } else {
          LOG_INF("WR", "initial progress prefetch skipped: error=%u", static_cast<unsigned>(error));
        }
        phase_ = Phase::OpenToc;
        return Event::None;
      }
      if (error == Error::SessionExpired) {
        requestAuthentication(Phase::FetchProgress);
        return phase_ == Phase::Failed ? fail(error_) : Event::None;
      }
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchProgress);
      requestSucceeded();
      phase_ = Phase::DecideProgress;
      return Event::None;
    }

    case Phase::DecideProgress: {
      const Error error = decideProgress();
      if (error != Error::Ok) return fail(error);
      if (progressSyncResult_.outcome != ProgressSyncOutcome::Pending) {
        bookSession_.reset();
        phase_ = Phase::Complete;
        logJobComplete();
        return Event::Complete;
      }
      phase_ = Phase::FetchProgressReader;
      return Event::None;
    }

    case Phase::FetchProgressReader: {
      const Error error = fetchProgressReaderOnce();
      if (error == Error::SessionExpired) {
        requestAuthentication(Phase::FetchProgressReader);
        return phase_ == Phase::Failed ? fail(error_) : Event::None;
      }
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchProgressReader);
      requestSucceeded();
      phase_ = Phase::SendProgressEnter;
      return Event::None;
    }

    case Phase::SendProgressEnter: {
      const Error error = sendProgressOnce(false);
      if (error == Error::SessionExpired) {
        requestAuthentication(Phase::FetchProgressReader);
        return phase_ == Phase::Failed ? fail(error_) : Event::None;
      }
      if (error != Error::Ok) return handleRequestError(error, Phase::SendProgressEnter);
      requestSucceeded();
      phase_ = Phase::SendProgressReport;
      return Event::None;
    }

    case Phase::SendProgressReport: {
      const Error error = sendProgressOnce(true);
      if (error == Error::SessionExpired) {
        requestAuthentication(Phase::FetchProgressReader);
        return phase_ == Phase::Failed ? fail(error_) : Event::None;
      }
      if (error != Error::Ok) return handleRequestError(error, Phase::SendProgressReport);
      requestSucceeded();
      progressVerifyAttempts_ = 0;
      nextActionAt_ = millis() + kNetworkRetryBaseMs;
      phase_ = Phase::VerifyProgress;
      return Event::None;
    }

    case Phase::VerifyProgress: {
      const Error error = fetchProgressOnce(true);
      if (error == Error::SessionExpired) {
        requestAuthentication(Phase::VerifyProgress);
        return phase_ == Phase::Failed ? fail(error_) : Event::None;
      }
      if (error != Error::Ok) return handleRequestError(error, Phase::VerifyProgress);
      requestSucceeded();
      ++progressVerifyAttempts_;
      progressSyncResult_.remote.percent = normalizedRemoteProgress() * 100.0f;
      const bool samePosition = sameRemotePosition();
      const bool sameAppId = remoteAppIdMatchesLocal();
      const auto& remote = progressSyncResult_.remote;
      const ProgressSyncOutcome outcome = progressVerification(
          samePosition, remote.hasAppId, sameAppId, remote.hasUpdateTime, remote.updateTime, progressUploadStartedAt_);
      LOG_INF("WR", "progress verify: attempt=%u same=%u time=%u sameApp=%u outcome=%u",
              static_cast<unsigned>(progressVerifyAttempts_), static_cast<unsigned>(samePosition),
              static_cast<unsigned>(remote.updateTime), static_cast<unsigned>(sameAppId),
              static_cast<unsigned>(outcome));
      switch (outcome) {
        case ProgressSyncOutcome::LocalUploaded:
        case ProgressSyncOutcome::AlreadySynced:
          progressSyncResult_.outcome = outcome;
          break;
        case ProgressSyncOutcome::SelectionRequired:
          progressSyncResult_.outcome = outcome;
          break;
        case ProgressSyncOutcome::ApplyRemote:
          return fail(Error::Protocol);
        case ProgressSyncOutcome::Pending:
          if (progressVerifyAttempts_ < kMaxRequestAttempts) {
            nextActionAt_ = millis() + kNetworkRetryBaseMs;
            return Event::None;
          }
          return fail(Error::Unavailable);
      }
      bookSession_.reset();
      phase_ = Phase::Complete;
      logJobComplete();
      return Event::Complete;
    }

    case Phase::OpenToc:
      guardBookSession("toc");
      if (tocFile_.isOpen()) tocFile_.close();
      if (!WeReadStore::openToc(tocPath_, tocFile_, chapterCount_) || chapterCount_ == 0) {
        return fail(Error::Protocol);
      }
      firstChapterIndex_ = 0;
      lastChapterIndex_ = chapterCount_ - 1;
      chapterIndex_ = firstChapterIndex_;
      progressStage_ = ProgressStage::Chapters;
      progressCompleted_ = 0;
      progressTotal_ = chapterRangeCount(firstChapterIndex_, lastChapterIndex_, chapterCount_);
      psvts_[0] = '\0';
      logMemory("toc parsed");
      switch (options_.chapterScope) {
        case DownloadOptions::ChapterScope::WholeBook:
          phase_ = Phase::LoadChapter;
          return Event::None;
        case DownloadOptions::ChapterScope::SelectRange:
          bookSession_.reset();
          phase_ = Phase::AwaitChapterRange;
          return Event::ChapterRangeReady;
      }

    case Phase::LoadChapter: {
      if (chapterIndex_ > lastChapterIndex_) {
        if (tocFile_.isOpen()) tocFile_.close();
        phase_ = Phase::PrepareImages;
        return Event::None;
      }
      if (!WeReadStore::readTocRecord(tocFile_, chapterIndex_, chapter_)) return fail(Error::SdCard);
      chapterResponseAttempts_ = 0;
      HalFile imageIndex;
      uint32_t imageCount = 0;
      if (Storage.exists(WeReadStore::chapterPath(bookDir_, chapterIndex_).c_str()) &&
          WeReadStore::openImageIndex(WeReadStore::imageIndexPath(bookDir_, chapterIndex_), imageIndex, imageCount)) {
        phase_ = Phase::AdvanceChapter;
        return Event::None;
      }
      if (!makeReaderReferer(book_.bookId, chapter_.chapterUid, referer_)) return fail(Error::Protocol);
      if (!psvts_[0]) {
        if (!TimeUtils::isClockValid()) {
          if (!WeReadHttpClient::networkReady()) return fail(Error::Network);
          // SNTP and TLS both hold network buffers. A cold-clock download
          // reconnects after sync rather than keeping both alive.
          bookSession_.reset();
          if (!halClock.requestSync()) return fail(Error::Clock);
          nextActionAt_ = millis() + kClockSyncTimeoutMs;
          phase_ = Phase::SyncClock;
          logMemory("clock sync start");
          return Event::None;
        }
      }
      phase_ = psvts_[0] ? Phase::FetchPrimary : Phase::FetchReader;
      return Event::None;
    }

    case Phase::SyncClock:
      if (TimeUtils::isClockValid()) {
        logMemory("clock sync complete");
        phase_ = Phase::LoadChapter;
        return Event::None;
      }
      if (halClock.syncState() != ClockSyncState::Failed && static_cast<long>(millis() - nextActionAt_) < 0) {
        return Event::None;
      }
      logMemory(halClock.syncState() == ClockSyncState::Failed ? "clock sync failed" : "clock sync timeout");
      return fail(Error::Clock);

    case Phase::FetchReader: {
      if (!waitForShardPace()) return Event::None;
      const Error error = fetchReaderOnce();
      if (error == Error::SessionExpired) return reauthenticateChapter();
      if (error == Error::Unavailable) return retryChapterResponse();
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchReader);
      requestSucceeded();
      LOG_DBG("WR", "reader psvts: chapter=%u refreshed=%u", static_cast<unsigned>(chapterIndex_),
              static_cast<unsigned>(chapterResponseAttempts_ > 0));
      phase_ = Phase::FetchPrimary;
      return Event::None;
    }

    case Phase::FetchPrimary: {
      if (!waitForShardPace()) return Event::None;
      const Error error = fetchShardOnce("/web/book/chapter/e_0", bookDir_ + "/shard0.part");
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchPrimary);
      requestAttempt_ = 0;
      guardBookSession("primary");
      return inspectPrimary();
    }

    case Phase::FetchText0: {
      if (!waitForShardPace()) return Event::None;
      const std::string raw0 = bookDir_ + "/shard0.part";
      const Error error = fetchShardOnce("/web/book/chapter/t_0", raw0);
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchText0);
      requestAttempt_ = 0;
      switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileIsEmptyObject(raw0))) {
        case WeReadProtocol::ChapterResponse::Content:
          phase_ = Phase::FetchText1;
          return Event::None;
        case WeReadProtocol::ChapterResponse::AuthenticationRequired:
          return reauthenticateChapter();
        case WeReadProtocol::ChapterResponse::Retryable:
          return retryChapterResponse();
        case WeReadProtocol::ChapterResponse::Error:
          return fail(Error::Protocol);
      }
    }

    case Phase::FetchText1: {
      if (!waitForShardPace()) return Event::None;
      const std::string raw1 = bookDir_ + "/shard1.part";
      const Error error = fetchShardOnce("/web/book/chapter/t_1", raw1);
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchText1);
      requestSucceeded();
      guardBookSession("text validate");
      switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileIsEmptyObject(raw1))) {
        case WeReadProtocol::ChapterResponse::Content:
          if (!validateShard(bookDir_ + "/shard0.part") || !validateShard(raw1)) return fail(Error::Integrity);
          phase_ = Phase::DecodeText;
          return Event::None;
        case WeReadProtocol::ChapterResponse::AuthenticationRequired:
          return reauthenticateChapter();
        case WeReadProtocol::ChapterResponse::Retryable:
          return retryChapterResponse();
        case WeReadProtocol::ChapterResponse::Error:
          return fail(Error::Protocol);
      }
    }

    case Phase::FetchEpub1: {
      if (!waitForShardPace()) return Event::None;
      const std::string raw1 = bookDir_ + "/shard1.part";
      const Error error = fetchShardOnce("/web/book/chapter/e_1", raw1);
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchEpub1);
      requestAttempt_ = 0;
      switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileIsEmptyObject(raw1))) {
        case WeReadProtocol::ChapterResponse::Content:
          phase_ = Phase::FetchEpub3;
          return Event::None;
        case WeReadProtocol::ChapterResponse::AuthenticationRequired:
          return reauthenticateChapter();
        case WeReadProtocol::ChapterResponse::Retryable:
          return retryChapterResponse();
        case WeReadProtocol::ChapterResponse::Error:
          return fail(Error::Protocol);
      }
    }

    case Phase::FetchEpub3: {
      if (!waitForShardPace()) return Event::None;
      const std::string raw1 = bookDir_ + "/shard1.part";
      const std::string raw3 = bookDir_ + "/shard3.part";
      const Error error = fetchShardOnce("/web/book/chapter/e_3", raw3);
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchEpub3);
      requestSucceeded();
      guardBookSession("epub validate");
      switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileIsEmptyObject(raw3))) {
        case WeReadProtocol::ChapterResponse::Content:
          if (!validateShard(bookDir_ + "/shard0.part") || !validateShard(raw1) || !validateShard(raw3)) {
            return fail(Error::Integrity);
          }
          phase_ = Phase::DecodeEpub;
          return Event::None;
        case WeReadProtocol::ChapterResponse::AuthenticationRequired:
          return reauthenticateChapter();
        case WeReadProtocol::ChapterResponse::Retryable:
          return retryChapterResponse();
        case WeReadProtocol::ChapterResponse::Error:
          return fail(Error::Protocol);
      }
    }

    case Phase::DecodeText:
      guardBookSession("text decode");
      logMemory("chapter decode");
      return decodeChapter(true);

    case Phase::DecodeEpub:
      guardBookSession("epub decode");
      logMemory("chapter decode");
      return decodeChapter(false);

    case Phase::AdvanceChapter:
      guardBookSession("progress");
      ++chapterIndex_;
      progressCompleted_ = chapterIndex_ - firstChapterIndex_;
      phase_ = Phase::LoadChapter;
      return Event::ChapterComplete;

    case Phase::PrepareImages: {
      if (options_.imagePolicy == WeReadStore::ImagePolicy::Exclude) {
        bookSession_.reset();
        progressStage_ = ProgressStage::Packaging;
        progressCompleted_ = 0;
        progressTotal_ = 0;
        phase_ = Phase::PackageBook;
        LOG_INF("WR", "image phase excluded");
        return Event::None;
      }
      const Error error = prepareImageWork();
      if (error != Error::Ok) return fail(error);
      if (imageWorkCount_ == 0) {
        LOG_INF("WR",
                "image phase complete: ms=%lu total=0 downloaded=0 cached=0 skipped=0 bytes=0 redirects=0 files=0 "
                "tlsNew=0 tlsReused=0",
                millis() - imagePhaseStartedAt_);
        tocFile_.close();
        progressStage_ = ProgressStage::Packaging;
        progressCompleted_ = 0;
        progressTotal_ = 0;
        phase_ = Phase::PackageBook;
      } else {
        phase_ = Phase::DownloadImages;
      }
      return Event::None;
    }

    case Phase::DownloadImages:
      return downloadNextImage();

    case Phase::PackageBook: {
      bookSession_.reset();
      logMemory("package start");
      const unsigned long packageStartedAt = millis();
      const Error error = packageBook(book_, bookDir_, tocPath_, chapterCount_, firstChapterIndex_, lastChapterIndex_,
                                      options_.imagePolicy, WeReadStore::imageWorkPath(bookDir_), ioBuffer_,
                                      sizeof(ioBuffer_), finalPartPath_);
      logMemory("package end");
      if (error != Error::Ok) return fail(error);
      if (!WeReadStore::saveSession(session_)) return fail(Error::SdCard);
      WeReadStore::BookOptions previousOptions;
      const bool hadPreviousOptions = WeReadStore::loadBookOptions(bookDir_, previousOptions);
      WeReadStore::BookOptions savedOptions;
      savedOptions.imagePolicy = options_.imagePolicy;
      if (!WeReadStore::saveBookOptions(bookDir_, savedOptions)) return fail(Error::SdCard);
      if (!WeReadStore::atomicReplace(finalPartPath_, outputPath_)) {
        if (hadPreviousOptions) {
          WeReadStore::saveBookOptions(bookDir_, previousOptions);
        } else {
          const std::string path = WeReadStore::optionsPath(bookDir_);
          if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
        }
        return fail(Error::SdCard);
      }
      if (!wholeChapterRange(firstChapterIndex_, lastChapterIndex_, chapterCount_) && !clearBookCache(outputPath_)) {
        return fail(Error::SdCard);
      }
      HalFile packaged;
      const uint64_t packageBytes = Storage.openFileForRead("WR", outputPath_, packaged) ? packaged.fileSize64() : 0;
      LOG_INF("WR", "package complete: ms=%lu bytes=%llu images=%s", millis() - packageStartedAt,
              static_cast<unsigned long long>(packageBytes),
              options_.imagePolicy == WeReadStore::ImagePolicy::Embed ? "embed" : "exclude");
      cleanupTransient(bookDir_, "");
      persistInitialProgress();
      phase_ = Phase::Complete;
      logJobComplete();
      return Event::Complete;
    }
  }
  return fail(Error::Protocol);
}

}  // namespace WeReadClient

#endif
