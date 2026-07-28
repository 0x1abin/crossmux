#pragma once

#include <cstddef>

namespace SdCardFontCache {

enum class Result {
  Ok,
  AlreadyCached,
  OpenFailed,
  InvalidFont,
  TooLarge,
  NotSafe,
  Oom,
  EraseFailed,
  ReadFailed,
  WriteFailed,
  VerifyFailed,
};

using ProgressCallback = void (*)(size_t completed, size_t total, void* context);

size_t capacity();
bool isValidFor(const char* sourcePath, size_t* payloadSize = nullptr);
bool readAt(size_t offset, void* data, size_t length, size_t payloadSize);
Result preload(const char* sourcePath, ProgressCallback progress = nullptr, void* context = nullptr);
const char* resultName(Result result);

}  // namespace SdCardFontCache
