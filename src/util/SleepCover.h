#pragma once
#include <cstdint>

namespace sleepCover {
inline constexpr char imagePath[] = "/sleep.bmp";
inline constexpr char partPath[] = "/sleep.bmp.part";
inline constexpr char backupPath[] = "/sleep.bmp.bak";
inline constexpr char overlayImagePath[] = "/sleep-overlay.bmp";
inline constexpr char overlayPartPath[] = "/sleep-overlay.bmp.part";
// Run before preparing a new part or reading the installed cover.
bool recover();
bool commit(uint8_t mode);
}  // namespace sleepCover
