#pragma once

#include <BoardConfig.h>
#include <HalFrontlight.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"
#include "activities/settings/SettingsActivity.h"
#include "util/DictionaryRegistry.h"

// Build the font family setting dynamically. SD-card rows follow whichever
// built-in rows are visible in this firmware.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  std::vector<StrId> enumValues;
#ifdef ENABLE_CHINESE_VERSION
  if (!registry || !registry->findFamily(SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY)) {
    enumValues.push_back(StrId::STR_NOTO_SANS);
  }
#else
  enumValues = {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS};
#endif
  const int builtinOptionCount = static_cast<int>(enumValues.size());

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-family entry it replaces

  if (registry && registry->getFamilyCount() > 0) {
    const auto& families = registry->getFamilies();
    s.enumStringValues.reserve(builtinOptionCount + families.size());
    for (const StrId value : enumValues) s.enumStringValues.push_back(I18N.get(value));
    std::transform(families.begin(), families.end(), std::back_inserter(s.enumStringValues),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  } else {
    s.enumValues = std::move(enumValues);
  }

  // The global SdCardFontSystem owns the registry for the lifetime of every
  // settings consumer, so referencing it avoids duplicating every family name.
  s.valueGetter = [registry, builtinOptionCount]() -> uint8_t {
    // If an SD card font is selected, find its index
    if (registry && SETTINGS.sdFontFamilyName[0] != '\0') {
      const auto& families = registry->getFamilies();
      for (int i = 0; i < static_cast<int>(families.size()); i++) {
        if (families[i].name == SETTINGS.sdFontFamilyName) {
          return static_cast<uint8_t>(builtinOptionCount + i);
        }
      }
      // SD font name not found in registry — fall through to built-in
    }
#ifdef ENABLE_CHINESE_VERSION
    return 0;
#else
    return SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
#endif
  };

  s.valueSetter = [registry, builtinOptionCount](uint8_t v) {
    if (v < builtinOptionCount) {
#ifdef ENABLE_CHINESE_VERSION
      SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
#else
      SETTINGS.fontFamily = v;
#endif
      SETTINGS.sdFontFamilyName[0] = '\0';
      SETTINGS.sdFontFlashPreload = 0;
    } else if (registry) {
      int sdIdx = v - builtinOptionCount;
      const auto& families = registry->getFamilies();
      if (sdIdx < static_cast<int>(families.size())) {
        strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
        SETTINGS.sdFontFlashPreload = 0;
      }
    }
  };

  return s;
}

