#include "WeReadXhtmlCodec.h"

#if defined(ENABLE_CHINESE_VERSION) || defined(CROSSPOINT_EMULATED)

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "Epub/htmlEntities.h"
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
  return hasAttributeToken(tag, "class", "qqreader-footnote") || hasAttributeToken(tag, "class", "duokan-footnote") ||
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
  HalFile* input = nullptr;
  HalFile* output = nullptr;
  HalFile* notes = nullptr;
  WeReadStore::IndexWriter* imageWriter = nullptr;
  char* tag = nullptr;
  size_t tagCapacity = 0;
  uint32_t chapterIndex = 0;
  const char* title = nullptr;
  uint32_t noteCount = 0;
  bool plainText = false;
  bool inTag = false;
  bool inEntity = false;
  bool skipHead = false;
  bool tagOverflow = false;
  bool textRunOpen = false;
  char tagQuote = '\0';
  char tagFirstChar = '\0';
  char skippedElement[24] = {};
  size_t skipDepth = 0;
  size_t tagLen = 0;
  char entity[24] = {};
  size_t entityLen = 0;
  uint32_t sourceUtf16Offset = 0;
  uint32_t tagSourceOffset = 0;
  uint32_t entitySourceOffset = 0;
  uint64_t tagByteBegin = 0;
  uint64_t tagByteEnd = 0;
  uint64_t resumeByteOffset = 0;
  bool (*continueWork)(void*) = nullptr;
  void* workContext = nullptr;
};

bool writeSourceOffsetMarker(XhtmlSanitizer& sanitizer, const uint32_t offset) {
  char marker[32];
  const int length =
      snprintf(marker, sizeof(marker), "<!--%s%u-->", kSourceOffsetMarkerName, static_cast<unsigned>(offset));
  return length > 0 && static_cast<size_t>(length) < sizeof(marker) && writeLiteral(*sanitizer.output, marker);
}

// Borrows the discarded tag workspace; the main input chunk stays intact during SD look-back.
class SourceRangeReader {
 public:
  SourceRangeReader(HalFile& input, char* buffer, const size_t capacity, const uint64_t end,
                    bool (*continueWork)(void*), void* workContext)
      : input_(input),
        buffer_(buffer),
        capacity_(capacity),
        end_(end),
        continueWork_(continueWork),
        workContext_(workContext) {}

  bool seek(const uint64_t offset) {
    if (offset > end_ || !input_.seek64(offset)) return false;
    offset_ = offset;
    index_ = 0;
    length_ = 0;
    return true;
  }

  int next() {
    if (offset_ == end_) return -1;
    if (index_ == length_) {
      if (continueWork_ && !continueWork_(workContext_)) {
        valid_ = false;
        return -1;
      }
      const size_t wanted = static_cast<size_t>(std::min<uint64_t>(capacity_, end_ - offset_));
      const int got = input_.read(buffer_, wanted);
      if (got <= 0 || static_cast<size_t>(got) > wanted) {
        valid_ = false;
        return -1;
      }
      index_ = 0;
      length_ = static_cast<size_t>(got);
    }
    ++offset_;
    return static_cast<unsigned char>(buffer_[index_++]);
  }

  uint64_t position() const { return offset_; }
  bool valid() const { return valid_; }

  bool writeTextRun(HalFile& output) {
    const size_t begin = index_ - 1;
    while (index_ < length_) {
      const uint8_t value = static_cast<uint8_t>(buffer_[index_]);
      if (value == '&' || value == '<' || value == '>' || value == '\'' || value == '"' ||
          (value < 0x20 && value != '\t' && value != '\n' && value != '\r')) {
        break;
      }
      ++index_;
      ++offset_;
    }
    return output.write(reinterpret_cast<const uint8_t*>(buffer_ + begin), index_ - begin) == index_ - begin;
  }

 private:
  HalFile& input_;
  char* buffer_;
  size_t capacity_;
  uint64_t end_;
  uint64_t offset_ = 0;
  size_t index_ = 0;
  size_t length_ = 0;
  bool valid_ = true;
  bool (*continueWork_)(void*);
  void* workContext_;
};

struct AnnotationRange {
  bool annotation = false;
  uint64_t begin = 0;
  uint64_t end = 0;
};

