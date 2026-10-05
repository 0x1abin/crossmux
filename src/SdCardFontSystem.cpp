#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <TtfEpdFont.h>
#include <esp_heap_caps.h>

#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "fontIds.h"

namespace {

#if CROSSPOINT_VECTOR_FONTS
// Stable, non-zero renderer font id for a vector family at a size (FNV-1a of
// name + size). 0 is the "not found" sentinel, so bump collisions to 1.
int computeTtfFontId(const char* familyName, uint8_t pointSize) {
  uint32_t hash = 2166136261u;
  for (const char* p = familyName; p && *p; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  hash ^= pointSize;
  hash *= 16777619u;
  hash ^= 0x54544600u;  // "TTF\0" salt to avoid colliding with cpfont ids
  const int id = static_cast<int>(hash);
  return id != 0 ? id : 1;
}
#endif  // CROSSPOINT_VECTOR_FONTS

}  // namespace

// Out-of-line ctor/dtor: TtfEpdFont is complete here, so unique_ptr<TtfEpdFont>
// can be constructed/destroyed. (Declared in the header where it is only
// forward-declared.)
SdCardFontSystem::SdCardFontSystem() = default;
SdCardFontSystem::~SdCardFontSystem() = default;

namespace {

// Point the reader font size at a size the given family actually ships, and
// persist the change so the settings UI and the loaded font never disagree.
// Guarded by the value-change check: a no-op snap must not write SPIFFS.
void snapFontPointSizeTo(const uint8_t availablePointSize) {
  if (availablePointSize == 0 || availablePointSize == SETTINGS.fontPointSize) return;
  LOG_DBG("SDFS", "Font size %u unavailable, snapping to %u", SETTINGS.fontPointSize, availablePointSize);
  SETTINGS.fontPointSize = availablePointSize;
  SETTINGS.saveToFile();
}

// Physical point sizes at 150 DPI, matching the SD-font converter.
struct UiFontSize {
  int fontId;
  uint8_t pointSize;
  int builtinFallbackId;
};
constexpr UiFontSize kUiFontSizes[] = {
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
    {SMALL_FONT_ID, 12, CJK_UI_12_FONT_ID},         {UI_10_FONT_ID, 14, CJK_UI_14_FONT_ID},
    {UI_12_FONT_ID, 16, CJK_UI_16_FONT_ID},
#else
    {SMALL_FONT_ID, 8, CJK_UI_8_FONT_ID},
    {UI_10_FONT_ID, 10, CJK_UI_10_FONT_ID},
    {UI_12_FONT_ID, 12, CJK_UI_12_FONT_ID},
#endif
#if FREEINK_DEVICE_READPICO
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
    {READER_STATUS_FONT_ID, 12, CJK_UI_12_FONT_ID}, {READER_ESTIMATE_FONT_ID, 14, CJK_UI_14_FONT_ID},
#else
    {READER_STATUS_FONT_ID, 8, CJK_UI_8_FONT_ID}, {READER_ESTIMATE_FONT_ID, 10, CJK_UI_10_FONT_ID},
#endif
#endif
};
#if FREEINK_DEVICE_READPICO
// Read Pico's 4.7" 1216x684 panel is ~300 PPI, roughly double the other targets'
// density, so the shared 8/10/12 pt UI sizes render about half as large on the glass
// and read as too small. These replace the table's point sizes on this device only;
// every other target keeps the shared values above.
//
// Order matches kUiFontSizes: SMALL, UI_10 (body), UI_12 (title), footer, estimate.
// The high-density profile uses 12/14 pt footers; legacy ReadPico stays at 8/10.
// Extra faces reuse resident sizes and the manager's
// bounded, uncached SD loader; no additional PSRAM glyph arena is allocated.
// Enlarged UI sizes were chosen on the glass
// -- 16/18 was too large, and the fallback when a family ships no such size is the
// built-in face at its ORIGINAL size.
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
constexpr uint8_t kReadPicoUiPointSizes[] = {12, 14, 16, 12, 14};
#else
constexpr uint8_t kReadPicoUiPointSizes[] = {12, 12, 14, 8, 10};
#endif
static_assert(std::size(kReadPicoUiPointSizes) == std::size(kUiFontSizes));
#endif

}  // namespace

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  registry_.discover();
  adoptCompleteChineseNotoSans();

  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t pointSize) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, pointSize);
  };
  SETTINGS.sdFontResolverCtx = this;

  // If user has a saved SD font selection, load it
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
#if CROSSPOINT_VECTOR_FONTS
      if (family->vector) {
        // Vector (.ttf/.otf) families load through the FreeInkFont path; the
        // .cpfont manager below rejects them ("Invalid magic bytes") and would
        // wipe the user's selection on every boot. loadTtfFamily keeps the
        // selection on transient failures and registers UI fallbacks itself.
        loadTtfFamily(*family, renderer, /*registryWasDirty=*/false);
      } else
