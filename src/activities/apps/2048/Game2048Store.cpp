#include "Game2048Store.h"

#include <HalStorage.h>
#include <Logging.h>

namespace {

constexpr const char* kSavePath = "/.crosspoint/2048.bin";
constexpr const char* kDir = "/.crosspoint";
// Bumped from 1 → 2 to drop dead bestScore + gameOver fields. v1 saves are silently
// discarded (load returns false → fresh game).
constexpr uint8_t SAVE_VERSION = 2;

bool ensureDir() {
  if (Storage.exists(kDir)) return true;
  return Storage.mkdir(kDir);
}

}  // namespace

bool Game2048Store::hasInProgress() { return Storage.exists(kSavePath); }

bool Game2048Store::load(Game2048SaveSlot& out) {
  HalFile f;
  if (!Storage.openFileForRead("G2048", kSavePath, f)) return false;

  uint8_t version = 0;
  if (f.read(&version, 1) != 1 || version != SAVE_VERSION) {
    LOG_ERR("G2048", "Save version mismatch (got %u)", static_cast<unsigned>(version));
    return false;
  }
  if (f.read(out.cells, sizeof(out.cells)) != static_cast<int>(sizeof(out.cells))) return false;
  if (f.read(reinterpret_cast<uint8_t*>(&out.score), sizeof(out.score)) != static_cast<int>(sizeof(out.score))) {
    return false;
  }
  uint8_t wonByte = 0;
  if (f.read(&wonByte, 1) != 1) return false;
  out.won = wonByte != 0;
  return true;
}

bool Game2048Store::save(const Game2048SaveSlot& in) {
  if (!ensureDir()) {
    LOG_ERR("G2048", "Cannot create dir %s", kDir);
    return false;
  }
  HalFile f;
  if (!Storage.openFileForWrite("G2048", kSavePath, f)) return false;

  const uint8_t version = SAVE_VERSION;
  if (f.write(&version, 1) != 1) return false;
  if (f.write(in.cells, sizeof(in.cells)) != sizeof(in.cells)) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(&in.score), sizeof(in.score)) != sizeof(in.score)) return false;
  const uint8_t wonByte = in.won ? 1 : 0;
  if (f.write(&wonByte, 1) != 1) return false;
  f.flush();
  return true;
}

bool Game2048Store::clear() {
  if (!Storage.exists(kSavePath)) return true;
  return Storage.remove(kSavePath);
}
