#include "WeReadProtocol.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace WeReadProtocol {
namespace {

bool append(char* out, size_t outSize, size_t& pos, const char* value, size_t len) {
  if (pos + len >= outSize) return false;
  memcpy(out + pos, value, len);
  pos += len;
  out[pos] = '\0';
  return true;
}

bool appendChar(char* out, size_t outSize, size_t& pos, char value) { return append(out, outSize, pos, &value, 1); }

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool appendUtf8(uint32_t cp, char* out, size_t outSize, size_t& pos) {
  char bytes[4];
  size_t count = 0;
  if (cp <= 0x7F) {
    bytes[count++] = static_cast<char>(cp);
  } else if (cp <= 0x7FF) {
    bytes[count++] = static_cast<char>(0xC0 | (cp >> 6));
    bytes[count++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    bytes[count++] = static_cast<char>(0xE0 | (cp >> 12));
    bytes[count++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    bytes[count++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0x10FFFF) {
    bytes[count++] = static_cast<char>(0xF0 | (cp >> 18));
    bytes[count++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    bytes[count++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    bytes[count++] = static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    return false;
  }
  return append(out, outSize, pos, bytes, count);
}

bool parseHex4(const char* value, uint32_t& cp) {
  cp = 0;
  for (int i = 0; i < 4; ++i) {
    const int digit = hexValue(value[i]);
    if (digit < 0) return false;
    cp = (cp << 4) | static_cast<uint32_t>(digit);
  }
  return true;
}

bool validCookieName(const char* name, const size_t len) {
  if (!name || len < 4 || memcmp(name, "wr_", 3) != 0) return false;
  for (size_t i = 0; i < len; ++i) {
    const auto value = static_cast<uint8_t>(name[i]);
    if (!std::isalnum(value) && value != '_' && value != '-') return false;
  }
  return true;
}

bool validCookieValue(const char* value, const size_t len) {
  if (!value && len != 0) return false;
  for (size_t i = 0; i < len; ++i) {
    const auto byte = static_cast<uint8_t>(value[i]);
    if (byte < 0x21 || byte > 0x7E || byte == ';') return false;
  }
  return true;
}

uint8_t base64Value(uint8_t c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-' || c == '+') return 62;
  if (c == '_' || c == '/') return 63;
  return 0xFF;
}

size_t decimalDigits(size_t value) {
  size_t digits = 1;
  while (value >= 10) {
    value /= 10;
    ++digits;
  }
  return digits;
}

uint32_t parseDecimal(const char* value, size_t len) {
  uint32_t result = 0;
  for (size_t i = 0; i < len; ++i) {
    if (value[i] < '0' || value[i] > '9') return 0;
    result = result * 10 + static_cast<uint32_t>(value[i] - '0');
  }
  return result;
}

}  // namespace

ChapterResponse classifyChapterResponse(const int status, const bool emptyObject) {
  if (status == 401) return ChapterResponse::AuthenticationRequired;
  if (status == 403 || (status == 200 && emptyObject)) return ChapterResponse::Retryable;
  return status == 200 ? ChapterResponse::Content : ChapterResponse::Error;
}

bool isEmptyJsonObject(const uint8_t* data, const size_t len) {
  if (!data) return false;
  uint8_t state = 0;
  for (size_t i = 0; i < len; ++i) {
    if (std::isspace(data[i])) continue;
    if (state == 0 && data[i] == '{') {
      state = 1;
    } else if (state == 1 && data[i] == '}') {
      state = 2;
    } else {
      return false;
    }
  }
  return state == 2;
}

bool mergeRuntimeCookie(char* header, const size_t headerSize, const char* name, const size_t nameLen,
                        const char* value, const size_t valueLen) {
  if (!header || headerSize == 0 || nameLen >= headerSize || valueLen >= headerSize ||
      !validCookieName(name, nameLen) || !validCookieValue(value, valueLen)) {
    return false;
  }
  const size_t headerLen = strnlen(header, headerSize);
  if (headerLen == headerSize) return false;

  size_t entryStart = 0;
  while (entryStart < headerLen) {
    const char* equals = static_cast<const char*>(memchr(header + entryStart, '=', headerLen - entryStart));
    if (!equals) return false;
    const size_t entryNameLen = static_cast<size_t>(equals - header - entryStart);
    const char* separator = strstr(equals + 1, "; ");
    const size_t entryEnd = separator ? static_cast<size_t>(separator - header) : headerLen;
    if (entryNameLen == nameLen && memcmp(header + entryStart, name, nameLen) == 0) {
      const size_t oldValueStart = static_cast<size_t>(equals - header) + 1;
      const size_t oldValueLen = entryEnd - oldValueStart;
      if (valueLen == 0) {
        const size_t removeStart = entryStart == 0 ? 0 : entryStart - 2;
        const size_t removeEnd = entryStart == 0 && separator ? entryEnd + 2 : entryEnd;
        memmove(header + removeStart, header + removeEnd, headerLen - removeEnd + 1);
        return true;
      }
      const size_t baseLength = headerLen - oldValueLen;
      if (valueLen >= headerSize - baseLength) return false;
      memmove(header + oldValueStart + valueLen, header + entryEnd, headerLen - entryEnd + 1);
      memcpy(header + oldValueStart, value, valueLen);
      return true;
    }
    entryStart = entryEnd + (separator ? 2 : 0);
  }

  if (valueLen == 0) return true;
  const size_t separatorLen = headerLen == 0 ? 0 : 2;
  const size_t available = headerSize - headerLen - 1;
  const size_t prefixLength = separatorLen + nameLen + 1;
  if (prefixLength > available || valueLen > available - prefixLength) return false;
  size_t pos = headerLen;
  if (separatorLen != 0) {
    header[pos++] = ';';
    header[pos++] = ' ';
  }
  memcpy(header + pos, name, nameLen);
  pos += nameLen;
  header[pos++] = '=';
  memcpy(header + pos, value, valueLen);
  header[pos + valueLen] = '\0';
  return true;
}

bool isAllowedXhtmlTag(const char* name) {
  if (!name) return false;
  static constexpr const char* kAllowed[] = {"p",  "div", "section",    "article", "h1",   "h2", "h3",     "h4",
                                             "h5", "h6",  "blockquote", "ul",      "ol",   "li", "strong", "b",
                                             "em", "i",   "br",         "hr",      "span", "img"};
  return std::any_of(std::begin(kAllowed), std::end(kAllowed),
                     [name](const char* allowed) { return strcmp(name, allowed) == 0; });
}

bool extractImageAttributes(const char* tag, char* source, const size_t sourceSize, char* alt, const size_t altSize) {
  if (!tag || !source || sourceSize < 2 || !alt || altSize == 0) return false;
  source[0] = '\0';
  alt[0] = '\0';
  const char* cursor = tag;
  while (std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
  if (*cursor == '/') return false;
  const char* name = cursor;
  while (std::isalnum(static_cast<unsigned char>(*cursor))) ++cursor;
  if (cursor - name != 3 || std::tolower(static_cast<unsigned char>(name[0])) != 'i' ||
      std::tolower(static_cast<unsigned char>(name[1])) != 'm' ||
      std::tolower(static_cast<unsigned char>(name[2])) != 'g') {
    return false;
  }

  while (*cursor) {
    while (std::isspace(static_cast<unsigned char>(*cursor)) || *cursor == '/') ++cursor;
    if (!*cursor) break;
    const char* attribute = cursor;
    while (*cursor && !std::isspace(static_cast<unsigned char>(*cursor)) && *cursor != '=' && *cursor != '/') {
      ++cursor;
    }
    const size_t nameLen = static_cast<size_t>(cursor - attribute);
    const bool sourceAttribute = nameLen == 3 && std::tolower(static_cast<unsigned char>(attribute[0])) == 's' &&
                                 std::tolower(static_cast<unsigned char>(attribute[1])) == 'r' &&
                                 std::tolower(static_cast<unsigned char>(attribute[2])) == 'c';
    const bool altAttribute = nameLen == 3 && std::tolower(static_cast<unsigned char>(attribute[0])) == 'a' &&
                              std::tolower(static_cast<unsigned char>(attribute[1])) == 'l' &&
                              std::tolower(static_cast<unsigned char>(attribute[2])) == 't';
    while (std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
    if (*cursor != '=') {
      while (*cursor && !std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
      continue;
    }
    ++cursor;
    while (std::isspace(static_cast<unsigned char>(*cursor))) ++cursor;
    if (*cursor != '"' && *cursor != '\'') {
      if (sourceAttribute || altAttribute) return false;
      while (*cursor && !std::isspace(static_cast<unsigned char>(*cursor)) && *cursor != '/') ++cursor;
      continue;
    }
    const char quote = *cursor++;
    const char* value = cursor;
    while (*cursor && *cursor != quote) ++cursor;
    if (*cursor != quote) return false;
    const size_t valueLen = static_cast<size_t>(cursor - value);
    ++cursor;

    char* destination = nullptr;
    size_t capacity = 0;
    if (sourceAttribute) {
      destination = source;
      capacity = sourceSize;
    } else if (altAttribute) {
      destination = alt;
      capacity = altSize;
    }
    if (destination) {
      if (valueLen >= capacity) {
        if (sourceAttribute) return false;
        alt[0] = '\0';
        continue;
      }
      memcpy(destination, value, valueLen);
      destination[valueLen] = '\0';
    }
  }
  return source[0] != '\0';
}

ImageType normalizeImageUrl(const char* source, char* output, const size_t outputSize) {
  if (!source || !output || outputSize < 10) return ImageType::None;
  size_t written = 0;
  if (source[0] == '/' && source[1] == '/') {
    static constexpr char kScheme[] = "https:";
    if (sizeof(kScheme) - 1 >= outputSize) return ImageType::None;
    memcpy(output, kScheme, sizeof(kScheme) - 1);
    written = sizeof(kScheme) - 1;
  } else {
    static constexpr char kScheme[] = "https://";
    for (size_t i = 0; i < sizeof(kScheme) - 1; ++i) {
      if (!source[i] || std::tolower(static_cast<unsigned char>(source[i])) != kScheme[i]) return ImageType::None;
      output[written++] = kScheme[i];
    }
    source += sizeof(kScheme) - 1;
  }

  while (*source && *source != '#') {
    if (written + 1 >= outputSize) return ImageType::None;
    const unsigned char value = static_cast<unsigned char>(*source);
    if (value <= 0x20 || value == 0x7F || value == '"' || value == '\'' || value == '<' || value == '>' ||
        value == '\\') {
      return ImageType::None;
    }
    if (strncmp(source, "&amp;", 5) == 0) {
      output[written++] = '&';
      source += 5;
      continue;
    }
    output[written++] = *source++;
  }
  output[written] = '\0';

  const char* host = output + 8;
  const char* path = strchr(host, '/');
  if (!path || path == host || host[0] == '.' || path[-1] == '.') return ImageType::None;
  bool previousDot = false;
  for (const char* cursor = host; cursor < path; ++cursor) {
    const unsigned char value = static_cast<unsigned char>(*cursor);
    if (!std::isalnum(value) && value != '.' && value != '-') return ImageType::None;
    if (value == '.' && previousDot) return ImageType::None;
    previousDot = value == '.';
  }
  const char* pathEnd = strchr(path, '?');
  if (!pathEnd) pathEnd = output + written;
  const char* extension = pathEnd;
  while (extension > path && extension[-1] != '.') --extension;
  if (extension == path || extension[-1] != '.') return ImageType::None;
  const size_t extensionLen = static_cast<size_t>(pathEnd - extension);
  const auto extensionIs = [extension, extensionLen](const char* expected) {
    if (strlen(expected) != extensionLen) return false;
    for (size_t i = 0; i < extensionLen; ++i) {
      if (std::tolower(static_cast<unsigned char>(extension[i])) != expected[i]) return false;
    }
    return true;
  };
  if (extensionIs("jpg") || extensionIs("jpeg")) return ImageType::Jpeg;
  return extensionIs("png") ? ImageType::Png : ImageType::None;
}

bool PsvtsExtractor::reset() {
  keyOffset_ = 0;
  valueLength_ = 0;
  state_ = State::SearchKey;
  if (!out_ || outSize_ < 2) return false;
  out_[0] = '\0';
  return true;
}

void PsvtsExtractor::resumeSearch(const uint8_t value) {
  static constexpr char kKey[] = "\"psvts\"";
  state_ = State::SearchKey;
  keyOffset_ = value == static_cast<uint8_t>(kKey[0]) ? 1 : 0;
}

bool PsvtsExtractor::feed(const uint8_t* data, const size_t len) {
  if ((!data && len != 0) || !out_ || outSize_ < 2) return false;
  static constexpr char kKey[] = "\"psvts\"";
  for (size_t i = 0; i < len; ++i) {
    const uint8_t value = data[i];
    switch (state_) {
      case State::SearchKey:
        if (value == static_cast<uint8_t>(kKey[keyOffset_])) {
          if (++keyOffset_ == sizeof(kKey) - 1) state_ = State::ExpectColon;
        } else {
          resumeSearch(value);
        }
        break;
      case State::ExpectColon:
        if (std::isspace(value)) break;
        if (value == ':') {
          state_ = State::ExpectQuote;
        } else {
          resumeSearch(value);
        }
        break;
      case State::ExpectQuote:
        if (std::isspace(value)) break;
        if (value == '"') {
          valueLength_ = 0;
          state_ = State::ReadValue;
        } else {
          resumeSearch(value);
        }
        break;
      case State::ReadValue:
        if (value == '"') {
          if (valueLength_ == 0) {
            state_ = State::Invalid;
          } else {
            out_[valueLength_] = '\0';
            state_ = State::Complete;
          }
        } else if ((std::isalnum(value) || value == '-' || value == '_') && valueLength_ + 1 < outSize_) {
          out_[valueLength_++] = static_cast<char>(value);
        } else {
          out_[0] = '\0';
          state_ = State::Invalid;
        }
        break;
      case State::Complete:
      case State::Invalid:
        break;
    }
  }
  return true;
}

bool XhtmlTagProbe::reset() {
  nameLength_ = 0;
  state_ = State::SearchOpen;
  name_[0] = '\0';
  return true;
}

bool XhtmlTagProbe::feed(const uint8_t* data, const size_t len) {
  if (!data && len != 0) return false;
  for (size_t i = 0; i < len && state_ != State::Complete; ++i) {
    const uint8_t value = data[i];
    switch (state_) {
      case State::SearchOpen:
        if (value == '<') {
          nameLength_ = 0;
          state_ = State::ReadName;
        }
        break;
      case State::ReadName:
        if (std::isalnum(value)) {
          if (nameLength_ + 1 >= sizeof(name_)) {
            state_ = State::SkipTag;
          } else {
            name_[nameLength_++] = static_cast<char>(std::tolower(value));
          }
        } else if (nameLength_ != 0 && (std::isspace(value) || value == '/' || value == '>')) {
          name_[nameLength_] = '\0';
          state_ = isAllowedXhtmlTag(name_) ? State::Complete : (value == '>' ? State::SearchOpen : State::SkipTag);
        } else {
          state_ = value == '<' ? State::ReadName : State::SkipTag;
          nameLength_ = 0;
        }
        break;
      case State::SkipTag:
        if (value == '>') state_ = State::SearchOpen;
        break;
      case State::Complete:
        break;
    }
  }
  return true;
}

bool encodeId(const char* value, Md5Function md5, char* out, size_t outSize) {
  if (!value || !value[0] || !md5 || !out || outSize < 24) return false;

  const size_t valueLen = strlen(value);
  char firstHash[33] = {};
  if (!md5(reinterpret_cast<const uint8_t*>(value), valueLen, firstHash)) return false;

  size_t pos = 0;
  out[0] = '\0';
  if (!append(out, outSize, pos, firstHash, 3)) return false;

  bool numeric = true;
  for (size_t i = 0; i < valueLen; ++i) {
    if (value[i] < '0' || value[i] > '9') {
      numeric = false;
      break;
    }
  }
  if (!appendChar(out, outSize, pos, numeric ? '3' : '4') || !appendChar(out, outSize, pos, '2') ||
      !append(out, outSize, pos, firstHash + 30, 2)) {
    return false;
  }

  if (numeric) {
    for (size_t offset = 0; offset < valueLen; offset += 9) {
      if (offset != 0 && !appendChar(out, outSize, pos, 'g')) return false;
      const size_t count = std::min<size_t>(9, valueLen - offset);
      uint32_t number = 0;
      for (size_t i = 0; i < count; ++i) number = number * 10 + static_cast<uint32_t>(value[offset + i] - '0');
      char chunk[16];
      const int chunkLen = snprintf(chunk, sizeof(chunk), "%x", static_cast<unsigned>(number));
      char lengthHex[3];
      snprintf(lengthHex, sizeof(lengthHex), "%02x", chunkLen);
      if (!append(out, outSize, pos, lengthHex, 2) ||
          !append(out, outSize, pos, chunk, static_cast<size_t>(chunkLen))) {
        return false;
      }
    }
  } else {
    char encoded[260];
    size_t encodedLen = 0;
    encoded[0] = '\0';
    for (size_t i = 0; i < valueLen; ++i) {
      char byteHex[3];
      const int count = snprintf(byteHex, sizeof(byteHex), "%x", static_cast<unsigned char>(value[i]));
      if (!append(encoded, sizeof(encoded), encodedLen, byteHex, static_cast<size_t>(count))) return false;
    }
    char lengthHex[3];
    snprintf(lengthHex, sizeof(lengthHex), "%02x", static_cast<unsigned>(encodedLen));
    if (!append(out, outSize, pos, lengthHex, 2) || !append(out, outSize, pos, encoded, encodedLen)) return false;
  }

  while (pos < 20) {
    const size_t count = std::min<size_t>(20 - pos, 32);
    if (!append(out, outSize, pos, firstHash, count)) return false;
  }

  char finalHash[33] = {};
  if (!md5(reinterpret_cast<const uint8_t*>(out), pos, finalHash)) return false;
  return append(out, outSize, pos, finalHash, 3);
}

bool matchesMd5(const char* expected, const size_t expectedLen, const char* actual, const size_t actualLen) {
  if (!expected || !actual || expectedLen != 32 || actualLen != 32) return false;
  for (size_t i = 0; i < 32; ++i) {
    const int expectedNibble = hexValue(expected[i]);
    if (expectedNibble < 0 || expectedNibble != hexValue(actual[i])) return false;
  }
  return true;
}

bool signQuery(const char* query, char* out, size_t outSize) {
  if (!query || !out || outSize < 9) return false;
  const size_t length = strlen(query);
  int64_t a = 0x15051505;
  int64_t b = a;
  size_t i = length;
  while (i > 1) {
    const auto current = static_cast<uint8_t>(query[i - 1]);
    const auto previous = static_cast<uint8_t>(query[i - 2]);
    a = (a ^ (static_cast<int64_t>(current) << ((length - i + 1) % 30))) & 0x7fffffff;
    b = (b ^ (static_cast<int64_t>(previous) << ((i - 1) % 30))) & 0x7fffffff;
    i -= 2;
  }
  return snprintf(out, outSize, "%llx", static_cast<unsigned long long>(a + b)) > 0;
}

bool urlEncode(const char* value, char* out, size_t outSize) {
  if (!value || !out || outSize == 0) return false;
  static constexpr char kHex[] = "0123456789ABCDEF";
  size_t pos = 0;
  out[0] = '\0';
  for (const auto* p = reinterpret_cast<const uint8_t*>(value); *p; ++p) {
    const uint8_t c = *p;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      if (!appendChar(out, outSize, pos, static_cast<char>(c))) return false;
    } else {
      const char encoded[] = {'%', kHex[c >> 4], kHex[c & 0x0F]};
      if (!append(out, outSize, pos, encoded, sizeof(encoded))) return false;
    }
  }
  return true;
}

size_t decodeJsonString(const char* value, size_t len, char* out, size_t outSize) {
  if (!out || outSize == 0) return 0;
  out[0] = '\0';
  if (!value) return 0;

  size_t pos = 0;
  for (size_t i = 0; i < len;) {
    if (i + 5 < len && value[i] == '\\' && value[i + 1] == 'u') {
      uint32_t cp = 0;
      if (parseHex4(value + i + 2, cp)) {
        i += 6;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 5 < len && value[i] == '\\' && value[i + 1] == 'u') {
          uint32_t low = 0;
          if (parseHex4(value + i + 2, low) && low >= 0xDC00 && low <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            i += 6;
          } else {
            cp = 0xFFFD;
          }
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
          cp = 0xFFFD;
        }
        if (!appendUtf8(cp, out, outSize, pos)) break;
        continue;
      }
    }
    if (!appendChar(out, outSize, pos, value[i])) break;
    ++i;
  }
  return pos;
}

void JsonStringDecoder::reset() {
  pendingLen_ = 0;
  failed_ = false;
}

bool JsonStringDecoder::feed(const char* data, const size_t len) {
  if (failed_ || (!data && len != 0)) return false;
  for (size_t i = 0; i < len; ++i) {
    if (pendingLen_ == sizeof(pending_)) {
      failed_ = true;
      return false;
    }
    pending_[pendingLen_++] = static_cast<uint8_t>(data[i]);
    if (!drain(false)) return false;
  }
  return true;
}

bool JsonStringDecoder::finish() {
  if (failed_ || !drain(true)) return false;
  return pendingLen_ == 0;
}

bool JsonStringDecoder::drain(const bool final) {
  while (pendingLen_ > 0) {
    if (pending_[0] == '\\') {
      if (pendingLen_ < 2) {
        if (!final) return true;
        if (!emit(pending_, 1)) return false;
        consume(1);
        continue;
      }
      if (pending_[1] != 'u') {
        if (!emit(pending_, 1)) return false;
        consume(1);
        continue;
      }
      if (pendingLen_ < 6) {
        if (!final) return true;
        if (!emit(pending_, 1)) return false;
        consume(1);
        continue;
      }
      uint32_t codepoint = 0;
      if (!parseHex4(reinterpret_cast<const char*>(pending_ + 2), codepoint)) {
        if (!emit(pending_, 1)) return false;
        consume(1);
        continue;
      }
      size_t consumed = 6;
      if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
        if (pendingLen_ < 12) {
          if (!final) return true;
          codepoint = 0xFFFD;
        } else {
          uint32_t low = 0;
          if (pending_[6] == '\\' && pending_[7] == 'u' &&
              parseHex4(reinterpret_cast<const char*>(pending_ + 8), low) && low >= 0xDC00 && low <= 0xDFFF) {
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + low - 0xDC00;
            consumed = 12;
          } else {
            codepoint = 0xFFFD;
          }
        }
      } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
        codepoint = 0xFFFD;
      }
      if (!emitCodepoint(codepoint)) return false;
      consume(consumed);
      continue;
    }

    size_t codepointLen = 1;
    const uint8_t lead = pending_[0];
    if ((lead & 0xE0) == 0xC0) {
      codepointLen = 2;
    } else if ((lead & 0xF0) == 0xE0) {
      codepointLen = 3;
    } else if ((lead & 0xF8) == 0xF0) {
      codepointLen = 4;
    } else if (lead >= 0x80) {
      if (!emitCodepoint(0xFFFD)) return false;
      consume(1);
      continue;
    }
    if (pendingLen_ < codepointLen) {
      if (!final) return true;
      if (!emitCodepoint(0xFFFD)) return false;
      consume(pendingLen_);
      continue;
    }
    bool valid = true;
    for (size_t i = 1; i < codepointLen; ++i) valid &= (pending_[i] & 0xC0) == 0x80;
    if (!valid) {
      if (!emitCodepoint(0xFFFD)) return false;
      consume(1);
      continue;
    }
    if (!emit(pending_, codepointLen)) return false;
    consume(codepointLen);
  }
  return true;
}

bool JsonStringDecoder::emit(const uint8_t* data, const size_t len) {
  if (!sink_ || !sink_(ctx_, data, len)) {
    failed_ = true;
    return false;
  }
  return true;
}

bool JsonStringDecoder::emitCodepoint(const uint32_t codepoint) {
  uint8_t bytes[4];
  size_t len = 0;
  if (codepoint <= 0x7F) {
    bytes[len++] = static_cast<uint8_t>(codepoint);
  } else if (codepoint <= 0x7FF) {
    bytes[len++] = static_cast<uint8_t>(0xC0 | (codepoint >> 6));
    bytes[len++] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    bytes[len++] = static_cast<uint8_t>(0xE0 | (codepoint >> 12));
    bytes[len++] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
    bytes[len++] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  } else {
    bytes[len++] = static_cast<uint8_t>(0xF0 | (codepoint >> 18));
    bytes[len++] = static_cast<uint8_t>(0x80 | ((codepoint >> 12) & 0x3F));
    bytes[len++] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
    bytes[len++] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  }
  return emit(bytes, len);
}

void JsonStringDecoder::consume(const size_t len) {
  if (len < pendingLen_) memmove(pending_, pending_ + len, pendingLen_ - len);
  pendingLen_ -= static_cast<uint8_t>(len);
}

size_t swapPositions(const size_t encodedLength, const uint8_t* tail, const size_t tailLen, uint32_t out[10]) {
  if (!out || encodedLength < 4) return 0;
  if (encodedLength < 11) {
    out[0] = 0;
    out[1] = 2;
    return 2;
  }

  const size_t expectedTail = std::min<size_t>(4, (encodedLength + 9) / 10);
  if (!tail || tailLen != expectedTail) return 0;

  char decimal[64] = {};
  size_t decimalLen = 0;
  for (size_t i = tailLen; i > 0; --i) {
    const uint8_t value = tail[i - 1];
    uint32_t transformed = 0;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((value >> bit) & 1U) transformed += 1U << (2U * bit);
    }
    const int written = snprintf(decimal + decimalLen, sizeof(decimal) - decimalLen, "%u", transformed);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(decimal) - decimalLen) return 0;
    decimalLen += static_cast<size_t>(written);
  }

  const size_t modulus = encodedLength - expectedTail - 2;
  if (modulus == 0) return 0;
  const size_t step = decimalDigits(modulus);
  size_t count = 0;
  for (size_t i = 0; count < 10 && i + step < decimalLen; i += step) {
    out[count++] = parseDecimal(decimal + i, step) % modulus;
    out[count++] = parseDecimal(decimal + i + 1, step) % modulus;
  }
  return count;
}

bool Base64UrlDecoder::feed(const uint8_t* data, const size_t len) {
  if (failed_ || (!data && len != 0)) return false;
  for (size_t i = 0; i < len; ++i) {
    const uint8_t value = base64Value(data[i]);
    if (value == 0xFF) continue;
    quartet_[quartetLen_++] = value;
    if (quartetLen_ == 4 && !emit(3)) return false;
  }
  return true;
}

bool Base64UrlDecoder::finish() {
  if (failed_) return false;
  if (quartetLen_ == 1) {
    failed_ = true;
    return false;
  }
  if (quartetLen_ == 0) return true;
  return emit(quartetLen_ - 1);
}

bool Base64UrlDecoder::emit(const size_t count) {
  const uint8_t decoded[3] = {
      static_cast<uint8_t>((quartet_[0] << 2) | (quartet_[1] >> 4)),
      static_cast<uint8_t>((quartet_[1] << 4) | (quartet_[2] >> 2)),
      static_cast<uint8_t>((quartet_[2] << 6) | quartet_[3]),
  };
  quartetLen_ = 0;
  if (!sink_ || !sink_(ctx_, decoded, count)) {
    failed_ = true;
    return false;
  }
  return true;
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, const size_t len) {
  static constexpr uint32_t kNibbleTable[16] = {
      0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
      0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
  };
  for (size_t i = 0; i < len; ++i) {
    crc = kNibbleTable[(crc ^ data[i]) & 0x0F] ^ (crc >> 4);
    crc = kNibbleTable[(crc ^ (data[i] >> 4)) & 0x0F] ^ (crc >> 4);
  }
  return crc;
}

}  // namespace WeReadProtocol
