#include "SudokuStore.h"

#include <HalStorage.h>
#include <Logging.h>

namespace {

constexpr const char* kSavePath = "/.crosspoint/sudoku.bin";
constexpr const char* kStatsPath = "/.crosspoint/sudoku_stats.bin";
constexpr const char* kDir = "/.crosspoint";

constexpr uint8_t SAVE_VERSION = 1;
constexpr uint8_t STATS_VERSION = 1;

bool ensureDir() {
  if (Storage.exists(kDir)) return true;
  return Storage.mkdir(kDir);
}

}  // namespace

bool SudokuStore::hasInProgress() { return Storage.exists(kSavePath); }

bool SudokuStore::load(SudokuSaveSlot& out) {
  HalFile f;
  if (!Storage.openFileForRead("SDK", kSavePath, f)) {
    return false;
  }
  uint8_t version = 0;
  if (f.read(&version, 1) != 1 || version != SAVE_VERSION) {
    LOG_ERR("SDK", "Save version mismatch (got %u)", static_cast<unsigned>(version));
    return false;
  }
  uint8_t diff = 0;
  if (f.read(&diff, 1) != 1) return false;
  out.difficulty = static_cast<SudokuBoard::Difficulty>(diff);
  if (f.read(reinterpret_cast<uint8_t*>(&out.elapsedSec), sizeof(out.elapsedSec)) !=
      static_cast<int>(sizeof(out.elapsedSec))) {
    return false;
  }
  if (f.read(&out.cursorR, 1) != 1) return false;
  if (f.read(&out.cursorC, 1) != 1) return false;
  uint8_t notesByte = 0;
  if (f.read(&notesByte, 1) != 1) return false;
  out.notesMode = notesByte != 0;
  if (f.read(&out.mistakes, 1) != 1) return false;
  if (f.read(&out.hintsLeft, 1) != 1) return false;
  if (!out.board.readFrom(f)) {
    LOG_ERR("SDK", "Save board read failed");
    return false;
  }
  return true;
}

bool SudokuStore::save(const SudokuSaveSlot& in) {
  if (!ensureDir()) {
    LOG_ERR("SDK", "Cannot create dir %s", kDir);
    return false;
  }
  HalFile f;
  if (!Storage.openFileForWrite("SDK", kSavePath, f)) {
    return false;
  }
  const uint8_t version = SAVE_VERSION;
  if (f.write(&version, 1) != 1) return false;
  const uint8_t diff = static_cast<uint8_t>(in.difficulty);
  if (f.write(&diff, 1) != 1) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(&in.elapsedSec), sizeof(in.elapsedSec)) != sizeof(in.elapsedSec)) {
    return false;
  }
  if (f.write(&in.cursorR, 1) != 1) return false;
  if (f.write(&in.cursorC, 1) != 1) return false;
  const uint8_t notesByte = in.notesMode ? 1 : 0;
  if (f.write(&notesByte, 1) != 1) return false;
  if (f.write(&in.mistakes, 1) != 1) return false;
  if (f.write(&in.hintsLeft, 1) != 1) return false;
  if (!in.board.writeTo(f)) {
    LOG_ERR("SDK", "Save board write failed");
    return false;
  }
  f.flush();
  return true;
}

bool SudokuStore::clear() {
  if (!Storage.exists(kSavePath)) return true;
  return Storage.remove(kSavePath);
}

SudokuStats SudokuStore::loadStats() {
  SudokuStats out;
  if (!Storage.exists(kStatsPath)) return out;
  HalFile f;
  if (!Storage.openFileForRead("SDK", kStatsPath, f)) return out;
  uint8_t version = 0;
  if (f.read(&version, 1) != 1 || version != STATS_VERSION) return out;
  f.read(reinterpret_cast<uint8_t*>(out.bestTimeSec), sizeof(out.bestTimeSec));
  f.read(reinterpret_cast<uint8_t*>(out.completedCount), sizeof(out.completedCount));
  f.read(reinterpret_cast<uint8_t*>(out.startedCount), sizeof(out.startedCount));
  return out;
}

bool SudokuStore::saveStats(const SudokuStats& stats) {
  if (!ensureDir()) return false;
  HalFile f;
  if (!Storage.openFileForWrite("SDK", kStatsPath, f)) return false;
  const uint8_t version = STATS_VERSION;
  if (f.write(&version, 1) != 1) return false;
  if (f.write(reinterpret_cast<const uint8_t*>(stats.bestTimeSec), sizeof(stats.bestTimeSec)) !=
      sizeof(stats.bestTimeSec)) {
    return false;
  }
  if (f.write(reinterpret_cast<const uint8_t*>(stats.completedCount), sizeof(stats.completedCount)) !=
      sizeof(stats.completedCount)) {
    return false;
  }
  if (f.write(reinterpret_cast<const uint8_t*>(stats.startedCount), sizeof(stats.startedCount)) !=
      sizeof(stats.startedCount)) {
    return false;
  }
  f.flush();
  return true;
}

void SudokuStore::recordStart(SudokuBoard::Difficulty d) {
  const uint8_t i = static_cast<uint8_t>(d);
  if (i > 2) return;
  SudokuStats stats = loadStats();
  if (stats.startedCount[i] < 0xFFFF) stats.startedCount[i]++;
  saveStats(stats);
}

void SudokuStore::recordCompletion(SudokuBoard::Difficulty d, uint16_t timeSec) {
  const uint8_t i = static_cast<uint8_t>(d);
  if (i > 2) return;
  SudokuStats stats = loadStats();
  if (stats.completedCount[i] < 0xFFFF) stats.completedCount[i]++;
  if (stats.bestTimeSec[i] == 0 || timeSec < stats.bestTimeSec[i]) {
    stats.bestTimeSec[i] = timeSec;
  }
  saveStats(stats);
}
