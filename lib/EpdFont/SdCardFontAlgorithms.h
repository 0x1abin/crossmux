#pragma once

#include <algorithm>
#include <cstdint>

#include "EpdFontData.h"

namespace sd_card_font_algorithms {

struct PrewarmLoadPolicy {
  bool bitmap;
  bool kernLigature;
};

constexpr PrewarmLoadPolicy prewarmLoadPolicy(const bool metadataOnly, const bool loadKernLigature) {
  return {!metadataOnly, !metadataOnly && loadKernLigature};
}

inline bool insertSortedUnique(uint32_t codepoint, uint32_t* codepoints, uint32_t& count, uint32_t capacity) {
  auto* const end = codepoints + count;
  auto* const pos = std::lower_bound(codepoints, end, codepoint);
  if (pos != end && *pos == codepoint) return true;
  if (count >= capacity) return false;
  std::move_backward(pos, end, end + 1);
  *pos = codepoint;
  ++count;
  return true;
}

template <typename OnMatch>
void forEachKernClassMatch(const uint32_t* codepoints, uint32_t codepointCount, const EpdKernClassEntry* entries,
                           uint16_t entryCount, OnMatch onMatch) {
  uint32_t codepointIndex = 0;
  uint16_t entryIndex = 0;
  while (codepointIndex < codepointCount && entryIndex < entryCount) {
    const uint32_t codepoint = codepoints[codepointIndex];
    const uint32_t entryCodepoint = entries[entryIndex].codepoint;
    if (codepoint < entryCodepoint) {
      ++codepointIndex;
    } else if (codepoint > entryCodepoint) {
      ++entryIndex;
    } else {
      onMatch(entries[entryIndex]);
      ++codepointIndex;
      ++entryIndex;
    }
  }
}

}  // namespace sd_card_font_algorithms
