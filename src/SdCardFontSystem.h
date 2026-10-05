#pragma once

#include <HalStorage.h>  // HalFile (kept open for streamed TTFs)
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <VectorFontSupport.h>

#if CROSSPOINT_VECTOR_FONTS
#include <FontPsram.h>  // PsramVector for resident TTF bytes
#endif

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class GfxRenderer;
class TtfEpdFont;

// Shared by built-in registration and SD UI fallback setup.
inline constexpr int CJK_UI_8_FONT_ID = 0x434A4B08;
inline constexpr int CJK_UI_10_FONT_ID = 0x434A4B0A;
inline constexpr int CJK_UI_12_FONT_ID = 0x434A4B0C;
inline constexpr int CJK_UI_14_FONT_ID = 0x434A4B0E;
inline constexpr int CJK_UI_16_FONT_ID = 0x434A4B10;

// Reader footer slots keep their original sizes when Read Pico enlarges the UI.
inline constexpr int READER_STATUS_FONT_ID = 0x52534208;
inline constexpr int READER_ESTIMATE_FONT_ID = 0x5253420A;

/// Facade that owns the SD card font registry, manager, and resolver logic.
/// Hides implementation details behind a single begin() + ensureLoaded() API.
class SdCardFontSystem {
 public:
  static constexpr const char* COMPLETE_CHINESE_NOTO_SANS_FAMILY = "NotoSansSC";

  // Constructor and destructor are out-of-line (defined in the .cpp where
  // TtfEpdFont is a complete type) so the std::unique_ptr<TtfEpdFont> member
  // can be constructed/destroyed with only a forward declaration visible here.
  SdCardFontSystem();
  ~SdCardFontSystem();
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

  /// 正文当前用的矢量字面是否按 4 位覆盖度加载（16 级文字抗锯齿的前提）。
  /// 预烤的 .cpfont 位图只有 2 位，所以那种情况下恒为 false。
  /// / Whether the reader's live vector face carries 4-bit coverage, which is what the
  /// 16-level text path needs. Always false for pre-rasterised .cpfont bitmaps.
  bool readerFaceIsFourBit() const {
#if CROSSPOINT_VECTOR_FONTS
    return ttf_ != nullptr && ttfFourBit_;
#else
    return false;
#endif
  }

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

#if CROSSPOINT_VECTOR_FONTS
  // --- Vector (.ttf/.otf) font path (FreeInkFont via TtfEpdFont) -------------
  // Load/refresh the selected TTF family at the current reader size, register
  // it with the renderer, and track it so ensureSdCardFontReady() rebuilds its
  // glyph set per page. registryWasDirty forces a reload even if unchanged.
  void loadTtfFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, bool registryWasDirty);
  // Unregister + free the active TTF font (and its UI-size fallbacks), if any.
  void unloadTtf(GfxRenderer& renderer);
  // Register the loaded TTF at each built-in UI size as a script fallback, so UI
  // text (book titles, list rows, menus, status bar) in scripts the built-in
  // fonts lack renders in the chosen TTF. Mirrors setupUiFallbacks for .cpfont.
  void setupTtfUiFallbacks(GfxRenderer& renderer);
  // Open one style source file (resident if small, streamed if large) into
  // ttfSources_[style]. Returns false on open/read failure.
  bool openTtfSource(uint8_t style, const std::string& path);
  // Register every present source with `font` (shared bytes / file handles).
  void addTtfSources(TtfEpdFont& font);
  // Close/free all style sources.
  void freeTtfSources();
  // ReadFn for streamed sources: serves the PSRAM prefix cache first, SD after.
  static unsigned long prefixRead(void* ctx, unsigned long offset, unsigned char* buffer, unsigned long count);
#endif  // CROSSPOINT_VECTOR_FONTS

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  std::atomic<bool> registryDirty_{false};

#if CROSSPOINT_VECTOR_FONTS
  // One style source file. SMALL files are read fully into `bytes` (resident,
  // PSRAM when present); LARGE files stream from `file` (kept open) so a multi-MB
  // file never sits in RAM. All faces (reader + UI sizes) share these sources.
  struct TtfSource {
    // Resident form: the whole file. Streamed form: a PSRAM prefix cache of the
    // file head (cmap/loca/hmtx) — empty when PSRAM couldn't fund it.
    freeink::font::PsramVector<uint8_t> bytes;
    HalFile file;  // open handle (streamed form)
    bool streamed = false;
    unsigned long size = 0;
    bool present = false;
  };

  // Active TTF font (at most one reader-size vector family loaded at a time).
  std::unique_ptr<TtfEpdFont> ttf_;
  // Up to 4 style sources: 0=regular (required), 1=bold, 2=italic, 3=bold-italic.
  TtfSource ttfSources_[4];
  std::string ttfFamily_;     // loaded vector family name ("" = none)
  int ttfFontId_ = 0;         // renderer font id for ttf_ (0 = none)
  uint8_t ttfPointSize_ = 0;  // size ttf_ was built at
  // 正文那个矢量字面是否按 4 位覆盖度加载 —— 16 级文字通路的开关，在 loadTtfFamily()
  // 里按面板能力决定。见 EpubReaderActivity 的 use16LevelText。
  // / Whether the reader's vector face was loaded with 4-bit coverage: the switch for the
  // 16-level text path, decided in loadTtfFamily() from the panel's capability. Used by
  // EpubReaderActivity's use16LevelText.
  bool ttfFourBit_ = false;
  // UI-size TTF fallbacks (share ttfSources_); parallel to their renderer font ids.
  std::vector<std::unique_ptr<TtfEpdFont>> ttfUi_;
  std::vector<int> ttfUiIds_;
#endif  // CROSSPOINT_VECTOR_FONTS
};

// Global SD card font system instance (defined in main.cpp).
extern SdCardFontSystem sdFontSystem;