bool readAnnotationRange(SourceRangeReader& reader, AnnotationRange& result) {
  int value = reader.next();
  while (value >= 0 && std::isspace(value)) value = reader.next();
  char name[24] = {};
  size_t length = 0;
  while (value >= 0 && !std::isspace(value) && value != '/' && value != '>') {
    if (length + 1 < sizeof(name)) name[length] = static_cast<char>(value);
    length = std::min(length + 1, sizeof(name));
    value = reader.next();
  }
  if (!equalsIgnoreCase(name, length, "img")) return reader.valid();

  while (value >= 0) {
    while (value >= 0 && (std::isspace(value) || value == '/')) value = reader.next();
    if (value < 0) break;
    length = 0;
    memset(name, 0, sizeof(name));
    while (value >= 0 && !std::isspace(value) && value != '=' && value != '/') {
      if (length + 1 < sizeof(name)) name[length] = static_cast<char>(value);
      length = std::min(length + 1, sizeof(name));
      value = reader.next();
    }
    const bool classAttribute = equalsIgnoreCase(name, length, "class");
    const bool altAttribute = equalsIgnoreCase(name, length, "alt");
    while (value >= 0 && std::isspace(value)) value = reader.next();
    if (value != '=') continue;
    value = reader.next();
    while (value >= 0 && std::isspace(value)) value = reader.next();
    const int quote = value == '\'' || value == '"' ? value : 0;
    const uint64_t begin = reader.position() - (quote || value < 0 ? 0 : 1);
    if (quote) value = reader.next();
    static constexpr char kClass[] = "qqreader-footnote";
    size_t tokenLength = 0;
    bool tokenMatches = true;
    while (value >= 0 && (quote ? value != quote : !std::isspace(value))) {
      if (classAttribute) {
        if (std::isspace(value)) {
          result.annotation |= tokenMatches && tokenLength == sizeof(kClass) - 1;
          tokenLength = 0;
          tokenMatches = true;
        } else {
          tokenMatches &= tokenLength < sizeof(kClass) - 1 &&
                          std::tolower(value) == kClass[std::min(tokenLength, sizeof(kClass) - 1)];
          tokenLength = std::min(tokenLength + 1, sizeof(kClass));
        }
      }
      value = reader.next();
    }
    if (classAttribute) result.annotation |= tokenMatches && tokenLength == sizeof(kClass) - 1;
    if (quote && value != quote) return false;
    if (altAttribute) {
      result.begin = begin;
      result.end = reader.position() - (value < 0 ? 0 : 1);
    }
    if (quote) value = reader.next();
  }
  return reader.valid();
}

bool writeXmlByte(HalFile& output, const uint8_t value) {
  const char* escaped = nullptr;
  switch (value) {
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
  }
  return escaped ? writeLiteral(output, escaped) : output.write(value) == 1;
}

bool writeXmlCodepoint(HalFile& output, const uint32_t value) {
  char encoded[5] = {};
  if (value <= 0x7F) {
    encoded[0] = static_cast<char>(value);
  } else if (value <= 0x7FF) {
    encoded[0] = static_cast<char>(0xC0 | (value >> 6));
    encoded[1] = static_cast<char>(0x80 | (value & 0x3F));
  } else if (value <= 0xFFFF) {
    encoded[0] = static_cast<char>(0xE0 | (value >> 12));
    encoded[1] = static_cast<char>(0x80 | ((value >> 6) & 0x3F));
    encoded[2] = static_cast<char>(0x80 | (value & 0x3F));
  } else {
    encoded[0] = static_cast<char>(0xF0 | (value >> 18));
    encoded[1] = static_cast<char>(0x80 | ((value >> 12) & 0x3F));
    encoded[2] = static_cast<char>(0x80 | ((value >> 6) & 0x3F));
    encoded[3] = static_cast<char>(0x80 | (value & 0x3F));
  }
  return writeXmlText(output, encoded);
}

bool writeAnnotationText(SourceRangeReader& reader, HalFile& output) {
  for (int value = reader.next(); value >= 0; value = reader.next()) {
    if (value != '&') {
      if (value < 0x20 && value != '\t' && value != '\n' && value != '\r') return false;
      if (value == '<' || value == '>' || value == '\'' || value == '"') {
        if (!writeXmlByte(output, static_cast<uint8_t>(value))) return false;
      } else if (!reader.writeTextRun(output)) {
        return false;
      }
      continue;
    }
    const uint64_t afterAmpersand = reader.position();
    value = reader.next();
    if (value == '#') {
      value = reader.next();
      const uint32_t base = value == 'x' || value == 'X' ? 16U : 10U;
      if (base == 16) value = reader.next();
      uint32_t codepoint = 0;
      bool hasDigit = false;
      bool overflow = false;
      while (value >= 0) {
        const int digit = value >= '0' && value <= '9'   ? value - '0'
                          : value >= 'a' && value <= 'f' ? value - 'a' + 10
                          : value >= 'A' && value <= 'F' ? value - 'A' + 10
                                                         : -1;
        if (digit < 0 || static_cast<uint32_t>(digit) >= base) break;
        hasDigit = true;
        if (codepoint > (0x10FFFFU - static_cast<uint32_t>(digit)) / base) overflow = true;
        if (!overflow) codepoint = codepoint * base + static_cast<uint32_t>(digit);
        value = reader.next();
      }
      const bool validCodepoint =
          codepoint == 9 || codepoint == 10 || codepoint == 13 || (codepoint >= 0x20 && codepoint <= 0xD7FF) ||
          (codepoint >= 0xE000 && codepoint <= 0xFFFD) || (codepoint >= 0x10000 && codepoint <= 0x10FFFF);
      if (value == ';' && hasDigit && !overflow && validCodepoint) {
        if (!writeXmlCodepoint(output, codepoint)) return false;
        continue;
      }
    } else {
      char entity[24] = {'&'};
      size_t length = 1;
      while (value >= 0 && length < sizeof(entity) && !std::isspace(value) && value != '&' && value != '<') {
        entity[length++] = static_cast<char>(value);
        if (value == ';') break;
        value = reader.next();
      }
      const char* decoded = value == ';' ? lookupHtmlEntity(entity, length) : nullptr;
      if (decoded) {
        if (!writeXmlText(output, decoded)) return false;
        continue;
      }
    }
    // Replay unknown references as literal text without retaining an unbounded token.
    if (!reader.seek(afterAmpersand) || !writeLiteral(output, "&amp;")) return false;
  }
  return reader.valid();
}

