#include "WoodfishStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

constexpr char kDirectory[] = "/.crosspoint";
constexpr char kSavePath[] = "/.crosspoint/woodfish.bin";
constexpr char kTempPath[] = "/.crosspoint/woodfish.bin.tmp";
constexpr char kBackupPath[] = "/.crosspoint/woodfish.bin.bak";
constexpr uint8_t kMagic[] = {'W', 'D', 'F', '1'};
constexpr size_t kRecordSize = 12;

enum class ReadResult : uint8_t {
  Missing,
  Invalid,
  Valid,
};

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe32(uint8_t* data, const uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8);
  data[2] = static_cast<uint8_t>(value >> 16);
  data[3] = static_cast<uint8_t>(value >> 24);
}

ReadResult readRecord(const char* path, uint32_t& total) {
  if (!Storage.exists(path)) return ReadResult::Missing;

  HalFile file;
  if (!Storage.openFileForRead("WDF", path, file)) return ReadResult::Invalid;

  uint8_t record[kRecordSize];
  if (file.fileSize64() != kRecordSize || file.read(record, sizeof(record)) != static_cast<int>(sizeof(record)) ||
      memcmp(record, kMagic, sizeof(kMagic)) != 0) {
    LOG_ERR("WDF", "Invalid woodfish save header: %s", path);
    return ReadResult::Invalid;
  }

  const uint32_t savedTotal = readLe32(record + 4);
  if (readLe32(record + 8) != ~savedTotal) {
    LOG_ERR("WDF", "Invalid woodfish save checksum: %s", path);
    return ReadResult::Invalid;
  }

  total = savedTotal;
  return ReadResult::Valid;
}

}  // namespace

bool WoodfishStore::load(uint32_t& total) {
  total = 0;
  if (readRecord(kSavePath, total) == ReadResult::Valid) return true;

  uint32_t backupTotal = 0;
  if (readRecord(kBackupPath, backupTotal) != ReadResult::Valid) return false;
  total = backupTotal;

  if (Storage.exists(kSavePath) && !Storage.remove(kSavePath)) {
    LOG_ERR("WDF", "Cannot remove invalid woodfish save");
  } else if (!Storage.rename(kBackupPath, kSavePath)) {
    LOG_ERR("WDF", "Cannot restore woodfish backup");
  }
  return true;
}

bool WoodfishStore::save(const uint32_t total) {
  if (!Storage.ensureDirectoryExists(kDirectory)) {
    LOG_ERR("WDF", "Cannot create %s", kDirectory);
    return false;
  }

  uint8_t record[kRecordSize] = {kMagic[0], kMagic[1], kMagic[2], kMagic[3]};
  writeLe32(record + 4, total);
  writeLe32(record + 8, ~total);

  if (Storage.exists(kTempPath) && !Storage.remove(kTempPath)) {
    LOG_ERR("WDF", "Cannot remove stale woodfish temporary file");
    return false;
  }
  {
    HalFile file;
    if (!Storage.openFileForWrite("WDF", kTempPath, file)) return false;
    if (file.write(record, sizeof(record)) != sizeof(record)) {
      LOG_ERR("WDF", "Short write saving woodfish total");
      return false;
    }
    file.flush();
  }

  if (Storage.exists(kBackupPath) && !Storage.remove(kBackupPath)) {
    LOG_ERR("WDF", "Cannot remove stale woodfish backup");
    return false;
  }

  const bool hadSave = Storage.exists(kSavePath);
  if (hadSave && !Storage.rename(kSavePath, kBackupPath)) {
    LOG_ERR("WDF", "Cannot preserve woodfish save");
    return false;
  }
  if (!Storage.rename(kTempPath, kSavePath)) {
    LOG_ERR("WDF", "Cannot install woodfish save");
    if (hadSave && !Storage.rename(kBackupPath, kSavePath)) {
      LOG_ERR("WDF", "Cannot restore previous woodfish save");
    }
    return false;
  }
  if (hadSave && Storage.exists(kBackupPath) && !Storage.remove(kBackupPath)) {
    LOG_ERR("WDF", "Cannot remove committed woodfish backup");
  }
  return true;
}
