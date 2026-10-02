#pragma once

#include <Print.h>

#include <cstdint>
#include <string_view>

#include "TxtEncoding.h"

class TxtToHtml {
 public:
  struct Options {
    txt_encoding::Encoding encoding = txt_encoding::Encoding::Utf8;
    void* context = nullptr;
    bool (*map)(void*, uint32_t source, uint32_t visible, uint8_t sourceWidth, uint8_t visibleStep) = nullptr;
    uint32_t (*chapterOffset)(void*) = nullptr;
    bool (*nextChapter)(void*) = nullptr;
  };

  static const char* cacheVersionTag(std::string_view filename);
  static bool stream(std::string_view filename, void* readerCtx, int (*readFn)(void*, uint8_t*, size_t), Print& out);
  static bool stream(std::string_view filename, void* readerCtx, int (*readFn)(void*, uint8_t*, size_t), Print& out,
                     const Options& options);
  static bool stream(std::string_view filename, std::string_view content, Print& out);
};