bool writeAnnotation(XhtmlSanitizer& sanitizer) {
  SourceRangeReader reader(*sanitizer.input, sanitizer.tag, sanitizer.tagCapacity, sanitizer.tagByteEnd - 1,
                           sanitizer.continueWork, sanitizer.workContext);
  const bool written = [&]() {
    AnnotationRange range;
    if (!reader.seek(sanitizer.tagByteBegin + 1) || !readAnnotationRange(reader, range)) return false;
    if (!range.annotation || range.begin == range.end) return true;
    if (sanitizer.noteCount == UINT32_MAX) return false;
    if (sanitizer.noteCount == 0 && (!writeLiteral(*sanitizer.notes,
                                                   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                                                   "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
                                                   "xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>") ||
                                     !writeXmlText(*sanitizer.notes, sanitizer.title) ||
                                     !writeLiteral(*sanitizer.notes, "</title></head><body>"))) {
      return false;
    }
    ++sanitizer.noteCount;
    char markup[224];
    int length = snprintf(markup, sizeof(markup),
                          "<a epub:type=\"noteref\" role=\"doc-noteref\" id=\"wr-ref-%u\" "
                          "href=\"notes%06u.xhtml#wr-note-%u\">[%u]</a>",
                          static_cast<unsigned>(sanitizer.noteCount), static_cast<unsigned>(sanitizer.chapterIndex),
                          static_cast<unsigned>(sanitizer.noteCount), static_cast<unsigned>(sanitizer.noteCount));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(markup) ||
        !writeSourceOffsetMarker(sanitizer, sanitizer.tagSourceOffset) || !writeLiteral(*sanitizer.output, markup) ||
        !writeSourceOffsetMarker(sanitizer, sanitizer.sourceUtf16Offset)) {
      return false;
    }
    length = snprintf(markup, sizeof(markup),
                      "<aside epub:type=\"footnote\" role=\"doc-footnote\" id=\"wr-note-%u\"><p>"
                      "<a epub:type=\"backlink\" role=\"doc-backlink\" href=\"ch%06u.xhtml#wr-ref-%u\">[%u]</a> ",
                      static_cast<unsigned>(sanitizer.noteCount), static_cast<unsigned>(sanitizer.chapterIndex),
                      static_cast<unsigned>(sanitizer.noteCount), static_cast<unsigned>(sanitizer.noteCount));
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(markup) || !writeLiteral(*sanitizer.notes, markup)) {
      return false;
    }
    SourceRangeReader text(*sanitizer.input, sanitizer.tag, sanitizer.tagCapacity, range.end, sanitizer.continueWork,
                           sanitizer.workContext);
    return text.seek(range.begin) && writeAnnotationText(text, *sanitizer.notes) &&
           writeLiteral(*sanitizer.notes, "</p></aside>");
  }();
  const bool restored = sanitizer.input->seek64(sanitizer.resumeByteOffset);
  return written && restored;
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
    return sanitizer.skipDepth != 0 || sanitizer.skipHead || writeAnnotation(sanitizer);
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
  if (!closing && strcmp(name, "img") == 0 && !sanitizer.skipHead &&
      hasAttributeToken(sanitizer.tag, "class", "qqreader-footnote")) {
    return writeAnnotation(sanitizer);
  }
  if (!closing && isAnnotationTag(sanitizer.tag)) {
    if (selfClosing || strcmp(name, "img") == 0) return true;
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
    if (!writeXmlByte(output, *p)) return false;
  }
  return true;
}

