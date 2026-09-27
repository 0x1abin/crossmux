#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Reader font size is stored as an actual point size (see CrossPointSettings::
// fontPointSize), not an abstract Small/Medium/Large slot. The selectable sizes
// therefore come from whichever family is active: the built-in set below, or the
// .cpfont files a user installed for an SD family.

// The unified firmware keeps one offline reader fallback. Other point sizes and
// style variants are supplied by installed SD-card font families.
#if FREEINK_DEVICE_READPICO
// Read Pico offers 16 pt alongside the 12 pt offline fallback. Its panel is far
// denser than the X3/X4 baseline the single-size list was chosen for, so the
// larger step is worth having in the picker. This changes only which sizes are
// SELECTABLE: no new font is embedded, and a unit without an installed 16 pt
// .cpfont family snaps back to 12 via snapToNearestPointSize().
inline constexpr uint8_t BUILTIN_READER_POINT_SIZES[] = {12, 16};
#else
inline constexpr uint8_t BUILTIN_READER_POINT_SIZES[] = {12};
#endif

// Point sizes selectable for the active reader font, ascending: the SD family's
// installed sizes when `sdFamilyName` names one the registry knows, otherwise
// the built-in set. Never returns empty.
std::vector<uint8_t> readerFontPointSizes(const SdCardFontRegistry* registry, const char* sdFamilyName);

// Closest entry in `sizes` (ascending, `count` > 0) to `pt`; ties resolve to the
// smaller size. Takes a raw range rather than a vector because getReaderFontId()
// runs inside the page render loop and must not allocate.
uint8_t snapToNearestPointSize(const uint8_t* sizes, size_t count, uint8_t pt);

inline uint8_t snapToNearestPointSize(const std::vector<uint8_t>& sizes, const uint8_t pt) {
  return sizes.empty() ? pt : snapToNearestPointSize(sizes.data(), sizes.size(), pt);
}
