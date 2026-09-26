#include "OpenAiTranscriber.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureClient.h>
#include <strings.h>

#include <cstdio>
#include <cstring>

namespace OpenAiTranscriber {
namespace {

constexpr char HOST[] = "api.openai.com";
constexpr uint16_t PORT = 443;
constexpr char PATH[] = "/v1/audio/transcriptions";
constexpr char MODEL[] = "whisper-1";
constexpr char BOUNDARY[] = "----CrossMuxVoiceNote7d3f9a41";

// One buffer serves the request head, the SD->TLS body chunks, and the
// response reader. 4 KB keeps SD reads block-aligned without a large heap hit.
constexpr size_t CHUNK_BYTES = 4096;
constexpr size_t MAX_LINE = 512;
// Only error bodies are buffered (to pull out error.message).
constexpr size_t ERROR_BODY_MAX = 1024;

constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t IO_STALL_TIMEOUT_MS = 30000;
// Whisper can take a minute or more on long audio after the upload finishes.
constexpr uint32_t RESPONSE_TIMEOUT_MS = 180000;

// Same floors as the KOReader client: the largest single wolfSSL allocation is
// the ~17 KB record buffer; 35 KB free covers session + record + SP workspace.
constexpr uint32_t MIN_FREE_FOR_TLS = 35 * 1024;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20 * 1024;

constexpr int MAX_ATTEMPTS = 3;
constexpr uint32_t RETRY_DELAY_MS[MAX_ATTEMPTS] = {0, 1000, 3000};

using Sink = bool (*)(void* ctx, const uint8_t* data, size_t len);

void setMessage(Result& result, const char* text) { snprintf(result.message, sizeof(result.message), "%s", text); }

const char* baseName(const char* path) {
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

bool writeAll(freeink::SecureClient& client, const uint8_t* data, const size_t len) {
  size_t offset = 0;
  uint32_t lastProgress = millis();
  while (offset < len) {
    // wolfSSL requires a WANT_WRITE retry with the same buffer and length.
    const size_t n = client.write(data + offset, len - offset);
    if (n > 0) {
      offset += n;
      lastProgress = millis();
      continue;
    }
    if (!client.connected()) return false;
    if (millis() - lastProgress >= IO_STALL_TIMEOUT_MS) return false;
    delay(2);
  }
  return true;
}

// Buffered reader over the TLS stream with an overall deadline and a
// cancellation poll through the progress callback.
class ResponseReader {
 public:
  ResponseReader(freeink::SecureClient& client, uint8_t* buffer, const size_t capacity,
                 const ProgressCallback& progress, const size_t total)
      : client_(client), buf_(buffer), cap_(capacity), progress_(progress), total_(total) {}

  bool timedOut() const { return timedOut_; }
  bool cancelled() const { return cancelled_; }
  bool gotBytes() const { return gotBytes_; }

  bool readLine(char* out, const size_t outCap) {
    size_t n = 0;
    while (true) {
      if (pos_ >= len_ && !fill()) return false;
      const char c = static_cast<char>(buf_[pos_++]);
      if (c == '\n') break;
      if (n + 1 >= outCap) return false;
      out[n++] = c;
    }
    if (n > 0 && out[n - 1] == '\r') --n;
    out[n] = '\0';
    return true;
  }

  bool readExact(size_t count, Sink sink, void* ctx) {
    while (count > 0) {
      if (pos_ >= len_ && !fill()) return false;
      const size_t take = count < len_ - pos_ ? count : len_ - pos_;
      if (!sink(ctx, buf_ + pos_, take)) return false;
      pos_ += take;
      count -= take;
    }
    return true;
  }

  // Connection: close framing: the body ends when the peer closes.
  bool readToEof(Sink sink, void* ctx) {
    while (true) {
      if (pos_ >= len_ && !fill()) return !timedOut_ && !cancelled_;
      if (!sink(ctx, buf_ + pos_, len_ - pos_)) return false;
      pos_ = len_;
    }
  }

  bool readChunked(Sink sink, void* ctx) {
    char line[32];
    while (true) {
      if (!readLine(line, sizeof(line))) return false;
      const size_t size = strtoul(line, nullptr, 16);
      if (size == 0) {
        // Drain optional trailers up to the terminating blank line.
        char trailer[MAX_LINE];
        while (readLine(trailer, sizeof(trailer)) && trailer[0] != '\0') {
        }
        return true;
      }
      if (!readExact(size, sink, ctx)) return false;
      if (!readLine(line, sizeof(line))) return false;  // CRLF after chunk data
    }
  }

 private:
  bool fill() {
    const uint32_t start = millis();
    while (true) {
      if (progress_ && !progress_(total_, total_)) {
        cancelled_ = true;
        return false;
      }
      const int n = client_.read(buf_, cap_);
      if (n > 0) {
        len_ = static_cast<size_t>(n);
        pos_ = 0;
        gotBytes_ = true;
        return true;
      }
      if (n < 0 || !client_.connected()) return false;  // peer closed
      if (millis() - start >= RESPONSE_TIMEOUT_MS) {
        timedOut_ = true;
        return false;
      }
      delay(10);
    }
  }

  freeink::SecureClient& client_;
  uint8_t* buf_;
  size_t cap_;
  const ProgressCallback& progress_;
  size_t total_;
  size_t pos_ = 0;
  size_t len_ = 0;
  bool timedOut_ = false;
  bool cancelled_ = false;
  bool gotBytes_ = false;
};

struct FileSinkCtx {
  HalFile* file;
  bool failed;
};

bool fileSink(void* ctx, const uint8_t* data, const size_t len) {
  auto* sink = static_cast<FileSinkCtx*>(ctx);
  if (sink->file->write(data, len) != len) {
    sink->failed = true;
    return false;
  }
  return true;
}

struct TextSinkCtx {
  char* buf;
  size_t len;
  size_t cap;
};

bool textSink(void* ctx, const uint8_t* data, const size_t len) {
  auto* sink = static_cast<TextSinkCtx*>(ctx);
  const size_t room = sink->cap - 1 - sink->len;
  const size_t take = len < room ? len : room;
  memcpy(sink->buf + sink->len, data, take);
  sink->len += take;
  sink->buf[sink->len] = '\0';
  return true;  // keep draining past the cap so the framing stays intact
}

// Pulls "message": "..." out of OpenAI's {"error": {...}} body without a JSON
// parser; escapes other than \" and \\ are kept verbatim.
void extractErrorMessage(const char* body, char* out, const size_t outCap) {
  const char* key = strstr(body, "\"message\"");
  if (!key) return;
  const char* p = strchr(key + 9, ':');
  if (!p) return;
  p = strchr(p, '"');
  if (!p) return;
  ++p;
  size_t n = 0;
  while (*p && *p != '"' && n + 1 < outCap) {
    if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) ++p;
    out[n++] = *p++;
  }
  out[n] = '\0';
}

enum class Attempt : uint8_t { Done, RetryableFailure };

Attempt attemptOnce(const char* apiKey, const char* wavPath, const char* txtPath, HalFile& wav, const size_t wavSize,
                    uint8_t* buf, const ProgressCallback& progress, Result& result) {
  const char* fileName = baseName(wavPath);
  static constexpr char PREAMBLE_FMT[] =
      "--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n%s\r\n"
      "--%s\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\ntext\r\n"
      "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
      "Content-Type: audio/wav\r\n\r\n";
  static constexpr char TRAILER_FMT[] = "\r\n--%s--\r\n";
  const int preambleLen = snprintf(nullptr, 0, PREAMBLE_FMT, BOUNDARY, MODEL, BOUNDARY, BOUNDARY, fileName);
  const int trailerLen = snprintf(nullptr, 0, TRAILER_FMT, BOUNDARY);
  const size_t total = static_cast<size_t>(preambleLen) + wavSize + static_cast<size_t>(trailerLen);

  freeink::SecureClient client;
  client.setInsecure();
  client.setTimeout(CONNECT_TIMEOUT_MS);
  LOG_INF("OAI", "Connecting (free=%u largest=%u)", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (!client.connect(HOST, PORT)) {
    result.status = Status::ConnectFailed;
    setMessage(result, "TLS connect failed");
    return Attempt::RetryableFailure;
  }

  const int headLen = snprintf(reinterpret_cast<char*>(buf), CHUNK_BYTES,
                               "POST %s HTTP/1.1\r\n"
                               "Host: %s\r\n"
                               "Authorization: Bearer %s\r\n"
                               "User-Agent: CrossMux\r\n"
                               "Accept: text/plain\r\n"
                               "Content-Type: multipart/form-data; boundary=%s\r\n"
                               "Content-Length: %u\r\n"
                               "Connection: close\r\n\r\n",
                               PATH, HOST, apiKey, BOUNDARY, static_cast<unsigned>(total));
  bool sent =
      headLen > 0 && static_cast<size_t>(headLen) < CHUNK_BYTES && writeAll(client, buf, static_cast<size_t>(headLen));
  if (sent) {
    snprintf(reinterpret_cast<char*>(buf), CHUNK_BYTES, PREAMBLE_FMT, BOUNDARY, MODEL, BOUNDARY, BOUNDARY, fileName);
    sent = writeAll(client, buf, static_cast<size_t>(preambleLen));
  }

  size_t sentBytes = static_cast<size_t>(preambleLen);
  if (sent && !wav.seek(0)) {
    result.status = Status::FileError;
    setMessage(result, "Could not rewind recording");
    return Attempt::Done;
  }
  size_t remaining = wavSize;
  while (sent && remaining > 0) {
    if (progress && !progress(sentBytes, total)) {
      result.status = Status::Cancelled;
      return Attempt::Done;
    }
    const size_t want = remaining < CHUNK_BYTES ? remaining : CHUNK_BYTES;
    const int got = wav.read(buf, want);
    if (got <= 0) {
      result.status = Status::FileError;
      setMessage(result, "SD read failed");
      return Attempt::Done;
    }
    sent = writeAll(client, buf, static_cast<size_t>(got));
    remaining -= static_cast<size_t>(got);
    sentBytes += static_cast<size_t>(got);
  }
  if (sent) {
    snprintf(reinterpret_cast<char*>(buf), CHUNK_BYTES, TRAILER_FMT, BOUNDARY);
    sent = writeAll(client, buf, static_cast<size_t>(trailerLen));
  }
  if (!sent) {
    result.status = Status::SendFailed;
    setMessage(result, "Upload interrupted");
    return Attempt::RetryableFailure;
  }
  LOG_INF("OAI", "Uploaded %u bytes; waiting for transcript", static_cast<unsigned>(total));

  ResponseReader reader(client, buf, CHUNK_BYTES, progress, total);
  char line[MAX_LINE];
  if (!reader.readLine(line, sizeof(line))) {
    if (reader.cancelled()) {
      result.status = Status::Cancelled;
      return Attempt::Done;
    }
    result.status = reader.timedOut() ? Status::ResponseTimeout : Status::SendFailed;
    setMessage(result, reader.timedOut() ? "No response from OpenAI" : "Connection closed before response");
    return reader.gotBytes() || reader.timedOut() ? Attempt::Done : Attempt::RetryableFailure;
  }
  int httpStatus = 0;
  if (sscanf(line, "HTTP/%*s %d", &httpStatus) != 1) {
    result.status = Status::HttpError;
    setMessage(result, "Malformed HTTP response");
    return Attempt::Done;
  }
  result.httpStatus = httpStatus;

  bool chunked = false;
  bool haveLength = false;
  size_t contentLength = 0;
  while (true) {
    if (!reader.readLine(line, sizeof(line))) {
      result.status = reader.cancelled() ? Status::Cancelled : Status::HttpError;
      setMessage(result, "Truncated response headers");
      return Attempt::Done;
    }
    if (line[0] == '\0') break;
    if (strncasecmp(line, "Content-Length:", 15) == 0) {
      contentLength = strtoul(line + 15, nullptr, 10);
      haveLength = true;
    } else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0 && strstr(line + 18, "chunked")) {
      chunked = true;
    }
  }

  auto readBody = [&](Sink sink, void* ctx) {
    if (chunked) return reader.readChunked(sink, ctx);
    if (haveLength) return reader.readExact(contentLength, sink, ctx);
    return reader.readToEof(sink, ctx);
  };

  if (httpStatus != 200) {
    result.status = Status::HttpError;
    snprintf(result.message, sizeof(result.message), "HTTP %d", httpStatus);
    // Error bodies are small JSON; buffer just enough to extract the message.
    auto body = makeUniqueNoThrow<char[]>(ERROR_BODY_MAX);
    if (body) {
      TextSinkCtx ctx{body.get(), 0, ERROR_BODY_MAX};
      body[0] = '\0';
      readBody(textSink, &ctx);
      extractErrorMessage(body.get(), result.message, sizeof(result.message));
    }
    LOG_ERR("OAI", "HTTP %d: %s", httpStatus, result.message);
    return Attempt::Done;
  }

  // Stream the transcript to a temp file so a failed read never replaces an
  // earlier good transcript.
  char tmpPath[128];
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", txtPath);
  bool bodyOk = false;
  bool writeFailed = false;
  {
    HalFile out;
    if (!Storage.openFileForWrite("OAI", tmpPath, out)) {
      result.status = Status::WriteFailed;
      setMessage(result, "Could not create transcript file");
      return Attempt::Done;
    }
    FileSinkCtx ctx{&out, false};
    bodyOk = readBody(fileSink, &ctx);
    writeFailed = ctx.failed;
    out.flush();
  }
  if (!bodyOk || writeFailed) {
    Storage.remove(tmpPath);
    if (reader.cancelled()) {
      result.status = Status::Cancelled;
    } else {
      result.status = writeFailed ? Status::WriteFailed : Status::ResponseTimeout;
      setMessage(result, writeFailed ? "Could not write transcript" : "Transcript download interrupted");
    }
    return Attempt::Done;
  }
  if (Storage.exists(txtPath)) Storage.remove(txtPath);
  if (!Storage.rename(tmpPath, txtPath)) {
    Storage.remove(tmpPath);
    result.status = Status::WriteFailed;
    setMessage(result, "Could not save transcript");
    return Attempt::Done;
  }
  result.status = Status::Ok;
  result.message[0] = '\0';
  return Attempt::Done;
}

}  // namespace

Result transcribe(const char* apiKey, const char* wavPath, const char* txtPath, const ProgressCallback& progress) {
  Result result;
  if (!apiKey || apiKey[0] == '\0') {
    result.status = Status::NoApiKey;
    return result;
  }

  HalFile wav;
  if (!Storage.openFileForRead("OAI", wavPath, wav)) {
    result.status = Status::FileError;
    setMessage(result, "Could not open recording");
    return result;
  }
  const size_t wavSize = wav.fileSize();
  if (wavSize > MAX_UPLOAD_BYTES) {
    result.status = Status::FileTooLarge;
    return result;
  }

  // Held only for the duration of the request; freed on return.
  auto buf = makeUniqueNoThrow<uint8_t[]>(CHUNK_BYTES);
  if (!buf) {
    LOG_ERR("OAI", "OOM: transfer buffer (%u bytes)", static_cast<unsigned>(CHUNK_BYTES));
    result.status = Status::LowMemory;
    return result;
  }

  for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
    if (RETRY_DELAY_MS[attempt] > 0) {
      LOG_INF("OAI", "Retrying in %u ms", static_cast<unsigned>(RETRY_DELAY_MS[attempt]));
      const uint32_t until = millis() + RETRY_DELAY_MS[attempt];
      while (static_cast<int32_t>(millis() - until) < 0) {
        if (progress && !progress(0, wavSize)) {
          result.status = Status::Cancelled;
          return result;
        }
        delay(50);
      }
    }
    if (ESP.getFreeHeap() < MIN_FREE_FOR_TLS || ESP.getMaxAllocHeap() < MIN_BLOCK_FOR_TLS) {
      LOG_ERR("OAI", "Insufficient heap for TLS: free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(ESP.getMaxAllocHeap()));
      result.status = Status::LowMemory;
      return result;
    }
    result = Result{};
    if (attemptOnce(apiKey, wavPath, txtPath, wav, wavSize, buf.get(), progress, result) == Attempt::Done) break;
    LOG_ERR("OAI", "Attempt %d failed: %s", attempt + 1, result.message);
  }
  return result;
}

}  // namespace OpenAiTranscriber
