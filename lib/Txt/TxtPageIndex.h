#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace txt_page_index {

constexpr uint8_t LEGACY_CACHE_VERSION = 4;
constexpr uint8_t LAZY_CACHE_VERSION = 5;
constexpr uint8_t ENCODING_CACHE_VERSION = 6;
constexpr uint8_t PARAGRAPH_LAYOUT_CACHE_VERSION = 7;
constexpr uint8_t CACHE_VERSION = PARAGRAPH_LAYOUT_CACHE_VERSION;
constexpr size_t CHECKPOINT_PAGE_COUNT = 32;

enum class AdvanceResult : uint8_t { Unchanged, PageAdded, Completed };

inline bool isSupportedCacheVersion(const uint8_t version) {
  return version >= LEGACY_CACHE_VERSION && version <= CACHE_VERSION;
}

inline bool canReuseCacheVersion(const uint8_t version, const bool confirmedUtf8) {
  return isSupportedCacheVersion(version) && (version >= ENCODING_CACHE_VERSION || confirmedUtf8);
}

inline bool canReuseParagraphLayout(const uint8_t version, const bool extraParagraphSpacing,
                                    const bool cachedExtraParagraphSpacing) {
  return version < PARAGRAPH_LAYOUT_CACHE_VERSION ? extraParagraphSpacing
                                                  : cachedExtraParagraphSpacing == extraParagraphSpacing;
}

inline AdvanceResult recordNextOffset(std::vector<size_t>& offsets, const size_t fileSize, const size_t nextOffset) {
  if (offsets.empty() || nextOffset <= offsets.back() || nextOffset > fileSize) {
    return AdvanceResult::Unchanged;
  }
  if (nextOffset == fileSize) {
    return AdvanceResult::Completed;
  }
  offsets.push_back(nextOffset);
  return AdvanceResult::PageAdded;
}

inline int estimateTotalPages(const size_t fileSize, const std::vector<size_t>& offsets, const bool complete) {
  if (offsets.empty()) {
    return 0;
  }
  if (complete || offsets.size() < 2 || offsets.back() == 0) {
    return static_cast<int>(std::min(offsets.size(), static_cast<size_t>(std::numeric_limits<int>::max())));
  }

  const uint64_t indexedPages = offsets.size() - 1;
  const uint64_t indexedBytes = offsets.back();
  const uint64_t estimate = (static_cast<uint64_t>(fileSize) * indexedPages + indexedBytes - 1) / indexedBytes;
  const uint64_t atLeastKnown = std::max<uint64_t>(estimate, offsets.size());
  return static_cast<int>(std::min<uint64_t>(atLeastKnown, std::numeric_limits<int>::max()));
}

inline int progressPercent(const size_t pageEndOffset, const size_t fileSize) {
  if (fileSize == 0) {
    return 0;
  }
  const uint64_t percent = (static_cast<uint64_t>(std::min(pageEndOffset, fileSize)) * 100 + fileSize / 2) / fileSize;
  return static_cast<int>(std::min<uint64_t>(percent, 100));
}

inline int estimatedPageNumber(const size_t fileSize, const size_t pageStartOffset, const int totalPages) {
  if (totalPages <= 0) return 0;
  if (fileSize == 0) return 1;
  const uint64_t page = static_cast<uint64_t>(std::min(pageStartOffset, fileSize)) * totalPages / fileSize + 1;
  return static_cast<int>(std::min<uint64_t>(page, totalPages));
}

inline bool shouldCheckpoint(const size_t knownPageCount, const bool complete) {
  return complete || (knownPageCount > 0 && knownPageCount % CHECKPOINT_PAGE_COUNT == 0);
}

inline size_t recoveryTargetPage(const size_t savedPage, const size_t knownPageCount) {
  if (knownPageCount == 0) {
    return 0;
  }
  const size_t lastKnownPage = knownPageCount - 1;
  const size_t recoverablePages =
      std::min(CHECKPOINT_PAGE_COUNT - 1, std::numeric_limits<size_t>::max() - lastKnownPage);
  return std::min(savedPage, lastKnownPage + recoverablePages);
}

inline size_t pageForOffset(const std::vector<size_t>& offsets, const size_t sourceOffset) {
  if (offsets.empty()) return 0;
  const auto nextPage = std::upper_bound(offsets.begin(), offsets.end(), sourceOffset);
  return nextPage == offsets.begin() ? 0 : static_cast<size_t>(nextPage - offsets.begin() - 1);
}

}  // namespace txt_page_index