#endif
          if (manager_.loadFamily(*family, renderer, SETTINGS.fontPointSize, SETTINGS.sdFontFlashPreload != 0)) {
        snapFontPointSizeTo(manager_.currentPointSize());
        setupUiFallbacks(renderer);
        LOG_DBG("SDFS", "Loaded SD card font family: %s", SETTINGS.sdFontFamilyName);
      } else {
        LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", SETTINGS.sdFontFamilyName);
        SETTINGS.clearSdFontFamily();
      }
    } else {
      LOG_DBG("SDFS", "SD font family not found on card: %s (clearing)", SETTINGS.sdFontFamilyName);
      SETTINGS.clearSdFontFamily();
    }
  }

  LOG_DBG("SDFS", "SD font system ready (%d families discovered)", registry_.getFamilyCount());
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer, bool allowFlashCache) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty) {
    LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
    registry_.discover();
    adoptCompleteChineseNotoSans();
  }

  const char* wantedFamily = SETTINGS.sdFontFamilyName;

#if CROSSPOINT_VECTOR_FONTS
  // Vector (.ttf/.otf) family selected: route through the FreeInkFont path and
  // drop any pre-rasterized (.cpfont) font that was loaded.
  if (wantedFamily[0] != '\0') {
    const auto* wantedFam = registry_.findFamily(wantedFamily);
    if (wantedFam && wantedFam->vector) {
      if (!manager_.currentFamilyName().empty()) manager_.unloadAll(renderer);
      loadTtfFamily(*wantedFam, renderer, registryWasDirty);
      return;
    }
  }
  // Not on a vector family — ensure any previously-loaded TTF font is released
  // before the pre-rasterized/built-in path below takes over.
  if (!ttfFamily_.empty()) unloadTtf(renderer);
#endif

  const std::string& currentFamily = manager_.currentFamilyName();
  const bool preferFlash = allowFlashCache && SETTINGS.sdFontFlashPreload != 0;

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
#ifndef CROSSMUX_UI_PROFILE_HIGH_DPI
    // Back on a built-in family, which exists only at BUILTIN_READER_POINT_SIZES:
    // a size inherited from an SD family has to come back into that set.
    snapFontPointSizeTo(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES),
                                               SETTINGS.fontPointSize));
#endif
    return;
  }

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.clearSdFontFamily();
      return;
    }
    const auto* selected = family->findNearestSize(SETTINGS.fontPointSize);
    const uint8_t wantedPt = selected ? selected->pointSize : 0;
    // Snap before the early return: the wanted size can already be loaded while
    // the setting still names a size this family does not ship.
    snapFontPointSizeTo(wantedPt);
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) {
      // Nothing to reload, but the UI fallbacks are registered as a side effect of
      // loading and are dropped again when the SD faces are unloaded. This is the
      // path the UI activities actually take, so without re-asserting them here a
      // page such as WiFi draws in the built-in font while other targets show the
      // SD family. setupUiFallbacks() is cheap on a warm family: loadFamilyExtraSize()
      // reuses an already-resident size instead of re-reading it.
      setupUiFallbacks(renderer);
      return;
    }
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u%s", wantedFamily, manager_.currentPointSize(), wantedPt,
            registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    if (manager_.loadFamily(*family, renderer, SETTINGS.fontPointSize, preferFlash)) {
      snapFontPointSizeTo(manager_.currentPointSize());
      setupUiFallbacks(renderer);
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.clearSdFontFamily();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.clearSdFontFamily();
  }
}

void SdCardFontSystem::releaseLoadedFont(GfxRenderer& renderer) { manager_.unloadAll(renderer); }

