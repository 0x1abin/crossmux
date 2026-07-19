#include "SokobanStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace {
constexpr const char* kSavePath = "/.crosspoint/sokoban.bin";
constexpr const char* kDir = "/.crosspoint";
constexpr uint8_t SAVE_VERSION = 2;

bool ensureDir() {
  if (Storage.exists(kDir)) return true;
  return Storage.mkdir(kDir);
}
}  // namespace

bool SokobanStore::save(const SokobanSaveSlot& slot) {
  if (!ensureDir()) return false;
  HalFile f;
  if (!Storage.openFileForWrite("SDK", kSavePath, f)) return false;
  uint8_t version = SAVE_VERSION;
  if (f.write(&version, 1) != 1) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(&slot.currentLevel), sizeof(slot.currentLevel)) !=
      sizeof(slot.currentLevel))
    return false;
  if (f.write(reinterpret_cast<const uint8_t*>(&slot.moves), sizeof(slot.moves)) != sizeof(slot.moves)) return false;
  f.flush();
  return true;
}

bool SokobanStore::load(SokobanSaveSlot& out) {
  if (!Storage.exists(kSavePath)) return false;
  HalFile f;
  if (!Storage.openFileForRead("SDK", kSavePath, f)) return false;
  uint8_t version = 0;
  if (f.read(&version, 1) != 1 || version != SAVE_VERSION) return false;
  if (f.read(reinterpret_cast<uint8_t*>(&out.currentLevel), sizeof(out.currentLevel)) != sizeof(out.currentLevel))
    return false;
  if (f.read(reinterpret_cast<uint8_t*>(&out.moves), sizeof(out.moves)) != sizeof(out.moves)) return false;
  out.hasBoard = false;
  return true;
}

bool SokobanStore::clear() {
  if (!Storage.exists(kSavePath)) return true;
  return Storage.remove(kSavePath);
}

bool SokobanStore::saveLevel(int level, int moves) {
  SokobanSaveSlot slot;
  slot.currentLevel = level;
  slot.moves = moves;
  slot.hasBoard = false;
  return save(slot);
}