// Build the font size setting dynamically: the options are the point sizes the
// active family actually ships, so an SD family built at 10/12/14 offers three
// sizes and a family built at 8..18 offers six. The selected point size persists
// in SETTINGS.fontPointSize (saved/loaded manually in CrossPointSettings::
// toJson/fromJson — the generic loop skips dynamic entries), while the ENUM
// contract shared with the web UI stays index-based.
inline SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry) {
  // Captured by copy: getSettingsList() returns by value and the lambdas outlive
  // this call, so they must not reference the registry.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated — see the matching note in
  // TextSettingsActivity::rebuildSizeList().
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (const uint8_t pt : sizes) {
    labels.push_back(std::to_string(pt) + " pt");
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "fontSize";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-size entry it replaces

  s.valueGetter = [sizes]() -> uint8_t {
    const uint8_t pt = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
    for (int i = 0; i < static_cast<int>(sizes.size()); i++) {
      if (sizes[i] == pt) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [sizes](uint8_t v) {
    if (v < sizes.size()) SETTINGS.fontPointSize = sizes[v];
  };

  return s;
}

// Build the dictionary selection setting dynamically from the folders discovered
// under /dictionaries. "None" plus one option per dictionary; the selected folder
// name persists in SETTINGS.dictionaryName (saved/loaded manually in
// CrossPointSettings::toJson/fromJson — the generic loop skips dynamic entries).
inline SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>& dictionaries) {
  std::vector<std::string> folderNames;
  folderNames.reserve(dictionaries.size());
  std::transform(dictionaries.begin(), dictionaries.end(), std::back_inserter(folderNames),
                 [](const DictionaryEntry& d) { return d.name; });

  SettingInfo s;
  s.nameId = StrId::STR_DICTIONARY;
  s.type = SettingType::ENUM;
  s.enumStringValues.reserve(folderNames.size() + 1);
  s.enumStringValues.push_back(I18N.get(StrId::STR_NONE_OPT));
  s.enumStringValues.insert(s.enumStringValues.end(), folderNames.begin(), folderNames.end());
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [folderNames]() -> uint8_t {
    for (size_t i = 0; i < folderNames.size(); i++) {
      // Compare within the settings field capacity: an over-long folder name is
      // stored truncated, and must still match its list entry.
      if (strncmp(folderNames[i].c_str(), SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName) - 1) == 0) {
        return static_cast<uint8_t>(i + 1);
      }
    }
    return 0;  // "None", also when the stored folder no longer exists
  };

  s.valueSetter = [folderNames](uint8_t v) {
    if (v == 0 || v > folderNames.size()) {
      SETTINGS.dictionaryName[0] = '\0';
      return;
    }
    strncpy(SETTINGS.dictionaryName, folderNames[v - 1].c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  };

  return s;
}

inline std::vector<StrId> buildLongPressMenuValues() {
  static constexpr StrId VALUES[] = {StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION,
                                     StrId::STR_DICTIONARY, StrId::STR_READER_MENU};
  const size_t count = BoardConfig::hasHomeKey() ? std::size(VALUES) : std::size(VALUES) - 1;
  return {VALUES, VALUES + count};
}

// Shared base settings list used by the device UI, persistence, and web API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
inline const std::vector<SettingInfo>& getBaseSettingsList() {
  static const std::vector<SettingInfo> baseList = [] {
    // Enum settings are persisted as numeric values. Assign these labels by enum
    // value so a reordered menu or enum cannot silently swap their behavior.
    std::vector<StrId> sleepScreenValues(CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
    sleepScreenValues[CrossPointSettings::DARK] = StrId::STR_DARK;
    sleepScreenValues[CrossPointSettings::LIGHT] = StrId::STR_LIGHT;
    sleepScreenValues[CrossPointSettings::CUSTOM] = StrId::STR_CUSTOM;
    sleepScreenValues[CrossPointSettings::COVER] = StrId::STR_COVER;
    sleepScreenValues[CrossPointSettings::COVER_CUSTOM] = StrId::STR_COVER_CUSTOM;
    sleepScreenValues[CrossPointSettings::BLANK] = StrId::STR_NONE_OPT;
    sleepScreenValues[CrossPointSettings::QUICK_RESUME] = StrId::STR_QUICK_RESUME;
    sleepScreenValues[CrossPointSettings::TRANSPARENT] = StrId::STR_TRANSPARENT;

    std::vector<StrId> statusBarClockValues(CrossPointSettings::STATUS_BAR_CLOCK_MODE_COUNT);
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_HIDE] = StrId::STR_HIDE;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_RIGHT] = StrId::STR_DIR_RIGHT;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_LEFT] = StrId::STR_DIR_LEFT;

    std::vector<SettingInfo> v = {
        // --- Display ---
        SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen, std::move(sleepScreenValues),
                          "sleepScreen", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                          {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                          {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                          "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_STANDBY_TITLE, &CrossPointSettings::standbyShortcutEnabled,
                            "standbyShortcutEnabled", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                          {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                          {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                           StrId::STR_PAGES_30, StrId::STR_NEVER},
                          "refreshFrequency", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme,
                          {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                           StrId::STR_THEME_ROUNDEDRAFF, StrId::STR_THEME_LYRA_CAROUSEL, StrId::STR_THEME_INX},
                          "uiTheme", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_INX_RECENT_LAYOUT, &CrossPointSettings::inxRecentLayout,
                          {StrId::STR_LAYOUT_FLOW, StrId::STR_LAYOUT_GRID, StrId::STR_LAYOUT_LIST,
                           StrId::STR_LAYOUT_ICONS, StrId::STR_COVER},
                          "inxRecentLayout", StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_INX_LIBRARY_LAYOUT, &CrossPointSettings::inxLibraryLayout,
                          {StrId::STR_LAYOUT_ICONS, StrId::STR_LAYOUT_LIST}, "inxLibraryLayout",
                          StrId::STR_CAT_DISPLAY),
        SettingInfo::Enum(StrId::STR_INX_APPS_LAYOUT, &CrossPointSettings::inxAppsLayout,
                          {StrId::STR_LAYOUT_ICONS, StrId::STR_LAYOUT_LIST}, "inxAppsLayout", StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                            StrId::STR_CAT_DISPLAY),
        SettingInfo::Toggle(StrId::STR_SHOW_BUTTON_HINTS, &CrossPointSettings::showButtonHints, "showButtonHints",
                            StrId::STR_CAT_DISPLAY),
#if FREEINK_CAP_FRONTLIGHT
        SettingInfo::Toggle(StrId::STR_RESTORE_LIGHT_ON_WAKE, &CrossPointSettings::frontlightRestoreOnWake,
                            "frontlightRestoreOnWake", StrId::STR_CAT_DISPLAY),
#endif
        // Night mode = inverted output polarity everywhere (ActivityManager
        // applies it to every activity), so it lives in the Display category.
        SettingInfo::Toggle(StrId::STR_NIGHT_MODE, &CrossPointSettings::screenInverted, "screenInverted",
                            StrId::STR_CAT_DISPLAY),

        // --- Reader ---
        // Built-in font-family entry. Replaced per-call with a registry-aware
        // version when SD fonts are installed.
        SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                          {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER)
            .withTextSettings(),
        // Placeholder: the selectable sizes depend on the active font family, so
        // this entry is always replaced by buildFontSizeSetting() below. It only
        // fixes the setting's position in the Reader category.
        SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER).withTextSettings(),
        SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                          {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE}, "lineSpacing",
                          StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin,
                           {CrossPointSettings::SCREEN_MARGIN_MIN, CrossPointSettings::SCREEN_MARGIN_MAX,
                            CrossPointSettings::SCREEN_MARGIN_STEP},
                           "screenMargin", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                          {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                           StrId::STR_BOOK_S_STYLE},
                          "paragraphAlignment", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_FAKE_BOLD, &CrossPointSettings::fakeBold,
                          {StrId::STR_STATE_OFF, StrId::STR_FAKE_BOLD_LIGHT, StrId::STR_FAKE_BOLD_STANDARD,
                           StrId::STR_FAKE_BOLD_HEAVY},
                          "fakeBold", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled, "focusReadingEnabled",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_READING_BACKGROUND, &CrossPointSettings::readingBackgroundEnabled,
                            "readingBackgroundEnabled", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_READING_GUIDE_LINE, &CrossPointSettings::readingGuideLineEnabled,
                            "readingGuideLineEnabled", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_READING_GUIDE_LINE_STYLE, &CrossPointSettings::readingGuideLineStyle,
                          {StrId::STR_SOLID_LINE, StrId::STR_SHORT_DASH, StrId::STR_MEDIUM_DASH, StrId::STR_LONG_DASH,
                           StrId::STR_DOTTED_LINE, StrId::STR_WAVY_LINE},
                          "readingGuideLineStyle", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::SignedValue(
            StrId::STR_READING_GUIDE_LINE_OFFSET, &CrossPointSettings::readingGuideLineOffset,
            {CrossPointSettings::READING_GUIDE_LINE_OFFSET_MIN, CrossPointSettings::READING_GUIDE_LINE_OFFSET_MAX, 1},
            "readingGuideLineOffset", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled, "hyphenationEnabled",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(
            StrId::STR_ORIENTATION, &CrossPointSettings::orientation,
            {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW},
            "orientation", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                            "extraParagraphSpacing", StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Toggle(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing, "textAntiAliasing",
                            StrId::STR_CAT_READER)
            .withTextSettings(),
        SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                          {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                          "imageRendering", StrId::STR_CAT_READER),
        SettingInfo::Toggle(StrId::STR_NIGHT_MODE, &CrossPointSettings::screenInverted, "screenInverted",
                            StrId::STR_CAT_READER),
        SettingInfo::Enum(StrId::STR_READER_MENU_STYLE, &CrossPointSettings::readerMenuStyle,
                          {StrId::STR_MENU_STYLE_LIST, StrId::STR_MENU_STYLE_TOOLBAR}, "readerMenuStyle",
                          StrId::STR_CAT_READER),
        // --- Controls ---
        SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                          {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED}, "sideButtonLayout",
                          StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(
            StrId::STR_TOUCH_READER_CONTROLS, &CrossPointSettings::touchReaderControls,
            {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE, StrId::STR_STATE_INVERTED_TAP},
            "touchReaderControls", StrId::STR_CAT_CONTROLS),
        // Persisted under the legacy "tapForReaderMenu" key: old saves map
        // 0 = Off, 1 = Tap.
        SettingInfo::Enum(StrId::STR_SHOW_READER_MENU, &CrossPointSettings::showReaderMenu,
                          {StrId::STR_STATE_OFF, StrId::STR_STATE_TAP, StrId::STR_STATE_SWIPE_UP}, "tapForReaderMenu",
                          StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, &CrossPointSettings::frontButtonFollowOrientation,
                            "frontButtonFollowOrientation", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                          {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                           StrId::STR_LONG_PRESS_BEHAVIOR_ORIENTATION},
                          "longPressButtonBehavior", StrId::STR_CAT_CONTROLS),
        SettingInfo::Enum(StrId::STR_LONG_PRESS_MENU, &CrossPointSettings::longPressMenuFunction,
                          buildLongPressMenuValues(), "longPressMenuFunction", StrId::STR_CAT_CONTROLS),
#if FREEINK_CAP_TOUCH
        SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                          {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH,
                           StrId::STR_FOOTNOTES, StrId::STR_CONFIRM},
                          "shortPwrBtn", StrId::STR_CAT_CONTROLS),
#else
        SettingInfo::Enum(
            StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
            {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH, StrId::STR_FOOTNOTES},
            "shortPwrBtn", StrId::STR_CAT_CONTROLS),
#endif
        SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack,
                            "pwrBtnFootnoteBack", StrId::STR_CAT_CONTROLS),
        SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &CrossPointSettings::backShortToFileBrowser,
                            "backShortToFileBrowser", StrId::STR_CAT_CONTROLS),

        // --- System ---
        SettingInfo::Value(
            StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
            {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
            "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles, "showHiddenFiles",
                            StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS, &CrossPointSettings::removeReadBooksFromRecents,
                            "removeReadBooksFromRecents", StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &CrossPointSettings::moveFinishedToReadFolder,
                            "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM),
        // Reading Analytics suite
        SettingInfo::Enum(StrId::STR_DAILY_GOAL, &CrossPointSettings::dailyGoalTarget,
                          {StrId::STR_MIN_15, StrId::STR_MIN_30, StrId::STR_MIN_45, StrId::STR_MIN_60},
                          "dailyGoalTarget", StrId::STR_CAT_READER)
            .withReadingStatsSettings(),
        SettingInfo::Toggle(StrId::STR_ENABLE_ACHIEVEMENTS, &CrossPointSettings::achievementsEnabled,
                            "achievementsEnabled", StrId::STR_CAT_READER)
            .withReadingStatsSettings(),
        SettingInfo::Toggle(StrId::STR_ACHIEVEMENT_POPUPS, &CrossPointSettings::achievementPopups, "achievementPopups",
                            StrId::STR_CAT_READER)
            .withReadingStatsSettings(),

        // OPDS download folder: persisted + web-exposed, but category-less so it
        // is hidden from the on-device Settings screen (edited via OPDS UI).
        SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                            sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"),
        // OPDS download filename format: persisted + web-exposed, category-less so it
        // is hidden from the on-device Settings screen (cycled from the OPDS UI).
        SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                          {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR, StrId::STR_FMT_TITLE},
                          "opdsFilenameFormat"),

        // Frontlight quick-panel state is persisted and web-exposed, but the
        // panel owns its on-device editing UI.
        SettingInfo::Value(StrId::STR_BRIGHTNESS, &CrossPointSettings::frontlightBrightness, {0, 100, 5},
                           "frontlightBrightness"),
#if FREEINK_CAP_WARMLIGHT
        SettingInfo::Value(StrId::STR_WARMTH, &CrossPointSettings::frontlightWarmth, {0, 100, 5}, "frontlightWarmth"),
#endif
        SettingInfo::Toggle(StrId::STR_FRONTLIGHT, &CrossPointSettings::frontlightOn, "frontlightOn"),

        // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(v, KOREADER_STORE.getPassword());
              KOREADER_STORE.saveToFile();
            },
            "koUsername", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
            [](const std::string& v) {
              KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), v);
              KOREADER_STORE.saveToFile();
            },
            "koPassword", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicString(
            StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
            [](const std::string& v) {
              KOREADER_STORE.setServerUrl(v);
              KOREADER_STORE.saveToFile();
            },
            "koServerUrl", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
            [](uint8_t v) {
              KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(v));
              KOREADER_STORE.saveToFile();
            },
            "koMatchMethod", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_SEND_METADATA, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSendMetadata()); },
            [](uint8_t v) {
              KOREADER_STORE.setSendMetadata(v != 0);
              KOREADER_STORE.saveToFile();
            },
            "koSendMetadata", StrId::STR_KOREADER_SYNC),
        SettingInfo::DynamicEnum(
            StrId::STR_SYNC_BEHAVIOR, {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC},
            [] { return static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior()); },
            [](uint8_t v) {
              KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(v));
              KOREADER_STORE.saveToFile();
            },
            "koSyncBehavior", StrId::STR_KOREADER_SYNC),
        // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
        SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                            "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE, &CrossPointSettings::statusBarBookProgressPercentage,
                            "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS, &CrossPointSettings::statusBarProgressBarThickness,
                          {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM, StrId::STR_PROGRESS_BAR_THICK},
                          "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                          {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                            StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Enum(StrId::STR_XTC_STATUS_BAR, &CrossPointSettings::xtcStatusBarMode,
                          {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP}, "xtcStatusBarMode",
                          StrId::STR_CUSTOMISE_STATUS_BAR),
        // Clock entries (web settings; device UI groups them under Date & Time).
        // Range 0..104 = quarter-hour steps from UTC-12:00 to UTC+14:00, biased by 48.
        SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock, std::move(statusBarClockValues),
                          "statusBarClock", StrId::STR_CUSTOMISE_STATUS_BAR),
        SettingInfo::Value(StrId::STR_CLOCK_UTC_OFFSET, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1},
                           "clockUtcOffsetQ", StrId::STR_CAT_SYSTEM),
        SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                          {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                          StrId::STR_CAT_SYSTEM),
        SettingInfo::Toggle(StrId::STR_AUTO_TIME, &CrossPointSettings::clockAutoSync, "clockAutoSync",
                            StrId::STR_CAT_SYSTEM),
    };
    // Only show tilt page turn setting when the QMI8658 IMU is present (X3)
    if (halTiltSensor.isAvailable()) {
      // Insert after the short power button setting (end of Controls section)
      for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->nameId == StrId::STR_SHORT_PWR_BTN) {
          v.insert(it + 1, SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                                             {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED},
                                             "tiltPageTurn", StrId::STR_CAT_CONTROLS));
          break;
        }
      }
    }
    return v;
  }();

  return baseList;
}