bool SdCardFontSystem::adoptCompleteChineseNotoSans() {
#ifdef ENABLE_CHINESE_VERSION
  if (SETTINGS.contentProfile != CrossPointSettings::ContentProfile::China || SETTINGS.sdFontFamilyName[0] != '\0' ||
      !registry_.findFamily(COMPLETE_CHINESE_NOTO_SANS_FAMILY))
    return false;

  strncpy(SETTINGS.sdFontFamilyName, COMPLETE_CHINESE_NOTO_SANS_FAMILY, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
  SETTINGS.sdFontFlashPreload = 0;
  if (!SETTINGS.saveToFile()) {
    LOG_ERR("SDFS", "Failed to save automatic NotoSansSC selection");
  }
  LOG_INF("SDFS", "Using installed NotoSansSC in place of the Chinese built-in font");
  return true;
#else
  return false;
#endif
}

void SdCardFontSystem::setupUiFallbacks(GfxRenderer& renderer) {
#if defined(ENABLE_CHINESE_VERSION) && !(defined(SIMULATOR) && FREEINK_DEVICE_READPICO) && !defined(BOARD_HAS_PSRAM)
  // No-PSRAM firmware keeps only the reader size resident.
  (void)renderer;
  return;
#else
#if defined(BOARD_HAS_PSRAM)
  if (!memory::psramHasHeadroom(0, 0, 0)) return;
#endif
  const std::string& familyName = manager_.currentFamilyName();
  if (familyName.empty()) return;  // no SD family loaded — nothing to fall back to

  const auto* family = registry_.findFamily(familyName);
  if (!family) return;

  renderer.clearPreferredFonts();
#if !FREEINK_DEVICE_READPICO
  // Probe the reader face before loading additional UI sizes.
  const auto readerIt = renderer.getFontMap().find(manager_.getFontId(familyName));
  if (readerIt == renderer.getFontMap().end()) return;
#if defined(BOARD_HAS_PSRAM)
  // Match upstream: Han, kana, Hangul, Greek, Cyrillic, Hebrew, Arabic, Thai, Devanagari.
  static constexpr uint32_t kFallbackProbes[] = {0x4E00, 0x3042, 0x30A2, 0xAC00, 0x03B1,
                                                 0x0430, 0x05D0, 0x0627, 0x0E01, 0x0905};
#else
  static constexpr uint32_t kFallbackProbes[] = {0x4E00, 0x3042, 0x30A2, 0xAC00};
#endif
  bool needsFallback = false;
  for (const uint32_t cp : kFallbackProbes) {
    if (!readerIt->second.hasCodepoint(cp)) continue;
#if defined(BOARD_HAS_PSRAM)
    // A primary face covering the probe may still lack other glyphs in that script.
    needsFallback = true;
#else
    for (const auto& ui : kUiFontSizes) {
      const auto primaryIt = renderer.getFontMap().find(ui.fontId);
      if (primaryIt != renderer.getFontMap().end() && !primaryIt->second.hasCodepoint(cp)) {
        needsFallback = true;
        break;
      }
    }
#endif
    if (needsFallback) break;
  }
  if (!needsFallback) {
    LOG_DBG("SDFS", "%s adds no UI coverage - skipping fallback sizes", familyName.c_str());
    return;
  }

#endif

  for (size_t i = 0; i < std::size(kUiFontSizes); ++i) {
    const auto& ui = kUiFontSizes[i];
#if FREEINK_DEVICE_READPICO
    const uint8_t pointSize = kReadPicoUiPointSizes[i];
#else
    const uint8_t pointSize = ui.pointSize;
#endif
    const int exactId = manager_.loadFamilyExtraSize(*family, renderer, pointSize);
    if (exactId != 0) {
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
      if (i < 3) {
        // Keep interface typography; SD supplies names outside the matching CJK subset.
        renderer.setFallbackFont(ui.fontId, ui.builtinFallbackId, exactId);
        continue;
      }
#endif
#if FREEINK_DEVICE_READPICO
      // Read Pico renders its whole UI in the selected reading family at the enlarged
      // sizes above, so the SD face has to BE this id, not merely a per-glyph fallback.
      // A fallback-only mapping left pure-ASCII runs resolving to the built-in face
      // while CJK-bearing runs switched to the SD one, so a mixed row was measured in
      // one font and drawn in the other. setPreferredFont() puts every text and metric
      // path on the SD face; this id's own fontMap entry (the built-in UI font) becomes
      // the fallback for glyphs the SD face lacks, then the embedded CJK subset.
      renderer.setPreferredFont(ui.fontId, exactId);
      renderer.setFallbackFont(ui.fontId, ui.fontId, ui.builtinFallbackId);
#else
      renderer.setFallbackFont(ui.fontId, exactId, ui.builtinFallbackId);
#endif
    } else {
      // The family ships no face at this size, so the UI stays entirely on the built-in
      // fonts -- and the embedded CJK subset MUST still be registered as their fallback.
      // Registering nothing here is what blanked whole pages: a CJK string then had no
      // face with CJK glyphs anywhere in its chain, so every glyph drew as nothing.
      // Substituting a different SD size instead is deliberately not done: a family
      // without the UI sizes should fall back to the flashed fonts, not silently render
      // the UI at some other point size.
      renderer.setFallbackFont(ui.fontId, ui.builtinFallbackId, 0);
      LOG_DBG("SDFS", "No %u pt SD glyphs in %s - UI stays on the built-in fonts", pointSize, familyName.c_str());
    }
  }
#endif
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*pointSize*/) const {
#if CROSSPOINT_VECTOR_FONTS
  // A loaded vector (.ttf) family answers first — it isn't in the .cpfont manager.
  if (ttfFontId_ != 0 && familyName && ttfFamily_ == familyName) return ttfFontId_;
#endif
  // The manager holds exactly one reader-size font, already selected for
  // SETTINGS.fontPointSize, so the size argument is implicit — always return
  // that font's ID. ensureLoaded() must have run for the current settings first.
  return manager_.getFontId(familyName);
}

#if CROSSPOINT_VECTOR_FONTS

void SdCardFontSystem::freeTtfSources() {
  for (auto& s : ttfSources_) {
    s.bytes.clear();
    freeink::font::PsramVector<uint8_t>().swap(s.bytes);  // actually release
    if (s.file) s.file.close();
    s.streamed = false;
    s.size = 0;
    s.present = false;
  }
}

void SdCardFontSystem::unloadTtf(GfxRenderer& renderer) {
  if (ttfFamily_.empty() && ttfFontId_ == 0 && ttfUiIds_.empty()) return;
  // UI-size fallbacks first (they borrow ttfSources_).
  for (const int id : ttfUiIds_) {
    renderer.unregisterTtfFont(id);
    renderer.removeFont(id);
  }
  ttfUiIds_.clear();
  ttfUi_.clear();
#ifndef CROSSMUX_UI_PROFILE_HIGH_DPI
  renderer.clearFallbackFonts();
#endif
  // removeFont already removes references to each released TTF face; fixed UI mappings survive.
  if (ttfFontId_ != 0) {
    renderer.unregisterTtfFont(ttfFontId_);
    renderer.removeFont(ttfFontId_);  // drop from the renderer's fontMap
  }
  ttf_.reset();  // frees the FT faces first (they read ttfSources_)
  freeTtfSources();
  ttfFamily_.clear();
  ttfFontId_ = 0;
  ttfPointSize_ = 0;
}

bool SdCardFontSystem::openTtfSource(const uint8_t style, const std::string& path) {
  if (style >= 4) return false;
  // Small fonts are read fully into RAM (fastest, fewest SD reads; PSRAM when
  // present). Large fonts (e.g. multi-MB variable/CJK) STREAM from SD so the
  // whole file never sits in RAM — the handle is kept open for the font's life.
  // PSRAM boards only (this whole path is vector-font gated), so size the
  // resident cap for the 8MB parts: a 4.5MB variable font held resident gets
  // GPOS kerning (streamed faces skip it, and GPOS-only fonts like
  // Merriweather VF lose ALL kerning when streamed) and skips per-glyph SD
  // reads. The heap gate below still falls back to streaming when PSRAM
  // can't fund the buffer.
  static constexpr size_t kResidentMax = 6 * 1024 * 1024;
  // Working headroom that must remain in internal DRAM after a resident load
  // (FreeType face setup, glyph caches, and the rest of the system).
  static constexpr size_t kInternalHeadroom = 96 * 1024;
  HalFile f = Storage.open(path.c_str());
  if (!f) {
    LOG_ERR("SDFS", "Failed to open TTF: %s", path.c_str());
    return false;
  }
  const size_t len = f.size();
  if (len == 0) {
    LOG_ERR("SDFS", "Empty TTF: %s", path.c_str());
    f.close();
    return false;
  }
  TtfSource& s = ttfSources_[style];
  // A resident buffer lands in PSRAM when fiFontMalloc can place it there;
  // otherwise it competes with everything else in internal DRAM. PsramAlloc
  // aborts on OOM, so this gate is load-bearing on no-PSRAM boards (X4/C3):
  // fall back to streaming instead of attempting an allocation that can fail.
  bool resident = len <= kResidentMax;
  if (resident && heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < len) {
    const size_t internalFree = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (internalFree < len + kInternalHeadroom) {
      LOG_DBG("SDFS", "TTF %s (%u KB) too large for DRAM (largest block %u KB), streaming", path.c_str(),
              static_cast<unsigned>(len / 1024), static_cast<unsigned>(internalFree / 1024));
      resident = false;
    }
  }
  if (resident) {
    s.bytes.resize(len);
    const int got = f.read(s.bytes.data(), len);
    f.close();
    if (static_cast<size_t>(got) != len) {
      LOG_ERR("SDFS", "Short read on TTF %s (%d/%u)", path.c_str(), got, static_cast<unsigned>(len));
      s.bytes.clear();
      return false;
    }
    s.streamed = false;
  } else {
    s.file = std::move(f);  // kept open; prefixRead() reads it on demand
    s.streamed = true;
    // Cache the file's head in PSRAM: an sfnt's per-glyph-fault tables (cmap,
    // loca, hmtx) sit before the multi-MB glyf table, so serving the first
    // 1 MB from RAM turns each glyph fault's 4-6 scattered SD seeks into one
    // glyf read. Gated per source so a small-PSRAM board takes what fits.
    static constexpr size_t kStreamPrefix = 1024 * 1024;
    const size_t prefix = len < kStreamPrefix ? len : kStreamPrefix;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) > prefix + 256 * 1024) {
      s.bytes.resize(prefix);
      if (s.file.seek(0) && static_cast<size_t>(s.file.read(s.bytes.data(), prefix)) == prefix) {
        LOG_DBG("SDFS", "Cached %u KB TTF prefix in PSRAM", static_cast<unsigned>(prefix / 1024));
      } else {
        s.bytes.clear();  // fall back to pure streaming
      }
    }
    LOG_DBG("SDFS", "Streaming TTF %s (%u KB) from SD", path.c_str(), static_cast<unsigned>(len / 1024));
  }
  s.size = static_cast<unsigned long>(len);
  s.present = true;
  return true;
}

