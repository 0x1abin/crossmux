#include "SdCardFontCache.h"

#include <HalOtaSlot.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <SdCardFont.h>

#include <algorithm>
#include <cstring>

#include "SdCardFontCacheFormat.h"

namespace SdCardFontCache {
namespace {

using sd_card_font_cache_format::Header;

constexpr size_t CHUNK_SIZE = 4096;
constexpr size_t ERASE_BLOCK_SIZE = 64 * 1024;
constexpr size_t MAX_PAYLOAD_SIZE = 6549504;
constexpr size_t CPFONT_HEADER_SIZE = 32;
constexpr size_t CPFONT_TOC_ENTRY_SIZE = 32;
constexpr uint8_t CPFONT_MAGIC[8] = {'C', 'P', 'F', 'O', 'N', 'T', '\0', '\0'};
constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

struct SourceIdentity {
  size_t size = 0;
  uint32_t contentHash = 0;
};

uint16_t readU16(const uint8_t* data) { return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8); }

uint32_t fnv1a(const uint8_t* data, size_t length, uint32_t hash = FNV_OFFSET) {
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= FNV_PRIME;
  }
  return hash;
}

bool identifySource(const char* sourcePath, SourceIdentity& identity) {
  if (!sourcePath || strlen(sourcePath) >= sizeof(Header{}.sourcePath)) return false;

  HalFile file;
  if (!Storage.openFileForRead("SDFCACHE", sourcePath, file)) return false;

  uint8_t data[CPFONT_HEADER_SIZE];
  if (file.read(data, sizeof(data)) != static_cast<int>(sizeof(data)) ||
      memcmp(data, CPFONT_MAGIC, sizeof(CPFONT_MAGIC)) != 0 || readU16(data + 8) != CPFONT_VERSION || data[12] == 0 ||
      data[12] > SdCardFont::MAX_STYLES) {
    return false;
  }

  const uint8_t styleCount = data[12];
  uint32_t hash = fnv1a(data, sizeof(data));
  for (uint8_t i = 0; i < styleCount; ++i) {
    if (file.read(data, CPFONT_TOC_ENTRY_SIZE) != static_cast<int>(CPFONT_TOC_ENTRY_SIZE)) return false;
    hash = fnv1a(data, CPFONT_TOC_ENTRY_SIZE, hash);
  }

  identity.size = file.fileSize();
  identity.contentHash = hash;
  return identity.size >= CPFONT_HEADER_SIZE + static_cast<size_t>(styleCount) * CPFONT_TOC_ENTRY_SIZE;
}

bool readHeader(const HalOtaSlot& slot, Header& header) {
  return slot.valid() && slot.size() > sd_card_font_cache_format::HEADER_AREA_SIZE &&
         slot.read(0, &header, sizeof(header)) &&
         sd_card_font_cache_format::isHeaderValid(
             header, std::min(slot.size() - sd_card_font_cache_format::HEADER_AREA_SIZE, MAX_PAYLOAD_SIZE));
}

void report(ProgressCallback progress, size_t completed, size_t total, void* context) {
  if (progress) progress(completed, total, context);
}

size_t roundUp(size_t value, size_t alignment) { return (value + alignment - 1) / alignment * alignment; }

}  // namespace

size_t capacity() {
  const HalOtaSlot slot = HalOtaSlot::inactive();
  return slot.size() > sd_card_font_cache_format::HEADER_AREA_SIZE
             ? std::min(slot.size() - sd_card_font_cache_format::HEADER_AREA_SIZE, MAX_PAYLOAD_SIZE)
             : 0;
}

bool isValidFor(const char* sourcePath, size_t* payloadSize) {
  if (payloadSize) *payloadSize = 0;

  const HalOtaSlot slot = HalOtaSlot::inactive();
  Header header{};
  SourceIdentity source{};
  const bool valid = readHeader(slot, header) && identifySource(sourcePath, source) &&
                     strcmp(header.sourcePath, sourcePath) == 0 && header.payloadSize == source.size &&
                     header.contentHash == source.contentHash;
  if (valid && payloadSize) *payloadSize = header.payloadSize;
  return valid;
}

bool readAt(size_t offset, void* data, size_t length, size_t payloadSize) {
  static const HalOtaSlot slot = HalOtaSlot::inactive();
  const size_t payloadCapacity =
      slot.size() > sd_card_font_cache_format::HEADER_AREA_SIZE
          ? std::min(slot.size() - sd_card_font_cache_format::HEADER_AREA_SIZE, MAX_PAYLOAD_SIZE)
          : 0;
  if (payloadSize > payloadCapacity || !sd_card_font_cache_format::containsPayloadRange(payloadSize, offset, length)) {
    return false;
  }
  return length == 0 || slot.read(sd_card_font_cache_format::HEADER_AREA_SIZE + offset, data, length);
}

