#pragma once

#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <atomic>

class GfxRenderer;

// Shared by built-in registration and SD UI fallback setup.
inline constexpr int CJK_UI_8_FONT_ID = 0x434A4B08;
inline constexpr int CJK_UI_10_FONT_ID = 0x434A4B0A;
inline constexpr int CJK_UI_12_FONT_ID = 0x434A4B0C;

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  static constexpr const char* COMPLETE_CHINESE_NOTO_SANS_FAMILY = "NotoSansSC";

  SdCardFontSystem() = default;
  SdCardFontSystem(const SdCardFontSystem&) = delete;
  SdCardFontSystem& operator=(const SdCardFontSystem&) = delete;
  /// Discover SD card fonts and load user's saved selection. Call once during setup.
  void begin(GfxRenderer& renderer);

  /// Ensure the correct SD font family is loaded for the current settings.
  /// Call before entering the reader or after settings change.
  /// Also re-discovers if the registry has been marked dirty (e.g. by web upload).
  void ensureLoaded(GfxRenderer& renderer, bool allowFlashCache = true);

  /// Release the resident SD font without changing the saved selection.
  void releaseLoadedFont(GfxRenderer& renderer);

  /// Resolve an SD card font ID from family name + reader point size.
  /// Returns 0 if not found. Used by CrossPointSettings::getReaderFontId().
  /// Pure lookup: never loads and never mutates residency, so
  /// adoptCompleteChineseNotoSans() can probe the reader size cheaply.
  int resolveFontId(const char* familyName, uint8_t pointSize) const;

  /// Resolve the SD font ID for a UI point size, additively loading that exact
  /// size from the active family when it is not resident yet. A requested size
  /// already resident (typically the reader face) is reused without loading, so
  /// CrossPointSettings::getReaderFontId() keeps its old behaviour. Returns 0 when
  /// the family ships no face at that size, which makes the caller keep its
  /// built-in UI font. Requires begin() or ensureLoaded() to have supplied the
  /// renderer.
  int resolveUiFontId(const char* familyName, uint8_t pointSize);

  /// Access the registry (e.g. for settings UI to enumerate available fonts).
  const SdCardFontRegistry& registry() const { return registry_; }

  /// Non-const access to the registry (for FontInstaller).
  SdCardFontRegistry& registry() { return registry_; }

  /// Mark the registry as needing re-discovery.
  /// Thread-safe: can be called from the web server task.
  void markRegistryDirty() { registryDirty_.store(true, std::memory_order_release); }

  /// Chinese builds replace the duplicate built-in reader face with the
  /// complete SD-card Noto Sans family when it is installed.
  /// Returns true when the saved selection changed.
  bool adoptCompleteChineseNotoSans();

  /// If the registry is dirty, re-scan the SD card now and clear the flag.
  /// Used by the web UI so uploaded/deleted fonts appear in the list
  /// without waiting for the reader activity to run ensureLoaded().
  void refreshIfDirty() {
    if (registryDirty_.exchange(false, std::memory_order_acquire)) {
      registry_.discover();
      adoptCompleteChineseNotoSans();
    }
  }

 private:
  // PSRAM-equipped S3 devices load size-matched SD UI fallbacks while retaining
  // the embedded CJK subsets as backups. No-PSRAM unified builds keep only the
  // reader size resident to preserve contiguous heap.
  void setupUiFallbacks(GfxRenderer& renderer);

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  // Last renderer passed to begin()/ensureLoaded(); needed to additively load
  // extra UI sizes on demand. Null until either has run.
  GfxRenderer* renderer_ = nullptr;
  std::atomic<bool> registryDirty_{false};
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
