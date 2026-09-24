#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// Uploads a WAV from the SD card to OpenAI's Whisper transcription endpoint and
// writes the plain-text transcript next to it. The multipart body is streamed
// from SD in fixed chunks, so peak RAM is one chunk plus the TLS session, not
// the recording (SecureHttpClient needs the whole body in one buffer).
namespace OpenAiTranscriber {

enum class Status : uint8_t {
  Ok,
  NoApiKey,
  FileError,
  FileTooLarge,
  LowMemory,
  ConnectFailed,
  SendFailed,
  ResponseTimeout,
  HttpError,  // non-200: message holds OpenAI's error.message when present
  WriteFailed,
  Cancelled,
};

struct Result {
  Status status = Status::FileError;
  int httpStatus = 0;
  char message[160] = {};
};

// Called from the calling task while uploading (sent < total) and while
// waiting for the response (sent == total). Return false to cancel.
using ProgressCallback = std::function<bool(size_t sent, size_t total)>;

// Whisper rejects uploads above 25 MB.
inline constexpr size_t MAX_UPLOAD_BYTES = 25u * 1024u * 1024u;

// Blocks until done. Requires Wi-Fi to be connected. On success txtPath holds
// the transcript (written via a temp file, then renamed).
Result transcribe(const char* apiKey, const char* wavPath, const char* txtPath, const ProgressCallback& progress);

}  // namespace OpenAiTranscriber
