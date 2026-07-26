#include "WeReadProtocol.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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
  if (status == 401 || status == 403 || (status == 200 && emptyObject)) return ChapterResponse::Unavailable;
  return status == 200 ? ChapterResponse::Content : ChapterResponse::Error;
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
