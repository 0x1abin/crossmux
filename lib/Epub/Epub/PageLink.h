#pragma once

#include <Memory.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "FootnoteEntry.h"

// One laid-out internal EPUB link. Coordinates are relative to the page origin;
// the reader adds its oriented margins when hit-testing the displayed page.
struct PageLink {
  char href[FOOTNOTE_HREF_LEN];
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;

  PageLink() { href[0] = '\0'; }

  bool contains(const int pageX, const int pageY, const int slop, const int minWidth) const {
    const int horizontalSlop = std::max(slop, (minWidth - width) / 2);
    return pageX >= x - horizontalSlop && pageX < x + width + horizontalSlop && pageY >= y - slop &&
           pageY < y + height + slop;
  }
};

class PageLinkList {
  std::unique_ptr<PageLink[]> entries_;
  uint8_t capacity_ = 0;
  uint8_t size_ = 0;
  bool allocationFailed_ = false;

  bool allocateStorage(const size_t capacity) {
    entries_ = makeUniqueNoThrow<PageLink[]>(capacity);
    if (!entries_) {
      allocationFailed_ = true;
      return false;
    }
    capacity_ = static_cast<uint8_t>(capacity);
    return true;
  }

 public:
  static constexpr size_t MAX_SIZE = 32;

  PageLinkList() = default;
  PageLinkList(const PageLinkList&) = delete;
  PageLinkList& operator=(const PageLinkList&) = delete;

  PageLinkList(PageLinkList&& other) noexcept { *this = std::move(other); }
  PageLinkList& operator=(PageLinkList&& other) noexcept {
    if (this == &other) return *this;
    entries_ = std::move(other.entries_);
    capacity_ = std::exchange(other.capacity_, 0);
    size_ = std::exchange(other.size_, 0);
    allocationFailed_ = std::exchange(other.allocationFailed_, false);
    return *this;
  }

  PageLink* append() {
    if (size_ >= MAX_SIZE || allocationFailed_) return nullptr;
    if (!entries_) {
      // 32 links occupy 8,448 bytes: too large for the task stack. Allocate
      // once per linked page so growth cannot fragment the C3 heap.
      if (!allocateStorage(MAX_SIZE)) return nullptr;
    }
    if (size_ >= capacity_) return nullptr;
    return &entries_[size_++];
  }

  bool resize(const size_t size) {
    if (size > MAX_SIZE) return false;
    if (size == 0) {
      entries_.reset();
      capacity_ = 0;
      size_ = 0;
      allocationFailed_ = false;
      return true;
    }
    if (!entries_ && !allocateStorage(size)) return false;
    if (size > capacity_) return false;
    size_ = static_cast<uint8_t>(size);
    return true;
  }

  void clear() { resize(0); }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  bool allocationFailed() const { return allocationFailed_; }
  PageLink* begin() { return entries_.get(); }
  const PageLink* begin() const { return entries_.get(); }
  PageLink* end() { return entries_ ? &entries_[size_] : nullptr; }
  const PageLink* end() const { return entries_ ? &entries_[size_] : nullptr; }
  PageLink& operator[](const size_t index) { return entries_[index]; }
  const PageLink& operator[](const size_t index) const { return entries_[index]; }
};
