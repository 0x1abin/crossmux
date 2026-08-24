#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "EpdFontData.h"

class SdCardFontGlyphCache final {
 public:
  static constexpr size_t TOTAL_BYTES = 1024 * 1024;
  static constexpr uint32_t ENTRY_COUNT = 4096;
  static constexpr uint32_t PROBE_COUNT = 4;
  static constexpr uint32_t NO_BITMAP = UINT32_MAX;

  enum class LookupResult : uint8_t { Miss, Metadata, Bitmap };
  enum class StoreResult : uint8_t { Stored, AlreadyStored, Collision, Full, Disabled };

  struct Entry {
    uint32_t key = 0;
    EpdGlyph glyph{};
    uint32_t bitmapOffset = NO_BITMAP;
  };

  static constexpr size_t ENTRY_BYTES = sizeof(Entry) * ENTRY_COUNT;
  static_assert(sizeof(EpdGlyph) == 16);
  static_assert(sizeof(Entry) == 24);
  static_assert(ENTRY_BYTES < TOTAL_BYTES);

  void reset(uint8_t* storage = nullptr, size_t size = 0) {
    storage_ = size >= ENTRY_BYTES ? storage : nullptr;
    size_ = storage_ ? size : 0;
    arenaUsed_ = 0;
    if (storage_) std::memset(storage_, 0, ENTRY_BYTES);
  }

  bool enabled() const { return storage_ != nullptr; }
  size_t cachedBytes() const { return enabled() ? ENTRY_BYTES + arenaUsed_ : 0; }

  LookupResult lookup(uint8_t style, uint32_t glyphIndex, EpdGlyph& glyph, const uint8_t*& bitmap) const {
    bitmap = nullptr;
    const Entry* entry = find(style, glyphIndex);
    if (!entry) return LookupResult::Miss;

    glyph = entry->glyph;
    if (glyph.dataLength == 0) return LookupResult::Bitmap;
    if (entry->bitmapOffset == NO_BITMAP) return LookupResult::Metadata;
    bitmap = arena() + entry->bitmapOffset;
    return LookupResult::Bitmap;
  }

  StoreResult storeMetadata(uint8_t style, uint32_t glyphIndex, const EpdGlyph& glyph) {
    Entry* entry = find(style, glyphIndex);
    if (entry) return StoreResult::AlreadyStored;
    entry = findEmpty(style, glyphIndex);
    if (!entry) return enabled() ? StoreResult::Collision : StoreResult::Disabled;

    entry->glyph = glyph;
    entry->bitmapOffset = glyph.dataLength == 0 ? 0 : NO_BITMAP;
    entry->key = makeKey(style, glyphIndex);  // Publish last.
    return StoreResult::Stored;
  }

  StoreResult storeBitmap(uint8_t style, uint32_t glyphIndex, const EpdGlyph& glyph, const uint8_t* bitmap) {
    StoreResult metadataResult = storeMetadata(style, glyphIndex, glyph);
    switch (metadataResult) {
      case StoreResult::Collision:
      case StoreResult::Disabled:
        return metadataResult;
      case StoreResult::Full:
        return StoreResult::Full;
      case StoreResult::Stored:
      case StoreResult::AlreadyStored:
        break;
    }

    Entry* entry = find(style, glyphIndex);
    if (!entry) return StoreResult::Collision;
    if (entry->glyph.dataLength == 0 || entry->bitmapOffset != NO_BITMAP) return StoreResult::AlreadyStored;
    if (!bitmap || entry->glyph.dataLength > arenaCapacity() - arenaUsed_) return StoreResult::Full;

    std::memcpy(arena() + arenaUsed_, bitmap, entry->glyph.dataLength);
    entry->bitmapOffset = static_cast<uint32_t>(arenaUsed_);  // Publish after the complete copy.
    arenaUsed_ += entry->glyph.dataLength;
    return StoreResult::Stored;
  }

 private:
  static uint32_t makeKey(uint8_t style, uint32_t glyphIndex) {
    return 1u + (static_cast<uint32_t>(style) << 16) + glyphIndex;
  }

  static uint32_t firstSlot(uint32_t key) { return key * 2654435761u & (ENTRY_COUNT - 1); }

  Entry* entries() { return reinterpret_cast<Entry*>(storage_); }
  const Entry* entries() const { return reinterpret_cast<const Entry*>(storage_); }
  uint8_t* arena() { return storage_ + ENTRY_BYTES; }
  const uint8_t* arena() const { return storage_ + ENTRY_BYTES; }
  size_t arenaCapacity() const { return size_ - ENTRY_BYTES; }

  Entry* find(uint8_t style, uint32_t glyphIndex) {
    return const_cast<Entry*>(static_cast<const SdCardFontGlyphCache*>(this)->find(style, glyphIndex));
  }

  const Entry* find(uint8_t style, uint32_t glyphIndex) const {
    if (!enabled() || style >= 4 || glyphIndex > UINT16_MAX) return nullptr;
    const uint32_t key = makeKey(style, glyphIndex);
    const uint32_t slot = firstSlot(key);
    for (uint32_t probe = 0; probe < PROBE_COUNT; ++probe) {
      const Entry& entry = entries()[(slot + probe) & (ENTRY_COUNT - 1)];
      if (entry.key == key) return &entry;
      if (entry.key == 0) return nullptr;
    }
    return nullptr;
  }

  Entry* findEmpty(uint8_t style, uint32_t glyphIndex) {
    if (!enabled() || style >= 4 || glyphIndex > UINT16_MAX) return nullptr;
    const uint32_t slot = firstSlot(makeKey(style, glyphIndex));
    for (uint32_t probe = 0; probe < PROBE_COUNT; ++probe) {
      Entry& entry = entries()[(slot + probe) & (ENTRY_COUNT - 1)];
      if (entry.key == 0) return &entry;
    }
    return nullptr;
  }

  uint8_t* storage_ = nullptr;
  size_t size_ = 0;
  size_t arenaUsed_ = 0;
};
