#include "TxtToHtml.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

const char* TxtToHtml::cacheVersionTag(std::string_view filename) {
  return FsHelpers::hasMarkdownExtension(filename) ? "<!-- MD_CACHE_VERSION: 2 -->" : "<!-- TXT_CACHE_VERSION: 2 -->";
}

bool TxtToHtml::stream(std::string_view filename, void* readerCtx, int (*readFn)(void*, uint8_t*, size_t), Print& out) {
  return stream(filename, readerCtx, readFn, out, Options{});
}

bool TxtToHtml::stream(std::string_view filename, void* readerCtx, int (*readFn)(void*, uint8_t*, size_t), Print& out,
                       const Options& options) {
  constexpr size_t BUFFER_SIZE = 8192;
  // Two fixed 8 KiB buffers, allocated once per conversion; task stacks cannot hold them.
  auto inBuf = makeUniqueNoThrow<uint8_t[]>(BUFFER_SIZE);
  auto outBuf = makeUniqueNoThrow<uint8_t[]>(BUFFER_SIZE);
  if (!inBuf || !outBuf) {
    LOG_ERR("TXT", "OOM: TXT/MD HTML streaming buffers (%zu bytes)", BUFFER_SIZE * 2);
    return false;
  }

  size_t outPos = 0;
  bool outputOk = true;
  const auto flushOut = [&] {
    if (outPos) {
      outputOk = outputOk && out.write(outBuf.get(), outPos) == outPos;
      outPos = 0;
    }
  };
  const auto writeByte = [&](const uint8_t b) {
    outBuf[outPos++] = b;
    if (outPos == BUFFER_SIZE) flushOut();
  };
  const auto writeStr = [&](const std::string_view s) {
    for (const char c : s) writeByte(static_cast<uint8_t>(c));
  };
  const auto escaped = [&](const uint8_t b) {
    switch (b) {
      case '&':
        writeStr("&amp;");
        break;
      case '<':
        writeStr("&lt;");
        break;
      case '>':
        writeStr("&gt;");
        break;
      default:
        writeByte(b);
        break;
    }
  };

  writeStr("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
  writeStr(cacheVersionTag(filename));
  writeStr("\n<!DOCTYPE html>\n<html>\n<head><title>");
  const std::string title = FsHelpers::getFileNameWithoutExtension(filename);
  for (const char c : title) escaped(static_cast<uint8_t>(c));
  writeStr("</title></head>\n<body>\n");

  size_t inputPos = 0;
  int inputSize = 0;
  uint32_t source = 0;
  uint32_t visible = 1;  // The body opens with one newline, counted by ChapterHtmlSlimParser.
  bool readError = false;
  const auto readByte = [&]() -> int {
    if (inputPos == static_cast<size_t>(inputSize)) {
      inputSize = readFn(readerCtx, inBuf.get(), BUFFER_SIZE);
      inputPos = 0;
      if (inputSize <= 0) {
        readError = inputSize < 0;
        inputSize = 0;
        return -1;
      }
      if (inputSize > static_cast<int>(BUFFER_SIZE)) {
        readError = true;
        return -1;
      }
    }
    if (source == UINT32_MAX) {
      readError = true;
      return -1;
    }
    ++source;
    return inBuf[inputPos++];
  };
  const auto map = [&](const uint32_t pos, const uint8_t width, const uint8_t step) {
    if (options.map && !options.map(options.context, pos, visible, width, step)) outputOk = false;
  };

  bool atLineStart = true;
  uint32_t pendingSpaces = 0;
  uint32_t pendingStart = 0;
  const auto finishSpaces = [&](const bool keep) {
    if (!pendingSpaces) return;
    map(pendingStart, 1, keep ? 1 : 0);
    if (keep) {
      for (uint32_t i = 1; i < pendingSpaces; ++i) writeStr("&#160;");
      writeByte(' ');
      visible += pendingSpaces;
    }
    pendingSpaces = 0;
  };

  while (outputOk) {
    const uint32_t begin = source;
    const int first = readByte();
    if (first < 0) break;
    uint8_t character[8] = {static_cast<uint8_t>(first)};
    uint8_t width = 1;
    size_t utf8Length = 1;
    if (first >= 0x80) {
      if (options.encoding == txt_encoding::Encoding::Gbk) {
        const int second = readByte();
        if (second < 0) {
          readError = true;
          break;
        }
        character[1] = static_cast<uint8_t>(second);
        width = 2;
        const auto converted = txt_encoding::transcodeGbkInPlace(character, 2, sizeof(character), true);
        if (converted.rawLength != 2 || converted.utf8Length == 0) {
          readError = true;
          break;
        }
        utf8Length = converted.utf8Length;
      } else {
        width = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC2 ? 2 : 0;
        if (width == 0 || first > 0xF4) {
          readError = true;
          break;
        }
        for (uint8_t i = 1; i < width; ++i) {
          const int next = readByte();
          if (next < 0 || (next & 0xC0) != 0x80) {
            readError = true;
            break;
          }
          character[i] = static_cast<uint8_t>(next);
        }
        if (readError) break;
        if (txt_encoding::detect(character, width, true) != txt_encoding::Encoding::Utf8) {
          readError = true;
          break;
        }
        utf8Length = width;
      }
    }

    if (options.chapterOffset && options.chapterOffset(options.context) == begin) {
      finishSpaces(true);
      char anchor[48];
      snprintf(anchor, sizeof(anchor), "<div id=\"txt-%u\"></div>", static_cast<unsigned>(begin));
      writeStr(anchor);
      if (!options.nextChapter(options.context)) {
        outputOk = false;
        break;
      }
    }
    if (begin == 0 && utf8Length == 3 && character[0] == 0xEF && character[1] == 0xBB && character[2] == 0xBF) {
      map(begin, width, 0);
      continue;
    }
    if (first == '\r') {
      finishSpaces(false);
      map(begin, 1, 0);
      continue;
    }
    if (first == '\n') {
      finishSpaces(false);
      map(begin, 1, 0);
      writeStr("<br />");
      atLineStart = true;
      continue;
    }
    if (first == ' ' && !atLineStart) {
      if (!pendingSpaces) pendingStart = begin;
      ++pendingSpaces;
      continue;
    }
    finishSpaces(true);
    map(begin, width, 1);
    ++visible;
    if (first == ' ')
      writeStr("&#160;");
    else if (first < 0x20 && first != '\t')
      writeByte(' ');
    else
      for (size_t i = 0; i < utf8Length; ++i) escaped(character[i]);
    if (first != ' ') atLineStart = false;
  }
  finishSpaces(false);
  map(source, 0, 0);  // EOF sentinel; no footer whitespace belongs to the source file.
  if (readError || !outputOk) {
    LOG_ERR("TXT", "Failed TXT/MD conversion (input, index or output): %.*s", static_cast<int>(filename.size()),
            filename.data());
    return false;
  }
  writeStr("\n</body>\n</html>\n");
  flushOut();
  return outputOk;
}

bool TxtToHtml::stream(std::string_view filename, std::string_view content, Print& out) {
  struct ViewReader {
    std::string_view text;
    size_t pos = 0;
  } reader{content};
  return stream(
      filename, &reader,
      [](void* ctx, uint8_t* buf, size_t capacity) -> int {
        auto& reader = *static_cast<ViewReader*>(ctx);
        const size_t count = std::min(capacity, reader.text.size() - reader.pos);
        memcpy(buf, reader.text.data() + reader.pos, count);
        reader.pos += count;
        return static_cast<int>(count);
      },
      out);
}
