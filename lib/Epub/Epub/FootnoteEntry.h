#pragma once

#include <Memory.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 256
// ponytail: bumped from 96 to 256; calibre-generated EPUBs with long filenames
// and URL-encoded characters routinely exceed 96 chars (e.g.
// "Author-Title_split_NNN.html#_ftnN" encoded is ~150 chars).

struct FootnoteEntry {
  char number[FOOTNOTE_NUMBER_LEN];
  char href[FOOTNOTE_HREF_LEN];

  FootnoteEntry() {
    number[0] = '\0';
    href[0] = '\0';
  }
};

class FootnoteList {
  std::unique_ptr<FootnoteEntry[]> entries_;
  uint8_t capacity_ = 0;
  uint8_t size_ = 0;
  bool allocationFailed_ = false;

  bool allocateStorage(const size_t capacity) {
    if (allocationFailed_) return false;

    // FootnoteEntry is 288 bytes: the bounded 4,608-byte maximum is too large
    // for the task stack, while inline storage would burden every footnote-free page.
    entries_ = makeUniqueNoThrow<FootnoteEntry[]>(capacity);
    if (!entries_) {
      allocationFailed_ = true;
      return false;
    }
    capacity_ = static_cast<uint8_t>(capacity);
    return true;
  }

 public:
  static constexpr size_t MAX_SIZE = 16;

  FootnoteList() = default;
  FootnoteList(const FootnoteList&) = delete;
  FootnoteList& operator=(const FootnoteList&) = delete;

  FootnoteList(FootnoteList&& other) noexcept { *this = std::move(other); }
  FootnoteList& operator=(FootnoteList&& other) noexcept {
    if (this == &other) return *this;
    entries_ = std::move(other.entries_);
    capacity_ = std::exchange(other.capacity_, 0);
    size_ = std::exchange(other.size_, 0);
    allocationFailed_ = std::exchange(other.allocationFailed_, false);
    return *this;
  }

  FootnoteEntry* append() {
    if (size_ >= MAX_SIZE || (!entries_ && !allocateStorage(MAX_SIZE)) || size_ >= capacity_) return nullptr;
    return &entries_[size_++];
  }

  bool resize(size_t size) {
    if (size > MAX_SIZE) return false;
    if (size == 0) {
      entries_.reset();
      capacity_ = 0;
      size_ = 0;
      allocationFailed_ = false;
      return true;
    }
    if ((!entries_ && !allocateStorage(size)) || size > capacity_) return false;
    size_ = static_cast<uint8_t>(size);
    return true;
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  bool allocationFailed() const { return allocationFailed_; }

  FootnoteEntry* data() { return entries_.get(); }
  const FootnoteEntry* data() const { return entries_.get(); }
  FootnoteEntry* begin() { return data(); }
  const FootnoteEntry* begin() const { return data(); }
  FootnoteEntry* end() { return size_ ? data() + size_ : data(); }
  const FootnoteEntry* end() const { return size_ ? data() + size_ : data(); }
  FootnoteEntry& operator[](size_t index) { return entries_[index]; }
  const FootnoteEntry& operator[](size_t index) const { return entries_[index]; }
};