// Streamed-source read: serve from the PSRAM prefix cache when the range is
// there, hit SD only for the tail (glyf outlines). A read straddling the
// boundary splits across both.
unsigned long SdCardFontSystem::prefixRead(void* ctx, const unsigned long offset, unsigned char* buffer,
                                           const unsigned long count) {
  auto* s = static_cast<TtfSource*>(ctx);
  const unsigned long cached = s->bytes.size();
  if (offset < cached) {
    const unsigned long fromCache = (offset + count <= cached) ? count : cached - offset;
    if (count == 0) return 0;  // seek probe
    memcpy(buffer, s->bytes.data() + offset, fromCache);
    if (fromCache == count) return count;
    return fromCache +
           SdCardFontRegistry::halFileRead(&s->file, offset + fromCache, buffer + fromCache, count - fromCache);
  }
  return SdCardFontRegistry::halFileRead(&s->file, offset, buffer, count);
}

void SdCardFontSystem::addTtfSources(TtfEpdFont& font) {
  for (uint8_t st = 0; st < 4; ++st) {
    TtfSource& s = ttfSources_[st];
    if (!s.present) continue;
    if (s.streamed) {
      font.addStreamSource(st, &SdCardFontSystem::prefixRead, &s, s.size);
    } else {
      font.addResidentSource(st, s.bytes.data(), static_cast<uint32_t>(s.bytes.size()));
    }
  }
}