Result preload(const char* sourcePath, ProgressCallback progress, void* context) {
  SourceIdentity source{};
  if (!identifySource(sourcePath, source)) return Result::InvalidFont;
  if (isValidFor(sourcePath)) return Result::AlreadyCached;

  const HalOtaSlot slot = HalOtaSlot::inactive();
  if (!slot.valid() || !slot.safeForScratchWrite()) return Result::NotSafe;
  if (source.size > capacity()) return Result::TooLarge;

  auto buffer = makeUniqueNoThrow<uint8_t[]>(CHUNK_SIZE);
  if (!buffer) return Result::Oom;

  HalFile file;
  if (!Storage.openFileForRead("SDFCACHE", sourcePath, file)) return Result::OpenFailed;
  if (!slot.erase(0, HalOtaSlot::ERASE_SIZE)) return Result::EraseFailed;

  const size_t total = source.size * 2;
  uint32_t payloadCrc = UINT32_MAX;
  size_t offset = 0;
  while (offset < source.size) {
    const size_t eraseLength = std::min(roundUp(source.size - offset, HalOtaSlot::ERASE_SIZE), ERASE_BLOCK_SIZE);
    if (!slot.erase(sd_card_font_cache_format::HEADER_AREA_SIZE + offset, eraseLength)) return Result::EraseFailed;

    const size_t blockEnd = std::min(offset + eraseLength, source.size);
    while (offset < blockEnd) {
      const size_t length = std::min(CHUNK_SIZE, blockEnd - offset);
      if (file.read(buffer.get(), length) != static_cast<int>(length)) return Result::ReadFailed;
      payloadCrc = sd_card_font_cache_format::crc32Update(payloadCrc, buffer.get(), length);
      if (!slot.write(sd_card_font_cache_format::HEADER_AREA_SIZE + offset, buffer.get(), length)) {
        return Result::WriteFailed;
      }
      offset += length;
      report(progress, offset, total, context);
    }
  }
  payloadCrc ^= UINT32_MAX;

  uint32_t flashCrc = UINT32_MAX;
  offset = 0;
  while (offset < source.size) {
    const size_t length = std::min(CHUNK_SIZE, source.size - offset);
    if (!slot.read(sd_card_font_cache_format::HEADER_AREA_SIZE + offset, buffer.get(), length)) {
      return Result::VerifyFailed;
    }
    flashCrc = sd_card_font_cache_format::crc32Update(flashCrc, buffer.get(), length);
    offset += length;
    report(progress, source.size + offset, total, context);
  }
  flashCrc ^= UINT32_MAX;
  if (flashCrc != payloadCrc) return Result::VerifyFailed;

  Header header{};
  memcpy(header.magic, sd_card_font_cache_format::MAGIC, sizeof(header.magic));
  header.version = sd_card_font_cache_format::VERSION;
  header.headerSize = sizeof(header);
  header.payloadSize = source.size;
  header.contentHash = source.contentHash;
  header.payloadCrc = payloadCrc;
  strncpy(header.sourcePath, sourcePath, sizeof(header.sourcePath) - 1);
  header.headerCrc = sd_card_font_cache_format::headerCrc(header);
  if (!slot.write(0, &header, sizeof(header))) return Result::WriteFailed;

  LOG_DBG("SDFCACHE", "Cached %s (%u bytes, crc=%08x)", sourcePath, static_cast<unsigned>(source.size),
          static_cast<unsigned>(payloadCrc));
  return Result::Ok;
}

const char* resultName(Result result) {
  switch (result) {
    case Result::Ok:
      return "ok";
    case Result::AlreadyCached:
      return "already_cached";
    case Result::OpenFailed:
      return "open_failed";
    case Result::InvalidFont:
      return "invalid_font";
    case Result::TooLarge:
      return "too_large";
    case Result::NotSafe:
      return "not_safe";
    case Result::Oom:
      return "oom";
    case Result::EraseFailed:
      return "erase_failed";
    case Result::ReadFailed:
      return "read_failed";
    case Result::WriteFailed:
      return "write_failed";
    case Result::VerifyFailed:
      return "verify_failed";
  }
  return "unknown";
}

}  // namespace SdCardFontCache
