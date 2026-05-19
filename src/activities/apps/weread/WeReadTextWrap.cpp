#include "WeReadTextWrap.h"

#include <GfxRenderer.h>
#include <Utf8.h>

#include <cstdint>
#include <utility>

namespace WeReadTextWrap {
namespace {

void appendCodepoint(std::string& dst, uint32_t cp) {
  if (cp < 0x80) {
    dst.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    dst.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    dst.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    dst.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    dst.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    dst.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    dst.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    dst.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}  // namespace

std::vector<std::string> wrap(const GfxRenderer& renderer, int fontId, const std::string& text, int maxWidth,
                              int maxLines) {
  std::vector<std::string> out;
  if (text.empty() || maxWidth <= 0) return out;

  std::string current;
  current.reserve(text.size());

  auto flushLine = [&]() {
    out.push_back(std::move(current));
    current.clear();
  };

  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  bool atLineStart = true;
  while (true) {
    uint32_t cp = utf8NextCodepoint(&p);
    if (cp == 0) break;

    if (cp == '\n') {
      flushLine();
      atLineStart = true;
      continue;
    }
    if (atLineStart && cp == ' ') continue;
    atLineStart = false;

    std::string candidate = current;
    appendCodepoint(candidate, cp);
    if (renderer.getTextWidth(fontId, candidate.c_str()) <= maxWidth) {
      current = std::move(candidate);
      continue;
    }

    if (current.empty()) {
      current = std::move(candidate);
      flushLine();
      atLineStart = true;
      continue;
    }

    const bool cpIsCjk = utf8IsCjkBreakable(cp);
    const bool cpIsSpace = (cp == ' ');
    const unsigned char lastByte = static_cast<unsigned char>(current.back());
    const bool prevIsCjkLike = (lastByte >= 0x80);

    if (cpIsCjk || prevIsCjkLike || cpIsSpace) {
      flushLine();
      if (!cpIsSpace) {
        appendCodepoint(current, cp);
      } else {
        atLineStart = true;
      }
      continue;
    }

    size_t sp = current.find_last_of(' ');
    if (sp == std::string::npos) {
      flushLine();
      appendCodepoint(current, cp);
    } else {
      std::string carry = current.substr(sp + 1);
      current.erase(sp);
      flushLine();
      current = std::move(carry);
      appendCodepoint(current, cp);
    }
  }

  if (!current.empty()) out.push_back(std::move(current));

  if (maxLines > 0 && static_cast<int>(out.size()) > maxLines) {
    out.resize(maxLines);
    std::string& last = out.back();
    // Append "…" (U+2026, UTF-8: 0xE2 0x80 0xA6); if it overflows the width,
    // truncatedText() strips trailing codepoints until it fits.
    last.append("\xe2\x80\xa6");
    if (renderer.getTextWidth(fontId, last.c_str()) > maxWidth) {
      last = renderer.truncatedText(fontId, last.c_str(), maxWidth);
    }
  }

  return out;
}

}  // namespace WeReadTextWrap
