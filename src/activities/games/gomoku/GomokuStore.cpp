#include "GomokuStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace {

constexpr const char* kSavePath = "/.crosspoint/gomoku.bin";
constexpr const char* kStatsPath = "/.crosspoint/gomoku_stats.bin";
constexpr const char* kDir = "/.crosspoint";

// Bump SAVE_VERSION / STATS_VERSION when the binary layout below changes.
// load() / loadStats() will reject mismatched versions and fall back to defaults.
constexpr uint8_t SAVE_VERSION = 1;
constexpr uint8_t STATS_VERSION = 1;

bool ensureDir() {
  if (Storage.exists(kDir)) return true;
  return Storage.mkdir(kDir);
}

}  // namespace

bool GomokuStore::hasInProgress() { return Storage.exists(kSavePath); }

bool GomokuStore::load(GomokuSaveSlot& out) {
  HalFile f;
  if (!Storage.openFileForRead("GMK", kSavePath, f)) {
    return false;
  }
  uint8_t version = 0;
  if (f.read(&version, 1) != 1 || version != SAVE_VERSION) {
    LOG_ERR("GMK", "Save version mismatch (got %u)", static_cast<unsigned>(version));
    return false;
  }
  uint8_t mode = 0;
  if (f.read(&mode, 1) != 1) return false;
  out.mode = static_cast<GomokuMode>(mode);
  uint8_t levelByte = 0;
  if (f.read(&levelByte, 1) != 1) return false;
  // Clamp out-of-range values (older saves persisted aiDifficulty=1 here).
  out.aiLevel = (levelByte <= static_cast<uint8_t>(GomokuAiLevel::Hard)) ? static_cast<GomokuAiLevel>(levelByte)
                                                                         : GomokuAiLevel::Medium;
  if (f.read(&out.cursorR, 1) != 1) return false;
  if (f.read(&out.cursorC, 1) != 1) return false;
  if (f.read(reinterpret_cast<uint8_t*>(&out.elapsedSec), sizeof(out.elapsedSec)) !=
      static_cast<int>(sizeof(out.elapsedSec))) {
    return false;
  }
  if (!out.board.readFrom(f)) {
    LOG_ERR("GMK", "Save board read failed");
    return false;
  }
  return true;
}

bool GomokuStore::save(const GomokuSaveSlot& in) {
  if (!ensureDir()) {
    LOG_ERR("GMK", "Cannot create dir %s", kDir);
    return false;
  }
  HalFile f;
  if (!Storage.openFileForWrite("GMK", kSavePath, f)) {
    return false;
  }
  const uint8_t version = SAVE_VERSION;
  if (f.write(&version, 1) != 1) return false;
  const uint8_t mode = static_cast<uint8_t>(in.mode);
  if (f.write(&mode, 1) != 1) return false;
  const uint8_t levelByte = static_cast<uint8_t>(in.aiLevel);
  if (f.write(&levelByte, 1) != 1) return false;
  if (f.write(&in.cursorR, 1) != 1) return false;
  if (f.write(&in.cursorC, 1) != 1) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(&in.elapsedSec), sizeof(in.elapsedSec)) != sizeof(in.elapsedSec)) {
    return false;
  }
  if (!in.board.writeTo(f)) {
    LOG_ERR("GMK", "Save board write failed");
    return false;
  }
  f.flush();
  return true;
}

bool GomokuStore::clear() {
  if (!Storage.exists(kSavePath)) return true;
  return Storage.remove(kSavePath);
}

GomokuStats GomokuStore::loadStats() {
  GomokuStats out;
  if (!Storage.exists(kStatsPath)) return out;
  HalFile f;
  if (!Storage.openFileForRead("GMK", kStatsPath, f)) return out;
  uint8_t version = 0;
  if (f.read(&version, 1) != 1 || version != STATS_VERSION) {
    LOG_ERR("GMK", "Stats version mismatch (got %u)", static_cast<unsigned>(version));
    return GomokuStats{};
  }
  // Each field is read separately; partial reads (truncated file) yield default-zero stats
  // rather than half-populated state being persisted back via saveStats() on next record*().
  if (f.read(reinterpret_cast<uint8_t*>(out.bestTimeSec), sizeof(out.bestTimeSec)) != sizeof(out.bestTimeSec) ||
      f.read(reinterpret_cast<uint8_t*>(out.blackWins), sizeof(out.blackWins)) != sizeof(out.blackWins) ||
      f.read(reinterpret_cast<uint8_t*>(out.whiteWins), sizeof(out.whiteWins)) != sizeof(out.whiteWins) ||
      f.read(reinterpret_cast<uint8_t*>(out.draws), sizeof(out.draws)) != sizeof(out.draws) ||
      f.read(reinterpret_cast<uint8_t*>(out.startedCount), sizeof(out.startedCount)) != sizeof(out.startedCount)) {
    LOG_ERR("GMK", "Stats truncated; resetting");
    return GomokuStats{};
  }
  return out;
}

bool GomokuStore::saveStats(const GomokuStats& stats) {
  if (!ensureDir()) return false;
  HalFile f;
  if (!Storage.openFileForWrite("GMK", kStatsPath, f)) return false;
  const uint8_t version = STATS_VERSION;
  if (f.write(&version, 1) != 1) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(stats.bestTimeSec), sizeof(stats.bestTimeSec)) !=
      sizeof(stats.bestTimeSec)) {
    return false;
  }
  if (f.write(reinterpret_cast<const uint8_t*>(stats.blackWins), sizeof(stats.blackWins)) != sizeof(stats.blackWins)) {
    return false;
  }
  if (f.write(reinterpret_cast<const uint8_t*>(stats.whiteWins), sizeof(stats.whiteWins)) != sizeof(stats.whiteWins)) {
    return false;
  }
  if (f.write(reinterpret_cast<const uint8_t*>(stats.draws), sizeof(stats.draws)) != sizeof(stats.draws)) {
    return false;
  }
  if (f.write(reinterpret_cast<const uint8_t*>(stats.startedCount), sizeof(stats.startedCount)) !=
      sizeof(stats.startedCount)) {
    return false;
  }
  f.flush();
  return true;
}

void GomokuStore::recordStart(uint8_t boardSize) {
  const uint8_t i = sizeIndex(boardSize);
  if (i > 1) return;
  GomokuStats stats = loadStats();
  if (stats.startedCount[i] < 0xFFFF) stats.startedCount[i]++;
  saveStats(stats);
}

void GomokuStore::recordWin(uint8_t boardSize, GomokuBoard::Stone winner, uint16_t timeSec) {
  const uint8_t i = sizeIndex(boardSize);
  if (i > 1) return;
  GomokuStats stats = loadStats();
  if (winner == GomokuBoard::Stone::Black && stats.blackWins[i] < 0xFFFF) stats.blackWins[i]++;
  if (winner == GomokuBoard::Stone::White && stats.whiteWins[i] < 0xFFFF) stats.whiteWins[i]++;
  if (stats.bestTimeSec[i] == 0 || (timeSec > 0 && timeSec < stats.bestTimeSec[i])) {
    stats.bestTimeSec[i] = timeSec;
  }
  saveStats(stats);
}

void GomokuStore::recordDraw(uint8_t boardSize) {
  const uint8_t i = sizeIndex(boardSize);
  if (i > 1) return;
  GomokuStats stats = loadStats();
  if (stats.draws[i] < 0xFFFF) stats.draws[i]++;
  saveStats(stats);
}