inline bool isSettingAvailableOnBoard(const SettingInfo& setting) {
  if (!BoardConfig::hasTouch() && setting.nameId == StrId::STR_TOUCH_READER_CONTROLS) return false;
  if (!BoardConfig::hasHomeKey() && setting.nameId == StrId::STR_SHOW_READER_MENU) return false;
  const bool frontlightSetting = setting.valuePtr == &CrossPointSettings::frontlightBrightness ||
                                 setting.valuePtr == &CrossPointSettings::frontlightOn ||
                                 setting.valuePtr == &CrossPointSettings::frontlightRestoreOnWake
#if FREEINK_CAP_WARMLIGHT
                                 || setting.valuePtr == &CrossPointSettings::frontlightWarmth
#endif
      ;
#if defined(SIMULATOR)
  if (frontlightSetting) return false;
#else
  if (frontlightSetting && !Frontlight.present()) return false;
#if FREEINK_CAP_WARMLIGHT
  if (setting.valuePtr == &CrossPointSettings::frontlightWarmth && !Frontlight.hasColorTemperature()) return false;
#endif
#endif
  if (!BoardConfig::hasTouch()) return true;
  return setting.nameId != StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION &&
         setting.nameId != StrId::STR_SUNLIGHT_FADING_FIX && setting.nameId != StrId::STR_SHOW_BUTTON_HINTS &&
         setting.nameId != StrId::STR_BACK_SHORT_TO_FILE_BROWSER;
}

