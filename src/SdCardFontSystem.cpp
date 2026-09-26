#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "fontIds.h"

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

#if !defined(ENABLE_CHINESE_VERSION) || \
    (CONFIG_IDF_TARGET_ESP32S3 && defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED))
// Physical point sizes at 150 DPI, matching the SD-font converter.
struct UiFontSize {
  int fontId;
  uint8_t pointSize;
  int builtinFallbackId;
};
constexpr UiFontSize kUiFontSizes[] = {
    {SMALL_FONT_ID, 8, CJK_UI_8_FONT_ID},
    {UI_10_FONT_ID, 10, CJK_UI_10_FONT_ID},
    {UI_12_FONT_ID, 12, CJK_UI_12_FONT_ID},
};
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
  const std::string& currentFamily = manager_.currentFamilyName();
  const bool preferFlash = allowFlashCache && SETTINGS.sdFontFlashPreload != 0;

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    // Back on a built-in family, which exists only at BUILTIN_READER_POINT_SIZES:
    // a size inherited from an SD family has to come back into that set.
    snapFontPointSizeTo(snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES),
                                               SETTINGS.fontPointSize));
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
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) return;
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
#if defined(ENABLE_CHINESE_VERSION) && \
    !(CONFIG_IDF_TARGET_ESP32S3 && defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED))
  // No-PSRAM firmware keeps only the reader size resident.
  (void)renderer;
  return;
#else
#if CONFIG_IDF_TARGET_ESP32S3 && defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  if (!memory::psramHasHeadroom(0, 0, 0)) return;
#endif
  const std::string& familyName = manager_.currentFamilyName();
  if (familyName.empty()) return;  // no SD family loaded — nothing to fall back to

  const auto* family = registry_.findFamily(familyName);
  if (!family) return;

  // Probe the reader face before loading additional UI sizes.
  const auto readerIt = renderer.getFontMap().find(manager_.getFontId(familyName));
  if (readerIt == renderer.getFontMap().end()) return;
#if CONFIG_IDF_TARGET_ESP32S3 && defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
  // Match upstream: Han, kana, Hangul, Greek, Cyrillic, Hebrew, Arabic, Thai, Devanagari.
  static constexpr uint32_t kFallbackProbes[] = {0x4E00, 0x3042, 0x30A2, 0xAC00, 0x03B1,
                                                 0x0430, 0x05D0, 0x0627, 0x0E01, 0x0905};
#else
  static constexpr uint32_t kFallbackProbes[] = {0x4E00, 0x3042, 0x30A2, 0xAC00};
#endif
  bool needsFallback = false;
  for (const uint32_t cp : kFallbackProbes) {
    if (!readerIt->second.hasCodepoint(cp)) continue;
#if CONFIG_IDF_TARGET_ESP32S3 && defined(BOARD_HAS_PSRAM) && !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED)
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

  for (const auto& ui : kUiFontSizes) {
    const int sdFontId = manager_.loadFamilyExtraSize(*family, renderer, ui.pointSize);
    if (sdFontId != 0) {
      renderer.setFallbackFont(ui.fontId, sdFontId, ui.builtinFallbackId);
    } else {
      LOG_DBG("SDFS", "No %u pt SD glyphs for UI fallback in %s", ui.pointSize, familyName.c_str());
    }
  }
#endif
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*pointSize*/) const {
  // The manager holds exactly one reader-size font, already selected for
  // SETTINGS.fontPointSize, so the size argument is implicit — always return
  // that font's ID. ensureLoaded() must have run for the current settings first.
  return manager_.getFontId(familyName);
}
