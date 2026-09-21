#pragma once

#include <cstdint>

namespace appVisibility {

// Persisted bit positions: never reorder or reuse IDs, including uncompiled apps.
enum class AppId : uint8_t {
  ReadingStats = 0,
  WeRead = 1,
  Sudoku = 2,
  Gomoku = 3,
  ChineseChess = 4,
  Minesweeper = 5,
  Game2048 = 6,
  UglyAvatar = 7,
  Standby = 8,
  AirPage = 9,
  Buddy = 10,
  Sokoban = 11,
  PixelSwitch = 12,
  FileTransfer = 13,
  OpdsBrowser = 14,
  Calculator = 15,
  Woodfish = 16,
  Count = 17,
};

constexpr uint32_t appBit(const AppId id) { return uint32_t{1} << static_cast<uint8_t>(id); }

constexpr uint32_t DEFAULT_HIDDEN_APPS_MASK =
    appBit(AppId::ChineseChess) | appBit(AppId::Minesweeper) | appBit(AppId::Game2048) | appBit(AppId::UglyAvatar) |
    appBit(AppId::Buddy) | appBit(AppId::Sokoban) | appBit(AppId::PixelSwitch) | appBit(AppId::Woodfish);

static_assert(static_cast<uint8_t>(AppId::Count) <= 32, "app IDs must fit the persisted mask");

}  // namespace appVisibility
