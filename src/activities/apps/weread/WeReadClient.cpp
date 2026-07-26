#include "WeReadClient.h"

#if defined(ENABLE_CHINESE_VERSION) && !defined(__EMSCRIPTEN__)

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <Memory.h>
#include <StreamingJsonParser.h>
#include <esp_sntp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "WeReadProtocol.h"
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
constexpr uint8_t kMaxRequestAttempts = 3;
constexpr size_t kTransferBufferSize = 1024;
// Local decode/package work uses at most one 1 KB transfer buffer at a time.
// Keep wider headroom for SD internals and the event-driven progress render.
constexpr size_t kBookSessionMinFreeHeap = 20 * 1024;
constexpr size_t kBookSessionMinLargestBlock = 8 * 1024;

void logMemory(const char* phase) {
  LOG_INF("WR", "%s: free=%u largest=%u stack=%u", phase, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void startClockSync() {
  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
#ifdef ENABLE_CHINESE_VERSION
  esp_sntp_setservername(0, "ntp.aliyun.com");
  esp_sntp_setservername(1, "ntp.tencent.com");
  esp_sntp_setservername(2, "cn.pool.ntp.org");
#else
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, nullptr);
  esp_sntp_setservername(2, nullptr);
#endif
  esp_sntp_init();
}

void stopClockSync() {
  if (esp_sntp_enabled()) esp_sntp_stop();
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

void absorbSetCookie(WeReadStore::Session* session, const char* headerName, const char* headerValue) {
  if (!session || !equalsIgnoreCase(headerName, "set-cookie") || !headerValue) return;
  const char* equals = strchr(headerValue, '=');
  if (!equals) return;
  const char* end = strchr(equals + 1, ';');
  if (!end) end = headerValue + strlen(headerValue);
  const size_t nameLen = static_cast<size_t>(equals - headerValue);
  if (nameLen == 0 || nameLen >= 16) return;
  char name[16];
  memcpy(name, headerValue, nameLen);
  name[nameLen] = '\0';
  session->setCookie(name, equals + 1, static_cast<size_t>(end - equals - 1));
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
  cookie[0] = '\0';
  if (session && session->valid()) {
    if (!session->cookieHeader(cookie, cookieSize)) return Error::Protocol;
    headers[headerCount++] = {"Cookie", cookie};
  }

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
  const auto onHeader = [session](const char* name, const char* value) { absorbSetCookie(session, name, value); };
  const auto result = reusableSession
                          ? WeReadHttpClient::request(*reusableSession, url, options, onData, onHeader, status)
                          : WeReadHttpClient::request(url, options, onData, onHeader, status);
  if (result == WeReadHttpClient::Result::Ok) return sink.finish(sink.ctx) ? Error::Ok : Error::SdCard;
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
  bool rootClosed = false;
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
          nullptr, simpleObjectStart, simpleObjectEnd, simpleArrayStart, simpleArrayEnd};
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
  ctx.rootClosed = false;
  ctx.depth = 0;
  ctx.parser->reset();
  return true;
}

bool feedSimple(void* raw, const uint8_t* data, const size_t len) {
  auto& ctx = *static_cast<SimpleJsonContext*>(raw);
  ctx.parser->feed(reinterpret_cast<const char*>(data), len);
  return !ctx.parser->hasError();
}

enum class ShelfField : uint8_t { None, Books, BookId, Title, Author, ErrorCode };

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
          nullptr, shelfObjectStart, shelfObjectEnd, shelfArrayStart, shelfArrayEnd};
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

enum class TocField : uint8_t { None, Chapters, ChapterUid, Title, ChapterIdx, Paid, ErrorCode };

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
  return {ctx, tocKey, tocValue, tocValue, tocBool, nullptr, tocObjectStart, tocObjectEnd, tocArrayStart, tocArrayEnd};
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

struct FileSink {
  HalFile file;
  const std::string* path = nullptr;
  size_t size = 0;
};

