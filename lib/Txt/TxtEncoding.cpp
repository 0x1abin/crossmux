#include "TxtEncoding.h"

#include <algorithm>
#include <cstring>

namespace txt_encoding {
namespace {

bool isGbkLead(const uint8_t value) { return value >= 0x81 && value <= 0xFE; }

bool isGbkTrail(const uint8_t value) { return value >= 0x40 && value <= 0xFE && value != 0x7F; }

size_t utf8SequenceLength(const uint8_t lead) {
  if (lead < 0x80) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 0;
}

bool isStrictUtf8(const uint8_t* data, const size_t length, const bool finalChunk, bool& hasCompleteNonAscii) {
  size_t pos = 0;
  while (pos < length) {
    const uint8_t lead = data[pos];
    if (lead < 0x80) {
      pos++;
      continue;
    }
    const size_t sequenceLength = utf8SequenceLength(lead);
    if (sequenceLength == 0) return false;
    if (pos + sequenceLength > length) return !finalChunk;
    for (size_t i = 1; i < sequenceLength; i++) {
      if ((data[pos + i] & 0xC0) != 0x80) return false;
    }

    uint32_t codepoint = lead & ((1U << (7 - sequenceLength)) - 1);
    for (size_t i = 1; i < sequenceLength; i++) {
      codepoint = (codepoint << 6) | (data[pos + i] & 0x3F);
    }
    const bool overlong = (sequenceLength == 2 && codepoint < 0x80) || (sequenceLength == 3 && codepoint < 0x800) ||
                          (sequenceLength == 4 && codepoint < 0x10000);
    if (overlong || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) return false;
    hasCompleteNonAscii = true;
    pos += sequenceLength;
  }
  return true;
}

#ifdef ENABLE_CHINESE_VERSION
#include "GbkToUnicodeTable.inc"

uint16_t gbkToUnicode(const uint8_t lead, const uint8_t trail) {
  if (!isGbkLead(lead) || !isGbkTrail(trail)) return 0;
  const size_t trailIndex = trail < 0x7F ? trail - 0x40 : trail - 0x41;
  return kGbkToUnicode[(static_cast<size_t>(lead) - 0x81) * 190 + trailIndex];
}

bool isStrictGbk(const uint8_t* data, const size_t length, const bool finalChunk, bool& hasCompleteCharacter) {
  size_t pos = 0;
  while (pos < length) {
    if (data[pos] < 0x80) {
      pos++;
      continue;
    }
    if (!isGbkLead(data[pos])) return false;
    if (pos + 1 >= length) return !finalChunk;
    if (gbkToUnicode(data[pos], data[pos + 1]) == 0) return false;
    hasCompleteCharacter = true;
    pos += 2;
  }
  return true;
}

size_t utf8EncodedLength(const uint32_t codepoint) {
  if (codepoint < 0x80) return 1;
  if (codepoint < 0x800) return 2;
  return 3;
}

size_t appendUtf8(const uint32_t codepoint, uint8_t* output) {
  if (utf8EncodedLength(codepoint) == 1) {
    output[0] = static_cast<uint8_t>(codepoint);
    return 1;
  }
  if (utf8EncodedLength(codepoint) == 2) {
    output[0] = static_cast<uint8_t>(0xC0 | (codepoint >> 6));
    output[1] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
    return 2;
  }
  output[0] = static_cast<uint8_t>(0xE0 | (codepoint >> 12));
  output[1] = static_cast<uint8_t>(0x80 | ((codepoint >> 6) & 0x3F));
  output[2] = static_cast<uint8_t>(0x80 | (codepoint & 0x3F));
  return 3;
}
#endif

}  // namespace

bool isSerializedValueValid(const uint8_t value) { return value <= static_cast<uint8_t>(Encoding::Gbk); }

bool isSupported(const Encoding encoding) {
  switch (encoding) {
    case Encoding::Unknown:
    case Encoding::Utf8:
      return true;
    case Encoding::Gbk:
#ifdef ENABLE_CHINESE_VERSION
      return true;
#else
      return false;
#endif
  }
  return false;
}

Encoding detect(const uint8_t* data, const size_t length, const bool finalChunk) {
#ifndef ENABLE_CHINESE_VERSION
  (void)data;
  (void)length;
  (void)finalChunk;
  return Encoding::Utf8;
#else
  bool hasCompleteUtf8 = false;
  if (isStrictUtf8(data, length, finalChunk, hasCompleteUtf8)) {
    return hasCompleteUtf8 ? Encoding::Utf8 : Encoding::Unknown;
  }
  bool hasCompleteGbk = false;
  if (isStrictGbk(data, length, finalChunk, hasCompleteGbk)) {
    return hasCompleteGbk ? Encoding::Gbk : Encoding::Unknown;
  }
  return Encoding::Utf8;
#endif
}

TranscodeResult transcodeGbkInPlace(uint8_t* buffer, size_t rawLength, const size_t capacity, const bool finalChunk) {
#ifndef ENABLE_CHINESE_VERSION
  (void)buffer;
  (void)capacity;
  (void)finalChunk;
  return {0, 0};
#else
  if (!buffer || rawLength == 0 || rawLength >= capacity) return {0, 0};

  size_t utf8Length = 0;
  size_t pos = 0;
  while (pos < rawLength) {
    if (buffer[pos] < 0x80) {
      utf8Length++;
      pos++;
      continue;
    }
    if (pos + 1 < rawLength) {
      const uint16_t mapped = gbkToUnicode(buffer[pos], buffer[pos + 1]);
      if (mapped != 0) {
        utf8Length += utf8EncodedLength(mapped);
        pos += 2;
        continue;
      }
    }
    if (!finalChunk && pos + 1 == rawLength && isGbkLead(buffer[pos])) {
      rawLength = pos;
      break;
    }
    utf8Length++;
    pos++;
  }
  if (rawLength + utf8Length >= capacity) return {0, 0};

  const size_t sourceLength = rawLength;
  size_t sourcePos = 0;
  size_t outputPos = sourceLength;
  while (sourcePos < sourceLength) {
    if (buffer[sourcePos] < 0x80) {
      buffer[outputPos++] = buffer[sourcePos++];
      continue;
    }

    uint32_t codepoint = '?';
    size_t consumed = 1;
    if (sourcePos + 1 < sourceLength) {
      const uint16_t mapped = gbkToUnicode(buffer[sourcePos], buffer[sourcePos + 1]);
      if (mapped != 0) {
        codepoint = mapped;
        consumed = 2;
      }
    }
    outputPos += appendUtf8(codepoint, buffer + outputPos);
    sourcePos += consumed;
  }

  std::memmove(buffer, buffer + sourceLength, utf8Length);
  buffer[utf8Length] = '\0';
  return {sourceLength, utf8Length};
#endif
}

size_t gbkSourceLength(const uint8_t* utf8, const size_t utf8Length) {
  size_t sourceLength = 0;
  size_t pos = 0;
  while (pos < utf8Length) {
    const size_t sequenceLength = std::max<size_t>(utf8SequenceLength(utf8[pos]), 1);
    sourceLength += sequenceLength == 1 ? 1 : 2;
    pos += std::min(sequenceLength, utf8Length - pos);
  }
  return sourceLength;
}

}  // namespace txt_encoding