void SdCardFontSystem::setupTtfUiFallbacks(GfxRenderer& renderer) {
  if (ttfFamily_.empty()) return;
  // Small caches: UI strings (titles/rows) are short. Each UI family is 4-style
  // but LAZY, so only the regular face is ever built for UI text — the bold/
  // italic faces cost nothing. All faces share the reader's sources (streamed
  // handles or resident bytes), so no extra copy of any font file.
  // Each fallback instance carries its own FreeType face and lazy glyph
  // arena. Without PSRAM those compete with the reader's section build for
  // internal DRAM, and the build must win: below this floor, skip the
  // fallback (built-in bitmap UI fonts keep covering Latin UI text).
  static constexpr size_t kUiFallbackMinInternalHeap = 160 * 1024;
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
  ttfUi_.reserve(std::size(kUiFontSizes));
  ttfUiIds_.reserve(std::size(kUiFontSizes));
#endif
  for (const auto& ui : kUiFontSizes) {
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
    const int id = computeTtfFontId((ttfFamily_ + "\x01ui").c_str(), ui.pointSize);
    // UI and footer share 12/14pt faces; register and own each size only once.
    if (renderer.getFontMap().count(id) != 0) {
      renderer.setFallbackFont(ui.fontId, ui.builtinFallbackId, id);
      continue;
    }
#endif
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) == 0) {
      const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      if (internalFree < kUiFallbackMinInternalHeap) {
        LOG_DBG("SDFS", "Skipping TTF UI fallback @%upt (%u KB internal free)", ui.pointSize,
                static_cast<unsigned>(internalFree / 1024));
        continue;
      }
    }
    auto f = makeUniqueNoThrow<TtfEpdFont>();
    if (!f) {
      LOG_ERR("SDFS", "OOM: TtfEpdFont for UI fallback @%upt", ui.pointSize);
      continue;  // built-in bitmap UI fonts keep covering this size
    }
    addTtfSources(*f);
    const bool ok = f->load(ui.pointSize, /*twoBit=*/true, /*glyphCacheBytes=*/16 * 1024, /*maxGlyphs=*/384);
    if (!ok) continue;
    LOG_DBG("SDFS", "TTF UI fallback @%upt loaded (heap free %u)", ui.pointSize, (unsigned)ESP.getFreeHeap());
    // Distinct id from the reader-size font: a UI size can equal the reader size
    // (e.g. both 12pt), which would collide on computeTtfFontId and be dropped
    // as a duplicate. Salt the UI family name to separate the id spaces.