bool resetFile(void* raw) {
  auto& sink = *static_cast<FileSink*>(raw);
  if (!sink.path) return false;
  if (sink.file.isOpen()) sink.file.close();
  if (Storage.exists(sink.path->c_str())) Storage.remove(sink.path->c_str());
  sink.size = 0;
  return Storage.openFileForWrite("WR", *sink.path, sink.file);
}

bool writeFile(void* raw, const uint8_t* data, const size_t len) {
  auto& sink = *static_cast<FileSink*>(raw);
  if (sink.file.write(data, len) != len) return false;
  sink.size += len;
  return true;
}

bool finishFile(void* raw) {
  auto& sink = *static_cast<FileSink*>(raw);
  sink.file.flush();
  sink.file.close();
  return true;
}

bool isSafeProtocolToken(const char* value) {
  if (!value || !value[0]) return false;
  for (const auto* p = reinterpret_cast<const uint8_t*>(value); *p; ++p) {
    if (!std::isalnum(*p) && *p != '-' && *p != '_') return false;
  }
  return true;
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

bool refreshPsvts(char* out, const size_t outSize) {
  // The web reader initializes psvts as e(current Unix second); content requests
  // use the same value and move ct forward when both timestamps collide.
  uint32_t timestamp = TimeUtils::getCurrentValidTimestamp();
  if (!out || outSize == 0 || timestamp == 0) return false;
  char timestampText[16];
  char encoded[128];
  snprintf(timestampText, sizeof(timestampText), "%u", static_cast<unsigned>(timestamp));
  if (!WeReadProtocol::encodeId(timestampText, md5Hex, encoded, sizeof(encoded))) return false;
  if (out[0] && strcmp(out, encoded) == 0) {
    snprintf(timestampText, sizeof(timestampText), "%u", static_cast<unsigned>(timestamp + 1));
    if (!WeReadProtocol::encodeId(timestampText, md5Hex, encoded, sizeof(encoded))) return false;
  }
  const size_t len = strlen(encoded);
  if (len >= outSize) return false;
  memcpy(out, encoded, len + 1);
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

bool readPrefix(const std::string& path, uint8_t* out, const size_t len) {
  HalFile file;
  return Storage.openFileForRead("WR", path, file) && file.read(out, len) == static_cast<int>(len);
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

bool allowedTag(const char* name) {
  static constexpr const char* kAllowed[] = {"p",      "div", "section", "article",    "h1", "h2", "h3",
                                             "h4",     "h5",  "h6",      "blockquote", "ul", "ol", "li",
                                             "strong", "b",   "em",      "i",          "br", "hr", "span"};
  return std::any_of(std::begin(kAllowed), std::end(kAllowed),
                     [name](const char* allowed) { return strcmp(name, allowed) == 0; });
}

struct XhtmlSanitizer {
  HalFile* output = nullptr;
  bool plainText = false;
  bool inTag = false;
  bool inEntity = false;
  bool skip = false;
  bool skipHead = false;
  char tag[96] = {};
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
  if (sanitizer.skip || sanitizer.skipHead || strcmp(name, "html") == 0 || strcmp(name, "body") == 0 ||
      !allowedTag(name)) {
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

bool sanitizeToXhtml(const std::string& inputPath, const std::string& outputPath, const char* title,
                     const bool plainText) {
  HalFile input;
  if (!Storage.openFileForRead("WR", inputPath, input)) return false;
  const std::string partPath = outputPath + ".part";
  if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
  HalFile output;
  if (!Storage.openFileForWrite("WR", partPath, output)) return false;
  if (!writeLiteral(output,
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                    "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>") ||
      !writeXmlText(output, title) || !writeLiteral(output, "</title></head><body>")) {
    return false;
  }
  if (plainText && !writeLiteral(output, "<p>")) return false;

  XhtmlSanitizer sanitizer{&output, plainText};
  auto buffer = makeUniqueNoThrow<uint8_t[]>(kTransferBufferSize);
  if (!buffer) {
    LOG_ERR("WR", "OOM: %u-byte XHTML buffer", static_cast<unsigned>(kTransferBufferSize));
    return false;
  }
  while (input.available()) {
    const int got = input.read(buffer.get(), kTransferBufferSize);
    if (got <= 0) return false;
    for (int i = 0; i < got; ++i) {
      const uint8_t value = buffer[i];
      if (!plainText && sanitizer.inTag) {
        if (value == '>') {
          if (!processTag(sanitizer)) return false;
        } else if (sanitizer.tagLen + 1 < sizeof(sanitizer.tag)) {
          sanitizer.tag[sanitizer.tagLen++] = static_cast<char>(value);
        }
        continue;
      }
      if (!plainText && value == '<') {
        if (sanitizer.inEntity && !emitEntity(sanitizer)) return false;
        sanitizer.inTag = true;
        sanitizer.tagLen = 0;
        continue;
      }
      if (!emitSanitizedTextByte(sanitizer, value)) return false;
    }
  }
  if (sanitizer.inEntity && !emitEntity(sanitizer)) return false;
  if (plainText && !writeLiteral(output, "</p>")) return false;
  if (!writeLiteral(output, "</body></html>")) return false;
  output.flush();
  output.close();
  return WeReadStore::atomicReplace(partPath, outputPath);
}

bool writePlaceholder(const std::string& path, const char* title) {
  const std::string part = path + ".part";
  if (Storage.exists(part.c_str())) Storage.remove(part.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("WR", part, file)) return false;
  const bool ok = writeLiteral(file,
                               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                               "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>") &&
                  writeXmlText(file, title) && writeLiteral(file, "</title></head><body><p>") &&
                  writeXmlText(file, tr(STR_WEREAD_CACHE_NOT_AVAILABLE)) && writeLiteral(file, "</p></body></html>");
  file.flush();
  file.close();
  return ok && WeReadStore::atomicReplace(part, path);
}

bool writePackageFiles(const std::string& bookDir, const WeReadStore::ShelfRecord& book, const std::string& tocPath,
                       const uint32_t chapterCount, std::string& navPath, std::string& opfPath) {
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
    return false;
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
    return false;
  }

  for (uint32_t i = 0; i < chapterCount; ++i) {
    WeReadStore::TocRecord record;
    if (!WeReadStore::readTocRecord(toc, i, record)) return false;
    char filename[32];
    char item[192];
    snprintf(filename, sizeof(filename), "ch%06u.xhtml", static_cast<unsigned>(i));
    const int navLen = snprintf(item, sizeof(item), "<li><a href=\"%s\">", filename);
    if (navLen <= 0 || static_cast<size_t>(navLen) >= sizeof(item) || !writeLiteral(nav, item) ||
        !writeXmlText(nav, record.title) || !writeLiteral(nav, "</a></li>")) {
      return false;
    }
    const int opfLen =
        snprintf(item, sizeof(item), "<item id=\"ch%06u\" href=\"%s\" media-type=\"application/xhtml+xml\"/>",
                 static_cast<unsigned>(i), filename);
    if (opfLen <= 0 || static_cast<size_t>(opfLen) >= sizeof(item) || !writeLiteral(opf, item)) return false;
  }
  if (!writeLiteral(nav, "</ol></nav></body></html>") || !writeLiteral(opf, "</manifest><spine>")) return false;
  for (uint32_t i = 0; i < chapterCount; ++i) {
    char item[64];
    snprintf(item, sizeof(item), "<itemref idref=\"ch%06u\"/>", static_cast<unsigned>(i));
    if (!writeLiteral(opf, item)) return false;
  }
  if (!writeLiteral(opf, "</spine></package>")) return false;
  nav.flush();
  opf.flush();
  return true;
}

Error packageBook(const WeReadStore::ShelfRecord& book, const std::string& bookDir, const std::string& tocPath,
                  const uint32_t chapterCount, const std::string& finalPartPath) {
  std::string navPath;
  std::string opfPath;
  if (!writePackageFiles(bookDir, book, tocPath, chapterCount, navPath, opfPath)) return Error::SdCard;

  const std::string centralPath = bookDir + "/central.part";
  WeReadStore::StoreOnlyZipWriter zip;
  if (!zip.begin(finalPartPath, centralPath)) return Error::OutOfMemory;
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
  HalFile toc;
  uint32_t count = 0;
  if (!WeReadStore::openToc(tocPath, toc, count) || count != chapterCount) {
    zip.abort();
    return Error::SdCard;
  }
  for (uint32_t i = 0; i < chapterCount; ++i) {
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
  if (!zip.finish() || !WeReadStore::looksLikeZip(finalPartPath)) return Error::Integrity;
  Storage.remove(navPath.c_str());
  Storage.remove(opfPath.c_str());
  return Error::Ok;
}

void cleanupTransient(const std::string& bookDir, const std::string& finalPartPath) {
  static constexpr const char* kNames[] = {"/shard0.part",  "/shard1.part",  "/shard3.part", "/encoded.part",
                                           "/decoded.part", "/central.part", "/nav.part",    "/content.part"};
  for (const char* name : kNames) {
    const std::string path = bookDir + name;
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  }
  if (!finalPartPath.empty() && Storage.exists(finalPartPath.c_str())) Storage.remove(finalPartPath.c_str());
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
    case Phase::PrepareDownload:
    case Phase::FetchToc:
    case Phase::OpenToc:
    case Phase::LoadChapter:
    case Phase::SyncClock:
    case Phase::FetchPrimary:
    case Phase::FetchText0:
    case Phase::FetchText1:
    case Phase::FetchEpub1:
    case Phase::FetchEpub3:
    case Phase::DecodeText:
    case Phase::DecodeEpub:
    case Phase::WriteUnavailable:
    case Phase::AdvanceChapter:
    case Phase::PackageBook:
      return true;
  }
  return false;
}

void Operation::reset() {
  bookSession_.reset();
  if (phase_ == Phase::SyncClock) stopClockSync();
  if (kind_ == Kind::Download && active() && !bookDir_.empty()) cleanupTransient(bookDir_, finalPartPath_);
  if (tocFile_.isOpen()) tocFile_.close();
  phase_ = Phase::Idle;
  resumePhase_ = Phase::Idle;
  kind_ = Kind::Sync;
  error_ = Error::Ok;
  session_.clear();
  book_ = {};
  chapter_ = {};
  chapterCount_ = 0;
  chapterIndex_ = 0;
  requestAttempt_ = 0;
  cancelRequested_ = false;
  renewalAttempted_ = false;
  loginRecoveryAttempted_ = false;
  loginConfirmed_ = false;
  primaryPsvtsRefreshed_ = false;
  loginStartedAt_ = 0;
  nextActionAt_ = 0;
  lastShardRequestAt_ = 0;
  responseStatus_ = 0;
  previousVid_[0] = '\0';
  loginUid_[0] = '\0';
  psvts_[0] = '\0';
  cookie_[0] = '\0';
  url_[0] = '\0';
  referer_.clear();
  bookDir_.clear();
  tocPath_.clear();
  outputPath_.clear();
  finalPartPath_.clear();
}

bool Operation::begin(const Kind kind, const WeReadStore::ShelfRecord* book) {
  reset();
  kind_ = kind;
  if (kind == Kind::Download) {
    if (!book || !isSafeProtocolToken(book->bookId)) {
      error_ = Error::Protocol;
      phase_ = Phase::Failed;
      return false;
    }
    book_ = *book;
  }
  WeReadStore::loadSession(session_);
  const Phase first = kind == Kind::Sync ? Phase::SyncShelf : Phase::PrepareDownload;
  if (session_.valid()) {
    phase_ = first;
  } else {
    startLogin(first);
  }
  logMemory("job start");
  return true;
}

void Operation::cancel() {
  if (active()) cancelRequested_ = true;
}

void Operation::startLogin(const Phase resume) {
  memcpy(previousVid_, session_.vid, sizeof(previousVid_));
  previousVid_[sizeof(previousVid_) - 1] = '\0';
  session_.clear();
  loginUid_[0] = '\0';
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
  if (phase_ == Phase::SyncClock) stopClockSync();
  error_ = error;
  if (tocFile_.isOpen()) tocFile_.close();
  if (kind_ == Kind::Download && !bookDir_.empty()) {
    const std::string chapterPart = WeReadStore::chapterPath(bookDir_, chapterIndex_) + ".part";
    if (Storage.exists(chapterPart.c_str())) Storage.remove(chapterPart.c_str());
    cleanupTransient(bookDir_, finalPartPath_);
  }
  phase_ = Phase::Failed;
  LOG_ERR("WR", "job failed: phase=%u error=%u", static_cast<unsigned>(failedPhase), static_cast<unsigned>(error));
  logMemory("job failed");
  return Event::Failed;
}

Operation::Event Operation::handleRequestError(const Error error, const Phase retryPhase) {
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

void Operation::requestSucceeded() {
  requestAttempt_ = 0;
  nextActionAt_ = 0;
  renewalAttempted_ = false;
  loginRecoveryAttempted_ = false;
}

void Operation::guardBookSession(const char* phase) {
  if (!bookSession_.reusable()) return;
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t largestBlock = ESP.getMaxAllocHeap();
  LOG_INF("WR", "book TLS guard: phase=%s free=%u largest=%u stack=%u", phase ? phase : "?",
          static_cast<unsigned>(freeHeap), static_cast<unsigned>(largestBlock),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  if (freeHeap >= kBookSessionMinFreeHeap && largestBlock >= kBookSessionMinLargestBlock) return;
  LOG_INF("WR", "book TLS fallback: phase=%s requiredFree=%u requiredLargest=%u", phase ? phase : "?",
          static_cast<unsigned>(kBookSessionMinFreeHeap), static_cast<unsigned>(kBookSessionMinLargestBlock));
  bookSession_.reset();
}

bool Operation::preparePaths() {
  outputPath_ = WeReadStore::finalBookPath(book_);
  finalPartPath_ = outputPath_ + ".part";
  bookDir_ = WeReadStore::bookDirectory(book_.bookId);
  tocPath_ = bookDir_ + "/toc.bin";
  const std::string chaptersDir = bookDir_ + "/chapters";
  return WeReadStore::ensureRoot() && Storage.ensureDirectoryExists(bookDir_.c_str()) &&
         Storage.ensureDirectoryExists(chaptersDir.c_str()) && Storage.ensureDirectoryExists("/WeRead");
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
  logMemory("shelf parsed");
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
  if (!WeReadStore::looksLikeZip(source)) return fail(Error::Integrity);
  if (Storage.exists(finalPartPath_.c_str())) Storage.remove(finalPartPath_.c_str());
  if (!Storage.rename(source.c_str(), finalPartPath_.c_str()) ||
      !WeReadStore::atomicReplace(finalPartPath_, outputPath_)) {
    return fail(Error::SdCard);
  }
  cleanupTransient(bookDir_, "");
  if (!WeReadStore::saveSession(session_)) return fail(Error::SdCard);
  if (tocFile_.isOpen()) tocFile_.close();
  phase_ = Phase::Complete;
  logMemory("job complete");
  return Event::Complete;
}

Operation::Event Operation::inspectPrimary() {
  const std::string raw0 = bookDir_ + "/shard0.part";
  if (smallFileContains(raw0, "-2012")) {
    requestAuthentication(Phase::FetchPrimary);
    return phase_ == Phase::Failed ? fail(error_) : Event::None;
  }
  if ((responseStatus_ != 200 || smallFileContains(raw0, "{}")) && !primaryPsvtsRefreshed_) {
    if (!refreshPsvts(psvts_, sizeof(psvts_))) return fail(Error::Clock);
    primaryPsvtsRefreshed_ = true;
    phase_ = Phase::FetchPrimary;
    return Event::None;
  }
  switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileContains(raw0, "{}"))) {
    case WeReadProtocol::ChapterResponse::Content:
      break;
    case WeReadProtocol::ChapterResponse::Unavailable:
      phase_ = Phase::WriteUnavailable;
      return Event::None;
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
  if (!plainText) {
    uint8_t prefix[4] = {};
    if (readPrefix(decoded, prefix, sizeof(prefix)) && prefix[0] == 'P' && prefix[1] == 'K' && prefix[2] == 3 &&
        prefix[3] == 4) {
      return finishWholeBook(decoded);
    }
  }
  const bool ok =
      sanitizeToXhtml(decoded, WeReadStore::chapterPath(bookDir_, chapterIndex_), chapter_.title, plainText);
  Storage.remove(decoded.c_str());
  Storage.remove(raw0.c_str());
  Storage.remove(raw1.c_str());
  if (!plainText) Storage.remove(raw3.c_str());
  if (!ok) return fail(Error::SdCard);
  phase_ = Phase::AdvanceChapter;
  return Event::None;
}

Operation::Event Operation::step() {
  if (!active()) return Event::None;
  if (cancelRequested_) {
    bookSession_.reset();
    if (phase_ == Phase::SyncClock) stopClockSync();
    if (tocFile_.isOpen()) tocFile_.close();
    if (kind_ == Kind::Download && !bookDir_.empty()) cleanupTransient(bookDir_, finalPartPath_);
    error_ = Error::Cancelled;
    phase_ = Phase::Cancelled;
    logMemory("job cancelled");
    return Event::Cancelled;
  }
  if (requestAttempt_ > 0 && nextActionAt_ != 0 && static_cast<long>(millis() - nextActionAt_) < 0) {
    return Event::None;
  }

  switch (phase_) {
    case Phase::Idle:
    case Phase::Complete:
    case Phase::Cancelled:
    case Phase::Failed:
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
      logMemory("job complete");
      return Event::Complete;
    }

    case Phase::PrepareDownload:
      if (!preparePaths()) return fail(Error::SdCard);
      phase_ = Phase::FetchToc;
      return Event::None;

    case Phase::FetchToc: {
      const Error error = fetchTocOnce();
      if (error == Error::SessionExpired) {
        requestAuthentication(Phase::FetchToc);
        return phase_ == Phase::Failed ? fail(error_) : Event::None;
      }
      if (error != Error::Ok) return handleRequestError(error, Phase::FetchToc);
      requestSucceeded();
      phase_ = Phase::OpenToc;
      return Event::None;
    }

    case Phase::OpenToc:
      guardBookSession("toc");
      if (tocFile_.isOpen()) tocFile_.close();
      if (!WeReadStore::openToc(tocPath_, tocFile_, chapterCount_) || chapterCount_ == 0) {
        return fail(Error::Protocol);
      }
      chapterIndex_ = 0;
      psvts_[0] = '\0';
      logMemory("toc parsed");
      phase_ = Phase::LoadChapter;
      return Event::None;

    case Phase::LoadChapter: {
      if (chapterIndex_ >= chapterCount_) {
        bookSession_.reset();
        if (tocFile_.isOpen()) tocFile_.close();
        phase_ = Phase::PackageBook;
        return Event::None;
      }
      if (!WeReadStore::readTocRecord(tocFile_, chapterIndex_, chapter_)) return fail(Error::SdCard);
      if (Storage.exists(WeReadStore::chapterPath(bookDir_, chapterIndex_).c_str())) {
        phase_ = Phase::AdvanceChapter;
        return Event::None;
      }
      if (!makeReaderReferer(book_.bookId, chapter_.chapterUid, referer_)) return fail(Error::Protocol);
      if (!psvts_[0]) {
        if (!TimeUtils::isClockValid()) {
          // SNTP and TLS both hold network buffers. A cold-clock download
          // reconnects after sync rather than keeping both alive.
          bookSession_.reset();
          startClockSync();
          nextActionAt_ = millis() + kClockSyncTimeoutMs;
          phase_ = Phase::SyncClock;
          logMemory("clock sync start");
          return Event::None;
        }
        if (!refreshPsvts(psvts_, sizeof(psvts_))) return fail(Error::Clock);
      }
      primaryPsvtsRefreshed_ = false;
      phase_ = Phase::FetchPrimary;
      return Event::None;
    }

    case Phase::SyncClock:
      if (TimeUtils::isClockValid()) {
        stopClockSync();
        logMemory("clock sync complete");
        phase_ = Phase::LoadChapter;
        return Event::None;
      }
      if (static_cast<long>(millis() - nextActionAt_) < 0) return Event::None;
      stopClockSync();
      logMemory("clock sync timeout");
      return fail(Error::Clock);

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
      switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileContains(raw0, "{}"))) {
        case WeReadProtocol::ChapterResponse::Content:
          phase_ = Phase::FetchText1;
          return Event::None;
        case WeReadProtocol::ChapterResponse::Unavailable:
          phase_ = Phase::WriteUnavailable;
          return Event::None;
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
      switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileContains(raw1, "{}"))) {
        case WeReadProtocol::ChapterResponse::Content:
          if (!validateShard(bookDir_ + "/shard0.part") || !validateShard(raw1)) return fail(Error::Integrity);
          phase_ = Phase::DecodeText;
          return Event::None;
        case WeReadProtocol::ChapterResponse::Unavailable:
          phase_ = Phase::WriteUnavailable;
          return Event::None;
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
      switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileContains(raw1, "{}"))) {
        case WeReadProtocol::ChapterResponse::Content:
          phase_ = Phase::FetchEpub3;
          return Event::None;
        case WeReadProtocol::ChapterResponse::Unavailable:
          phase_ = Phase::WriteUnavailable;
          return Event::None;
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
      switch (WeReadProtocol::classifyChapterResponse(responseStatus_, smallFileContains(raw3, "{}"))) {
        case WeReadProtocol::ChapterResponse::Content:
          if (!validateShard(bookDir_ + "/shard0.part") || !validateShard(raw1) || !validateShard(raw3)) {
            return fail(Error::Integrity);
          }
          phase_ = Phase::DecodeEpub;
          return Event::None;
        case WeReadProtocol::ChapterResponse::Unavailable:
          phase_ = Phase::WriteUnavailable;
          return Event::None;
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

    case Phase::WriteUnavailable:
      guardBookSession("unavailable");
      if (!writePlaceholder(WeReadStore::chapterPath(bookDir_, chapterIndex_), chapter_.title)) {
        return fail(Error::SdCard);
      }
      phase_ = Phase::AdvanceChapter;
      return Event::None;

    case Phase::AdvanceChapter:
      guardBookSession("progress");
      ++chapterIndex_;
      phase_ = Phase::LoadChapter;
      return Event::ChapterComplete;

    case Phase::PackageBook: {
      bookSession_.reset();
      logMemory("package start");
      const Error error = packageBook(book_, bookDir_, tocPath_, chapterCount_, finalPartPath_);
      logMemory("package end");
      if (error != Error::Ok || !WeReadStore::atomicReplace(finalPartPath_, outputPath_)) {
        return fail(error == Error::Ok ? Error::SdCard : error);
      }
      cleanupTransient(bookDir_, "");
      if (!WeReadStore::saveSession(session_)) return fail(Error::SdCard);
      phase_ = Phase::Complete;
      logMemory("job complete");
      return Event::Complete;
    }
  }
  return fail(Error::Protocol);
}

}  // namespace WeReadClient

#endif