bool sanitizeChapter(const std::string& inputPath, const std::string& outputPath, const std::string& imageIndexPath,
                     const std::string& notesPath, const uint32_t chapterIndex, const char* title, const bool plainText,
                     uint8_t* readBuffer, const size_t readBufferSize, char* tagBuffer, const size_t tagBufferSize,
                     bool (*continueWork)(void*), void* workContext) {
  HalFile input;
  if (!readBuffer || readBufferSize == 0 || !tagBuffer || tagBufferSize < sizeof(WeReadStore::ImageRecord::url) ||
      !Storage.openFileForRead("WR", inputPath, input)) {
    return false;
  }
  const std::string partPath = outputPath + ".part";
  const std::string notesPartPath = notesPath + ".part";
  if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
  if (Storage.exists(notesPartPath.c_str())) Storage.remove(notesPartPath.c_str());
  if (Storage.exists(imageIndexPath.c_str()) && !Storage.remove(imageIndexPath.c_str())) return false;
  HalFile output;
  HalFile notes;
  WeReadStore::IndexWriter imageWriter;

  const bool written = [&]() {
    if (!Storage.openFileForWrite("WR", partPath, output) || !Storage.openFileForWrite("WR", notesPartPath, notes) ||
        !imageWriter.begin(imageIndexPath, WeReadStore::kImageMagic, sizeof(WeReadStore::ImageRecord))) {
      return false;
    }
    if (!writeLiteral(output,
                      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                      "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
                      "xmlns:epub=\"http://www.idpf.org/2007/ops\"><head><title>") ||
        !writeXmlText(output, title) || !writeLiteral(output, "</title></head><body>") ||
        (plainText && !writeLiteral(output, "<p>"))) {
      return false;
    }

    XhtmlSanitizer sanitizer;
    sanitizer.input = &input;
    sanitizer.output = &output;
    sanitizer.notes = &notes;
    sanitizer.imageWriter = &imageWriter;
    sanitizer.tag = tagBuffer;
    sanitizer.tagCapacity = tagBufferSize;
    sanitizer.chapterIndex = chapterIndex;
    sanitizer.title = title;
    sanitizer.plainText = plainText;
    sanitizer.continueWork = continueWork;
    sanitizer.workContext = workContext;
    bool firstChunk = true;
    uint64_t chunkStart = 0;
    while (input.available()) {
      if (continueWork && !continueWork(workContext)) return false;
      const int got = input.read(readBuffer, readBufferSize);
      if (got <= 0) return false;
      sanitizer.resumeByteOffset = chunkStart + static_cast<uint64_t>(got);
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
          const bool declaration = sanitizer.tagFirstChar == '!' || sanitizer.tagFirstChar == '?';
          if (!sanitizer.tagFirstChar && !std::isspace(value)) sanitizer.tagFirstChar = static_cast<char>(value);
          if (!declaration && sanitizer.tagQuote) {
            if (value == sanitizer.tagQuote) sanitizer.tagQuote = '\0';
          } else if (!declaration && (value == '\'' || value == '"')) {
            sanitizer.tagQuote = static_cast<char>(value);
          } else if (value == '>') {
            sanitizer.tagByteEnd = chunkStart + static_cast<uint64_t>(i);
            if (!processTag(sanitizer)) return false;
            continue;
          }
          if (sanitizer.tagLen + 1 < sanitizer.tagCapacity) {
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
          sanitizer.tagQuote = '\0';
          sanitizer.tagFirstChar = '\0';
          sanitizer.tagByteBegin = chunkStart + static_cast<uint64_t>(i) - 1;
          sanitizer.tagSourceOffset = sourceOffset;
          continue;
        }
        if (!emitSanitizedTextByte(sanitizer, value, sourceOffset)) return false;
      }
      chunkStart = sanitizer.resumeByteOffset;
    }
    if (sanitizer.inTag) return false;
    if (sanitizer.inEntity && !emitEntity(sanitizer)) return false;
    return writeSourceOffsetMarker(sanitizer, sanitizer.sourceUtf16Offset) &&
           (!plainText || writeLiteral(output, "</p>")) && writeLiteral(output, "</body></html>") &&
           (sanitizer.noteCount == 0 || writeLiteral(notes, "</body></html>"));
  }();
  if (!written) {
    imageWriter.abort();
    output.close();
    notes.close();
    Storage.remove(partPath.c_str());
    Storage.remove(notesPartPath.c_str());
    return false;
  }
  output.flush();
  output.close();
  notes.flush();
  notes.close();
  if (!WeReadStore::atomicReplace(notesPartPath, notesPath) || !WeReadStore::atomicReplace(partPath, outputPath) ||
      !imageWriter.finish()) {
    imageWriter.abort();
    Storage.remove(partPath.c_str());
    Storage.remove(notesPartPath.c_str());
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