#ifndef CROSSMUX_UI_PROFILE_HIGH_DPI
    const int id = computeTtfFontId((ttfFamily_ + "\x01ui").c_str(), ui.pointSize);
#endif
    renderer.insertFont(id, f->family());
    renderer.registerTtfFont(id, f.get());
#ifdef CROSSMUX_UI_PROFILE_HIGH_DPI
    renderer.setFallbackFont(ui.fontId, ui.builtinFallbackId, id);
#else
    renderer.setFallbackFont(ui.fontId, id);
#endif
    ttfUiIds_.push_back(id);
    ttfUi_.push_back(std::move(f));
  }
}

void SdCardFontSystem::loadTtfFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                     const bool registryWasDirty) {
  // Keep sizes inherited from other families within the selectable vector range.
  snapFontPointSizeTo(
      snapToNearestPointSize(VECTOR_READER_POINT_SIZES, std::size(VECTOR_READER_POINT_SIZES), SETTINGS.fontPointSize));
  const uint8_t size = SETTINGS.fontPointSize;

  // Already loaded, same family + size, and disk unchanged → nothing to do.
  if (!registryWasDirty && ttf_ && ttfFamily_ == family.name && ttfPointSize_ == size) return;

  // Reader-face glyph-cache budget (used by both the resize fast path and the
  // full load below): the default 32 KB holds ~90 CJK glyphs, but a CJK page
  // uses 300+, so the cache flush-cycles mid-page and every page turn
  // re-rasterizes the whole page through streamed SD reads (multi-second
  // turns). The arenas are PSRAM-backed (FontPsram); 1 MB / 4096 glyphs holds
  // a whole Japanese novel's working set (~3000 unique kanji+kana at ~350 B
  // each), so the flush-everything ceiling is never hit and warm page turns
  // are pure cache hits. Without PSRAM keep the internal-DRAM-safe default.
  //
  const bool havePsram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) > 0;
  // 面板支持 16 级时，正文矢量字面按 4 位覆盖度加载：FreeType 本来就算 8 位，压成 2 位等于
  // 把抗锯齿的层次丢掉。4 位只对 16 级通路有意义（那条路用 drawGrayscale16Pixel 直接把
  // 覆盖度写进 4bpp 缓冲），所以按面板能力决定，别的目标仍然是 2 位。
  // / When the panel does 16 levels, load the reader's vector face with 4-bit coverage:
  // FreeType already computes 8 bits, and packing to 2 discards the anti-aliasing depth.
  const bool fourBit = renderer.getGrayscaleLevels() == 16;
  const size_t cacheBytes = havePsram ? 1024 * 1024 : 32 * 1024;
  const uint16_t maxGlyphs = havePsram ? 4096 : 768;

  // Same family, only the reader size changed (size preview): the open style
  // sources and the size-independent UI fallbacks don't need rebuilding — just
  // re-drive the reader face at the new size, reusing the already-open files
  // instead of reopening all four and rebuilding every UI fallback.
  // 上面已按面板能力决定 fourBit；这里直接用它驱动加载。
  // / fourBit is decided above, next to the cache budget it also sizes.
  if (!registryWasDirty && ttf_ && ttfFamily_ == family.name) {
    renderer.unregisterTtfFont(ttfFontId_);
    renderer.removeFont(ttfFontId_);
    if (ttf_->load(size, /*twoBit=*/true, cacheBytes, maxGlyphs, fourBit)) {
      ttf_->build(" ");
      ttfFontId_ = computeTtfFontId(family.name.c_str(), size);
      renderer.insertFont(ttfFontId_, ttf_->family());
      renderer.registerTtfFont(ttfFontId_, ttf_.get());
      ttfPointSize_ = size;
      ttfFourBit_ = fourBit;
      return;
    }
    // Resize failed: fall through to a clean full reload.
  }

  unloadTtf(renderer);

  if (family.files.empty()) {
    LOG_ERR("SDFS", "Vector family %s has no file", family.name.c_str());
    SETTINGS.clearSdFontFamily();
    return;
  }

  // Open each style source the family ships (0=regular, 1=bold, 2=italic,
  // 3=bold-italic). A single-file family (loose .ttf, or a folder with one file)
  // supplies only regular; TtfEpdFont then derives bold/italic from the wght axis
  // or an oblique shear. Extra files upgrade those styles to the real designs.
  for (const auto& file : family.files) {
    const uint8_t role = file.style < 4 ? file.style : 0;
    if (ttfSources_[role].present) continue;  // registry already deduped by role
    openTtfSource(role, file.path);
  }
  if (!ttfSources_[0].present) {
    // Possibly a transient SD read failure: keep the user's selection so the
    // next ensureLoaded() retries; this session falls back to the built-in.
    LOG_ERR("SDFS", "Vector family %s: regular file failed to open (keeping selection)", family.name.c_str());
    freeTtfSources();
    return;
  }

  ttf_ = makeUniqueNoThrow<TtfEpdFont>();
  if (!ttf_) {
    // Transient OOM: keep the user's selection (unlike a parse failure) so the
    // next ensureLoaded() can retry once heap pressure passes.
    LOG_ERR("SDFS", "OOM: TtfEpdFont for %s", family.name.c_str());
    freeTtfSources();
    return;
  }
  addTtfSources(*ttf_);
  const bool ok = ttf_->load(size, /*twoBit=*/true, cacheBytes, maxGlyphs, fourBit);
  if (!ok) {
    // init failure is ambiguous (corrupt font vs. transient OOM inside
    // FreeType): keep the selection and retry next ensureLoaded() rather than
    // silently reverting the user to the built-in font. A genuinely broken
    // font costs one failed load per reader entry, visible in the log.
    LOG_ERR("SDFS", "FreeInkFont could not parse %s (keeping selection)", family.name.c_str());
    ttf_.reset();
    freeTtfSources();
    return;
  }
  // Seed the regular face's glyph cache; other styles + glyphs fault on demand.
  ttf_->build(" ");
  ttfFourBit_ = fourBit;

  ttfFontId_ = computeTtfFontId(family.name.c_str(), size);
  renderer.insertFont(ttfFontId_, ttf_->family());
  renderer.registerTtfFont(ttfFontId_, ttf_.get());
  ttfFamily_ = family.name;
  ttfPointSize_ = size;
  LOG_DBG("SDFS", "Reader TTF face loaded (heap free %u, max block %u)", (unsigned)ESP.getFreeHeap(),
          (unsigned)ESP.getMaxAllocHeap());
  setupTtfUiFallbacks(renderer);  // CJK/script UI fallback at the built-in UI sizes
  LOG_DBG("SDFS", "Loaded TTF font: %s @ %upt (id %d)", family.name.c_str(), size, ttfFontId_);
}

#endif  // CROSSPOINT_VECTOR_FONTS