// Visits the shared list without copying it. Dynamic font entries are built
// only while their callback runs, keeping the web request's peak heap bounded.
template <typename Callback>
inline void forEachSettingsListEntry(const SdCardFontRegistry* registry, Callback&& callback) {
  for (const auto& setting : getBaseSettingsList()) {
    if (!isSettingAvailableOnBoard(setting)) continue;

    if (setting.nameId == StrId::STR_FONT_FAMILY) {
      const SettingInfo dynamicSetting = buildFontFamilySetting(registry);
      callback(dynamicSetting);
    } else if (setting.nameId == StrId::STR_FONT_SIZE) {
      const SettingInfo dynamicSetting = buildFontSizeSetting(registry);
      callback(dynamicSetting);
    } else {
      callback(setting);
    }
  }
}

// Device settings need an owned list because they retain category entries and
// may insert a dynamically discovered dictionary entry.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr,
                                                const std::vector<DictionaryEntry>* dictionaries = nullptr) {
  const auto& baseList = getBaseSettingsList();

  std::vector<SettingInfo> v = baseList;
  v.erase(std::remove_if(v.begin(), v.end(), [](const SettingInfo& s) { return !isSettingAvailableOnBoard(s); }),
          v.end());
  {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_FAMILY; });
    if (it != v.end()) {
      *it = buildFontFamilySetting(registry);
    }
  }
  {
    // Unconditional: even with no SD fonts installed the sizes come from the
    // built-in family rather than a fixed Small/Medium/Large/XL enum.
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_SIZE; });
    if (it != v.end()) {
      *it = buildFontSizeSetting(registry);
    }
  }
  if (dictionaries && !dictionaries->empty()) {
    // Insert at the end of the Reader category (just before the first Controls entry).
    auto it =
        std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.category == StrId::STR_CAT_CONTROLS; });
    v.insert(it, buildDictionarySetting(*dictionaries));
  }
  return v;
}
