#include "WeReadXhtmlCodec.h"

#if defined(ENABLE_CHINESE_VERSION) || defined(CROSSPOINT_EMULATED)

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "WeReadProtocol.h"
#include "WeReadStore.h"

namespace WeReadXhtmlCodec {
namespace {

constexpr char kSourceOffsetMarkerName[] = "wr-co:";

bool isUtf8Continuation(const uint8_t value) { return (value & 0xC0) == 0x80; }

uint32_t utf16Width(const uint8_t utf8Lead) { return utf8Lead >= 0xF0 && utf8Lead <= 0xF4 ? 2U : 1U; }

bool equalsIgnoreCase(const char* value, const size_t length, const char* expected) {
  if (!value || !expected || strlen(expected) != length) return false;
  for (size_t i = 0; i < length; ++i) {
    if (std::tolower(static_cast<unsigned char>(value[i])) != std::tolower(static_cast<unsigned char>(expected[i]))) {
      return false;
    }
  }
  return true;
}

bool hasAttributeToken(const char* tag, const char* expectedName, const char* expectedToken) {
  if (!tag || !expectedName || !expectedToken) return false;
  const char* cursor = tag;
  while (*cursor && !std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  while (*cursor) {
    while (std::isspace(static_cast<unsigned char>(*cursor)) || *cursor == '/') ++cursor;
    if (!*cursor) break;
    const char* name = cursor;
    while (*cursor && !std::isspace(static_cast<unsigned char>(*cursor)) && *cursor != '=' && *cursor != '/') {
      ++cursor;
    }
    const size_t nameLength = static_cast<size_t>(cursor - name);
    while (std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
    if (*cursor != '=') {
      while (*cursor && !std::isspace(static_cast<unsigned char>(*cursor)) && *cursor != '/') ++cursor;
      continue;
    }
    ++cursor;
    while (std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
    if (*cursor != '"' && *cursor != '\'') {
      while (*cursor && !std::isspace(static_cast<unsigned char>(*cursor)) && *cursor != '/') ++cursor;
      continue;
    }
    const char quote = *cursor++;
    const char* value = cursor;
    while (*cursor && *cursor != quote) ++cursor;
    if (*cursor != quote) return false;
    const char* valueEnd = cursor++;
    if (!equalsIgnoreCase(name, nameLength, expectedName)) continue;
    while (value < valueEnd) {
      while (value < valueEnd && std::isspace(static_cast<unsigned char>(*value))) ++value;
      const char* token = value;
      while (value < valueEnd && !std::isspace(static_cast<unsigned char>(*value))) ++value;
      if (equalsIgnoreCase(token, static_cast<size_t>(value - token), expectedToken)) return true;
    }
  }
  return false;
}

bool isAnnotationTag(const char* tag) {
  return hasAttributeToken(tag, "class", "duokan-footnote") ||
         hasAttributeToken(tag, "class", "duokan-footnote-content") ||
         hasAttributeToken(tag, "class", "duokan-footnote-item") || hasAttributeToken(tag, "epub:type", "noteref") ||
         hasAttributeToken(tag, "epub:type", "footnote") || hasAttributeToken(tag, "epub:type", "endnote") ||
         hasAttributeToken(tag, "epub:type", "rearnote") || hasAttributeToken(tag, "type", "noteref") ||
         hasAttributeToken(tag, "type", "footnote") || hasAttributeToken(tag, "type", "endnote") ||
         hasAttributeToken(tag, "type", "rearnote") || hasAttributeToken(tag, "role", "doc-noteref") ||
         hasAttributeToken(tag, "role", "doc-footnote") || hasAttributeToken(tag, "role", "doc-endnote") ||
         hasAttributeToken(tag, "role", "doc-rearnote");
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
  bool skipHead = false;
  bool tagOverflow = false;
  bool textRunOpen = false;
  char skippedElement[24] = {};
  size_t skipDepth = 0;
  size_t tagLen = 0;
  char entity[24] = {};
  size_t entityLen = 0;
  uint32_t sourceUtf16Offset = 0;
  uint32_t tagSourceOffset = 0;
  uint32_t entitySourceOffset = 0;
};

bool writeSourceOffsetMarker(XhtmlSanitizer& sanitizer, const uint32_t offset) {
  char marker[32];
  const int length =
      snprintf(marker, sizeof(marker), "<!--%s%u-->", kSourceOffsetMarkerName, static_cast<unsigned>(offset));
  return length > 0 && static_cast<size_t>(length) < sizeof(marker) && writeLiteral(*sanitizer.output, marker);
}

bool advanceSourceUtf16Offset(XhtmlSanitizer& sanitizer, const uint8_t* data, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (isUtf8Continuation(data[i])) continue;
    const uint32_t width = utf16Width(data[i]);
    if (sanitizer.sourceUtf16Offset > UINT32_MAX - width) return false;
    sanitizer.sourceUtf16Offset += width;
  }
  return true;
}

bool emitEntity(XhtmlSanitizer& sanitizer) {
  sanitizer.entity[sanitizer.entityLen] = '\0';
  if (sanitizer.skipDepth == 0 && !sanitizer.skipHead &&
      !writeSourceOffsetMarker(sanitizer, sanitizer.entitySourceOffset)) {
    return false;
  }
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

bool emitSanitizedTextByte(XhtmlSanitizer& sanitizer, const uint8_t value, const uint32_t sourceOffset) {
  if (sanitizer.skipDepth != 0 || sanitizer.skipHead) return true;
  if (sanitizer.plainText) {
    if (value == '\r') return true;
    if (value == '\n') {
      return writeSourceOffsetMarker(sanitizer, sourceOffset) && writeLiteral(*sanitizer.output, "<br/>");
    }
  }
  if (sanitizer.inEntity) {
    if (value == ';') return emitEntity(sanitizer);
    if (sanitizer.entityLen + 1 >= sizeof(sanitizer.entity) || value == '<' || value == '&' || std::isspace(value)) {
      if (!writeSourceOffsetMarker(sanitizer, sanitizer.entitySourceOffset) ||
          !writeLiteral(*sanitizer.output, "&amp;") ||
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
    sanitizer.entitySourceOffset = sourceOffset;
    return true;
  }
  if (value < 0x20 && value != '\t' && value != '\n') return true;
  if (!writeSourceOffsetMarker(sanitizer, sourceOffset)) return false;
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

  if (sanitizer.skipDepth != 0) {
    if (strcmp(name, sanitizer.skippedElement) == 0) {
      if (closing) {
        --sanitizer.skipDepth;
        if (sanitizer.skipDepth == 0) sanitizer.skippedElement[0] = '\0';
      } else if (!selfClosing) {
        ++sanitizer.skipDepth;
      }
    }
    return true;
  }

  if (strcmp(name, "head") == 0) {
    sanitizer.skipHead = !closing && !selfClosing;
    return true;
  }
  if (!closing && isAnnotationTag(sanitizer.tag)) {
    if (selfClosing) return true;
    memcpy(sanitizer.skippedElement, name, len + 1);
    sanitizer.skipDepth = 1;
    return true;
  }
  if (!closing && !selfClosing && (strcmp(name, "script") == 0 || strcmp(name, "style") == 0)) {
    memcpy(sanitizer.skippedElement, name, len + 1);
    sanitizer.skipDepth = 1;
    return true;
  }
  if (!closing && strcmp(name, "img") == 0 && !sanitizer.skipHead) {
    WeReadStore::ImageRecord record;
    char alt[256] = {};
    const bool extracted =
        WeReadProtocol::extractImageAttributes(sanitizer.tag, record.url, sizeof(record.url), alt, sizeof(alt));
    const WeReadProtocol::ImageType type =
        extracted ? WeReadProtocol::normalizeImageUrl(record.url, sanitizer.tag, sizeof(record.url))
                  : WeReadProtocol::ImageType::None;
    decodeBasicHtmlEntities(alt);
    if (!writeSourceOffsetMarker(sanitizer, sanitizer.tagSourceOffset)) return false;
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
  if (sanitizer.skipHead || strcmp(name, "html") == 0 || strcmp(name, "body") == 0 ||
      !WeReadProtocol::isAllowedXhtmlTag(name)) {
    return true;
  }
  if (!closing && (strcmp(name, "br") == 0 || strcmp(name, "hr") == 0) &&
      !writeSourceOffsetMarker(sanitizer, sanitizer.tagSourceOffset)) {
    return false;
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

enum class SourceOffsetDirection : uint8_t {
  VisibleToNative,
  NativeToVisible,
};

class SourceOffsetResolver {
 public:
  SourceOffsetResolver(const SourceOffsetDirection direction, const uint32_t target)
      : direction_(direction), target_(target) {}

  bool feed(const uint8_t* data, const size_t length) {
    if (!data) return false;
    for (size_t i = 0; i < length && !invalid_; ++i) consume(data[i]);
    return !invalid_;
  }

  bool finish(uint32_t& result) {
    if (invalid_ || state_ != State::Text || !hasMarker_) return false;
    if (!found_) {
      switch (direction_) {
        case SourceOffsetDirection::VisibleToNative:
          if (visibleOffset_ == target_) setResult(nativeOffset_);
          break;
        case SourceOffsetDirection::NativeToVisible:
          if (target_ <= nativeOffset_) setResult(visibleOffset_);
          break;
      }
    }
    if (!found_) return false;
    result = result_;
    return true;
  }

 private:
  enum class State : uint8_t {
    Text,
    Tag,
    Entity,
  };

  void setResult(const uint32_t value) {
    if (found_) return;
    result_ = value;
    found_ = true;
  }

  void consume(const uint8_t value) {
    switch (state_) {
      case State::Text:
        consumeText(value);
        return;
      case State::Tag:
        consumeTag(value);
        return;
      case State::Entity:
        consumeEntity(value);
        return;
    }
  }

  void consumeText(const uint8_t value) {
    if (value == '<') {
      previousWasCarriageReturn_ = false;
      state_ = State::Tag;
      tokenLength_ = 0;
      tokenOverflow_ = false;
      return;
    }
    if (!insideBody_) return;
    if (value == '&') {
      previousWasCarriageReturn_ = false;
      state_ = State::Entity;
      tokenLength_ = 0;
      tokenOverflow_ = false;
      return;
    }
    if (isUtf8Continuation(value)) return;
    if (value == '\n' && previousWasCarriageReturn_) {
      if (hasMarker_) {
        if (direction_ == SourceOffsetDirection::NativeToVisible && target_ <= nativeOffset_) {
          setResult(visibleOffset_);
          return;
        }
        ++nativeOffset_;
      }
      previousWasCarriageReturn_ = false;
      return;
    }
    onVisibleCodepoint(utf16Width(value));
    previousWasCarriageReturn_ = value == '\r';
  }

  void consumeTag(const uint8_t value) {
    if (value == '>') {
      token_[tokenLength_] = '\0';
      processTag();
      state_ = State::Text;
      return;
    }
    if (tokenLength_ + 1 < sizeof(token_)) {
      token_[tokenLength_++] = static_cast<char>(value);
    } else {
      tokenOverflow_ = true;
    }
  }

  void consumeEntity(const uint8_t value) {
    if (value == ';') {
      token_[tokenLength_] = '\0';
      // Entities are bounded by source markers, so only advance the visible
      // coordinate. The following marker supplies the next native offset.
      onVisibleCodepoint(0);
      state_ = State::Text;
      return;
    }
    if (value == '<' || value == '&' || std::isspace(value) || tokenLength_ + 1 >= sizeof(token_)) {
      invalid_ = true;
      return;
    }
    token_[tokenLength_++] = static_cast<char>(value);
  }

  void processTag() {
    static constexpr char kCommentPrefix[] = "!--";
    constexpr size_t kCommentPrefixLength = sizeof(kCommentPrefix) - 1;
    constexpr size_t kMarkerNameLength = sizeof(kSourceOffsetMarkerName) - 1;
    constexpr size_t kMarkerPrefixLength = kCommentPrefixLength + kMarkerNameLength;
    if (!tokenOverflow_ && strncmp(token_, kCommentPrefix, kCommentPrefixLength) == 0 &&
        strncmp(token_ + kCommentPrefixLength, kSourceOffsetMarkerName, kMarkerNameLength) == 0) {
      processMarker(kMarkerPrefixLength);
      return;
    }

    if (strcmp(token_, "body") == 0) insideBody_ = true;
    if (strcmp(token_, "/body") == 0) insideBody_ = false;
  }

  void processMarker(const size_t prefixLength) {
    if (!insideBody_ || tokenLength_ <= prefixLength + 2 || token_[tokenLength_ - 2] != '-' ||
        token_[tokenLength_ - 1] != '-') {
      invalid_ = true;
      return;
    }
    uint32_t value = 0;
    for (size_t i = prefixLength; i + 2 < tokenLength_; ++i) {
      if (token_[i] < '0' || token_[i] > '9') {
        invalid_ = true;
        return;
      }
      const uint32_t digit = static_cast<uint32_t>(token_[i] - '0');
      if (value > (UINT32_MAX - digit) / 10) {
        invalid_ = true;
        return;
      }
      value = value * 10 + digit;
    }
    if (hasMarker_ && value < lastMarkerOffset_) {
      invalid_ = true;
      return;
    }
    nativeOffset_ = value;
    lastMarkerOffset_ = value;
    hasMarker_ = true;
    if (direction_ == SourceOffsetDirection::NativeToVisible && target_ <= value) setResult(visibleOffset_);
  }

  void onVisibleCodepoint(const uint32_t width) {
    if (hasMarker_) {
      switch (direction_) {
        case SourceOffsetDirection::VisibleToNative:
          if (visibleOffset_ == target_) {
            setResult(nativeOffset_);
            return;
          }
          break;
        case SourceOffsetDirection::NativeToVisible:
          if (target_ <= nativeOffset_ || target_ < nativeOffset_ + width) {
            setResult(visibleOffset_);
            return;
          }
          break;
      }
      nativeOffset_ += width;
    }
    ++visibleOffset_;
  }

  SourceOffsetDirection direction_;
  uint32_t target_ = 0;
  uint32_t visibleOffset_ = 0;
  uint32_t nativeOffset_ = 0;
  uint32_t lastMarkerOffset_ = 0;
  uint32_t result_ = 0;
  State state_ = State::Text;
  char token_[40] = {};
  size_t tokenLength_ = 0;
  bool insideBody_ = false;
  bool tokenOverflow_ = false;
  bool hasMarker_ = false;
  bool found_ = false;
  bool invalid_ = false;
  bool previousWasCarriageReturn_ = false;
};

bool resolveSourceOffset(const std::string& path, const SourceOffsetDirection direction, const uint32_t input,
                         uint32_t& output) {
  HalFile file;
  if (!Storage.openFileForRead("WR", path, file)) return false;
  SourceOffsetResolver resolver(direction, input);
  std::array<uint8_t, 128> buffer{};
  while (file.available()) {
    const int count = file.read(buffer.data(), buffer.size());
    if (count <= 0 || !resolver.feed(buffer.data(), static_cast<size_t>(count))) return false;
  }
  return resolver.finish(output);
}

}  // namespace

bool writeLiteral(HalFile& output, const char* text) {
  return output.write(reinterpret_cast<const uint8_t*>(text), strlen(text)) == strlen(text);
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

bool sanitizeChapter(const std::string& inputPath, const std::string& outputPath, const std::string& imageIndexPath,
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
    bool firstChunk = true;
    while (input.available()) {
      const int got = input.read(readBuffer, readBufferSize);
      if (got <= 0) return false;
      int i = 0;
      if (firstChunk && got >= 3 && readBuffer[0] == 0xEF && readBuffer[1] == 0xBB && readBuffer[2] == 0xBF) i = 3;
      firstChunk = false;
      for (; i < got;) {
        if (!sanitizer.inTag && !sanitizer.inEntity && sanitizer.skipDepth == 0 && !sanitizer.skipHead) {
          const size_t run =
              WeReadProtocol::safeXhtmlTextRunLength(readBuffer + i, static_cast<size_t>(got - i), plainText);
          if (run > 0) {
            // A logical run spans input reads so a marker can never split a UTF-8 codepoint.
            if ((!sanitizer.textRunOpen && !writeSourceOffsetMarker(sanitizer, sanitizer.sourceUtf16Offset)) ||
                output.write(readBuffer + i, run) != run || !advanceSourceUtf16Offset(sanitizer, readBuffer + i, run)) {
              return false;
            }
            sanitizer.textRunOpen = true;
            i += static_cast<int>(run);
            continue;
          }
        }
        sanitizer.textRunOpen = false;
        const uint8_t value = readBuffer[i++];
        const uint32_t sourceOffset = sanitizer.sourceUtf16Offset;
        if (!advanceSourceUtf16Offset(sanitizer, &value, 1)) return false;
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
          sanitizer.tagSourceOffset = sourceOffset;
          continue;
        }
        if (!emitSanitizedTextByte(sanitizer, value, sourceOffset)) return false;
      }
    }
    if (sanitizer.inEntity && !emitEntity(sanitizer)) return false;
    return writeSourceOffsetMarker(sanitizer, sanitizer.sourceUtf16Offset) &&
           (!plainText || writeLiteral(output, "</p>")) && writeLiteral(output, "</body></html>");
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

bool visibleToNativeOffset(const std::string& path, const uint32_t visibleOffset, uint32_t& nativeOffset) {
  return resolveSourceOffset(path, SourceOffsetDirection::VisibleToNative, visibleOffset, nativeOffset);
}

bool nativeToVisibleOffset(const std::string& path, const uint32_t nativeOffset, uint32_t& visibleOffset) {
  return resolveSourceOffset(path, SourceOffsetDirection::NativeToVisible, nativeOffset, visibleOffset);
}

}  // namespace WeReadXhtmlCodec

#endif
