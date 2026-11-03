#include "TxtProgress.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

#include "TxtEncoding.h"

namespace txt_progress {
namespace {
bool readU32(HalFile& file, uint32_t& out) {
  uint8_t bytes[4];
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) return false;
  out = bytes[0] | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
  return true;
}
bool readRecord(HalFile& file, const uint32_t index, Record& out) {
  const uint64_t pos = sizeof(Header) + uint64_t(index) * sizeof(Record);
  return file.seek(pos) && file.read(&out, sizeof(out)) == sizeof(out) && out.width <= 4 && out.step <= 1 &&
         out.reserved == 0 && (out.width || out.step == 0);
}
}  // namespace

LegacyResult readLegacySource(const char* cachePath, const uint32_t sourceSize, uint32_t& sourceOffset) {
  const std::string progressPath = std::string(cachePath) + "/progress.bin";
  if (!Storage.exists(progressPath.c_str())) return LegacyResult::Absent;
  HalFile progress;
  if (!Storage.openFileForRead("TXT", progressPath, progress)) return LegacyResult::Failed;
  uint32_t value = 0;
  if (!readU32(progress, value)) return LegacyResult::Failed;
  if (progress.fileSize64() == 8) {
    if (value != 0x4F545854 || !readU32(progress, sourceOffset) || sourceOffset > sourceSize)
      return LegacyResult::Failed;
    return LegacyResult::Restored;
  }
  if (progress.fileSize64() != 4) return LegacyResult::Failed;

  // Read one old page offset in-place. Never load the page table into RAM.
  HalFile index;
  const std::string indexPath = std::string(cachePath) + "/index.bin";
  if (!Storage.openFileForRead("TXT", indexPath, index)) return LegacyResult::Failed;
  uint32_t magic = 0, cachedSize = 0, count = 0;
  uint8_t version = 0;
  if (!readU32(index, magic) || magic != 0x54585449 || index.read(&version, 1) != 1 || version < 4 || version > 8 ||
      !readU32(index, cachedSize) || cachedSize != sourceSize)
    return LegacyResult::Failed;
  const uint32_t countPosition = 9 + 16 + 1 + (version >= 7 ? 1 : 0) + (version >= 5 ? 1 : 0) + (version >= 6 ? 1 : 0);
  if (!index.seek(countPosition) || !readU32(index, count) || value >= count ||
      index.fileSize64() != countPosition + 4 + uint64_t(count) * 4 ||
      !index.seek(countPosition + 4 + uint64_t(value) * 4) || !readU32(index, sourceOffset) ||
      sourceOffset > sourceSize) {
    return LegacyResult::Failed;
  }
  return LegacyResult::Restored;
}

namespace {
bool readMapping(HalFile& mapping, uint32_t sourceSize, Header& header, Record& end) {
  if (!mapping.seek(0) || mapping.read(&header, sizeof(header)) != sizeof(header) || header.magic != MAGIC ||
      header.version != VERSION || header.sourceSize != sourceSize || header.recordSize != sizeof(Record) ||
      !header.count || !txt_encoding::isSerializedValueValid(header.encoding) ||
      mapping.fileSize64() != sizeof(Header) + uint64_t(header.count) * sizeof(Record))
    return false;
  return readRecord(mapping, header.count - 1, end) && end.source == sourceSize && end.width == 0;
}
}  // namespace

bool resolve(HalFile& mapping, const uint32_t sourceSize, const uint32_t sourceOffset, uint32_t& visibleOffset) {
  Header header;
  Record end;
  if (sourceOffset > sourceSize || !readMapping(mapping, sourceSize, header, end)) return false;
  uint32_t first = 0, last = header.count;
  while (first < last) {
    const uint32_t mid = first + (last - first) / 2;
    Record record;
    if (!readRecord(mapping, mid, record)) return false;
    if (record.source <= sourceOffset)
      first = mid + 1;
    else
      last = mid;
  }
  if (!first) return false;
  Record record;
  if (!readRecord(mapping, first - 1, record)) return false;
  const uint64_t visible =
      uint64_t(record.visible) + (record.width ? (sourceOffset - record.source) / record.width * record.step : 0);
  if (visible > end.visible || visible > UINT32_MAX) return false;
  visibleOffset = static_cast<uint32_t>(visible);
  return true;
}
bool sourceForVisible(HalFile& mapping, const uint32_t sourceSize, const uint32_t visibleOffset,
                      uint32_t& sourceOffset) {
  Header header;
  Record end;
  if (!readMapping(mapping, sourceSize, header, end) || visibleOffset > end.visible) return false;
  uint32_t first = 0, last = header.count;
  while (first < last) {
    const uint32_t mid = first + (last - first) / 2;
    Record record;
    if (!readRecord(mapping, mid, record)) return false;
    if (record.visible <= visibleOffset)
      first = mid + 1;
    else
      last = mid;
  }
  if (!first) return false;
  Record record;
  if (!readRecord(mapping, first - 1, record)) return false;
  const uint64_t source =
      uint64_t(record.source) + (record.step ? uint64_t(visibleOffset - record.visible) * record.width : 0);
  if (source > sourceSize) return false;
  sourceOffset = static_cast<uint32_t>(source);
  return true;
}
}  // namespace txt_progress
