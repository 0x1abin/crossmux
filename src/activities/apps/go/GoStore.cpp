#include "GoStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>

namespace {

constexpr const char* kSavePath = "/.crosspoint/go.bin";
constexpr const char* kStatsPath = "/.crosspoint/go_stats.bin";
constexpr const char* kDir = "/.crosspoint";

// Bump SAVE_VERSION when the binary layout below changes.
constexpr uint8_t SAVE_VERSION = 1;
constexpr uint8_t STATS_VERSION = 1;

bool ensureDir() {
  if (Storage.exists(kDir)) return true;
  return Storage.mkdir(kDir);
}

}  // namespace

bool GoStore::hasInProgress() { return Storage.exists(kSavePath); }

bool GoStore::load(GoSaveSlot& out) {
  HalFile f;
  if (!Storage.openFileForRead("GO", kSavePath, f)) {
    return false;
  }
  uint8_t version = 0;
  if (f.read(&version, 1) != 1 || version != SAVE_VERSION) {
    LOG_ERR("GO", "Save version mismatch (got %u)", static_cast<unsigned>(version));
    return false;
  }
  uint8_t mode = 0;
  if (f.read(&mode, 1) != 1) return false;
  out.mode = static_cast<GoMode>(mode);
  uint8_t diffByte = 0;
  if (f.read(&diffByte, 1) != 1) return false;
  out.difficulty = (diffByte < static_cast<uint8_t>(GoDifficulty::Count))
                       ? static_cast<GoDifficulty>(diffByte)
                       : GoDifficulty::Kyu12;
  if (f.read(&out.cursorX, 1) != 1) return false;
  if (f.read(&out.cursorY, 1) != 1) return false;
  if (f.read(reinterpret_cast<uint8_t*>(&out.elapsedSec), sizeof(out.elapsedSec)) !=
      static_cast<int>(sizeof(out.elapsedSec))) {
    return false;
  }
  // Game is a POD of uint8_t fields (board + ko state + captures + area):
  // no padding, serialize as raw bytes.
  if (f.read(reinterpret_cast<uint8_t*>(&out.game), sizeof(Game)) != static_cast<int>(sizeof(Game))) {
    LOG_ERR("GO", "Save game read failed");
    return false;
  }
  return true;
}

bool GoStore::save(const GoSaveSlot& in) {
  if (!ensureDir()) {
    LOG_ERR("GO", "Cannot create dir %s", kDir);
    return false;
  }
  HalFile f;
  if (!Storage.openFileForWrite("GO", kSavePath, f)) {
    return false;
  }
  const uint8_t version = SAVE_VERSION;
  if (f.write(&version, 1) != 1) return false;
  const uint8_t mode = static_cast<uint8_t>(in.mode);
  if (f.write(&mode, 1) != 1) return false;
  const uint8_t diffByte = static_cast<uint8_t>(in.difficulty);
  if (f.write(&diffByte, 1) != 1) return false;
  if (f.write(&in.cursorX, 1) != 1) return false;
  if (f.write(&in.cursorY, 1) != 1) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(&in.elapsedSec), sizeof(in.elapsedSec)) != sizeof(in.elapsedSec)) {
    return false;
  }
  if (f.write(reinterpret_cast<const uint8_t*>(&in.game), sizeof(Game)) != sizeof(Game)) {
    LOG_ERR("GO", "Save game write failed");
    return false;
  }
  f.flush();
  return true;
}

bool GoStore::clear() {
  if (!Storage.exists(kSavePath)) return true;
  return Storage.remove(kSavePath);
}

GoStore::GoStats GoStore::loadStats() {
  GoStats out;
  if (!Storage.exists(kStatsPath)) return out;
  HalFile f;
  if (!Storage.openFileForRead("GO", kStatsPath, f)) return out;
  uint8_t version = 0;
  if (f.read(&version, 1) != 1 || version != STATS_VERSION) {
    LOG_ERR("GO", "Stats version mismatch (got %u)", static_cast<unsigned>(version));
    return GoStats{};
  }
  if (f.read(reinterpret_cast<uint8_t*>(&out), sizeof(out)) != static_cast<int>(sizeof(out))) {
    LOG_ERR("GO", "Stats truncated; resetting");
    return GoStats{};
  }
  return out;
}

bool GoStore::saveStats(const GoStats& stats) {
  if (!ensureDir()) return false;
  HalFile f;
  if (!Storage.openFileForWrite("GO", kStatsPath, f)) return false;
  const uint8_t version = STATS_VERSION;
  if (f.write(&version, 1) != 1) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(&stats), sizeof(stats)) != sizeof(stats)) return false;
  f.flush();
  return true;
}

void GoStore::recordStart() {
  GoStats stats = loadStats();
  if (stats.startedCount < 0xFFFF) stats.startedCount++;
  saveStats(stats);
}

void GoStore::recordWin(uint8_t winnerColor) {
  GoStats stats = loadStats();
  if (winnerColor == BLACK && stats.blackWins < 0xFFFF) stats.blackWins++;
  if (winnerColor == WHITE && stats.whiteWins < 0xFFFF) stats.whiteWins++;
  saveStats(stats);
}
