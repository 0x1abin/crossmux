#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
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
#include "HomeButtonSettings.h"
#include "KOReaderCredentialStore.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"
#include "activities/settings/SettingsActivity.h"
#include "components/UITheme.h"
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
    // 矢量字体（.ttf/.otf/.ttc）是运行时光栅化的，任意字号都能显示；其余是预烤好的
    // .cpfont 字集。给矢量家族加个标记，让字体选择器一眼看出哪个是哪种。
    //
    // 这只影响显示：持久化的是选项下标，而且下面 valueGetter 是按 families[i].name
    // 匹配的，不是按这个字符串 —— 所以追加标记不可能改变存进去的值。
    // / Vector families (.ttf/.otf/.ttc) are rasterized at runtime and can be shown at any
    // size; the rest are pre-rasterized .cpfont sets. Tag the vector ones so the picker says
    // which is which. Display only -- what gets persisted is the option index, and
    // valueGetter below matches on families[i].name rather than on this string, so
    // appending a marker cannot change the stored value.
    std::transform(families.begin(), families.end(), std::back_inserter(s.enumStringValues),
                   [](const SdCardFontFamilyInfo& f) {
                     return f.vector ? f.name + "  " + I18N.get(StrId::STR_VECTOR_FONT_TAG) : f.name;
                   });
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
  s.key = "dictionaryName";  // web settings API; persisted by name, not by the generic loop
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
  std::vector<StrId> values = {StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION,
                               StrId::STR_DICTIONARY};
  values.reserve(CrossPointSettings::LONG_PRESS_MENU_FUNCTION_COUNT);
  if (BoardConfig::hasHomeKey()) values.push_back(StrId::STR_READER_MENU);
  values.push_back(StrId::STR_SAVE_CLIPPING);
  return values;
}

inline uint8_t longPressMenuDisplayValue() {
  const uint8_t raw = SETTINGS.longPressMenuFunction;
  if (raw <= CrossPointSettings::LP_MENU_DICTIONARY) return raw;
  if (raw == CrossPointSettings::LP_MENU_READER_MENU) {
    return BoardConfig::hasHomeKey() ? 4 : CrossPointSettings::LP_MENU_DISABLED;
  }
  if (raw == CrossPointSettings::LP_MENU_CREATE_CLIPPING) return BoardConfig::hasHomeKey() ? 5 : 4;
  return CrossPointSettings::LP_MENU_DISABLED;
}

inline void setLongPressMenuFromDisplayValue(const uint8_t displayValue) {
  if (displayValue <= CrossPointSettings::LP_MENU_DICTIONARY) {
    SETTINGS.longPressMenuFunction = displayValue;
  } else if (BoardConfig::hasHomeKey()) {
    SETTINGS.longPressMenuFunction =
        displayValue == 4 ? CrossPointSettings::LP_MENU_READER_MENU : CrossPointSettings::LP_MENU_CREATE_CLIPPING;
  } else {
    SETTINGS.longPressMenuFunction = CrossPointSettings::LP_MENU_CREATE_CLIPPING;
  }
}

inline SettingInfo buildLongPressMenuSetting() {
  return SettingInfo::DynamicEnum(StrId::STR_LONG_PRESS_MENU, buildLongPressMenuValues(), longPressMenuDisplayValue,
                                  setLongPressMenuFromDisplayValue, "longPressMenuFunction", StrId::STR_CAT_CONTROLS);
}

inline std::vector<StrId> buildShortPowerButtonValues() {
  std::vector<StrId> values = {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH,
                               StrId::STR_FOOTNOTES};
  values.reserve(CrossPointSettings::SHORT_PWRBTN_COUNT);
  if (BoardConfig::hasTouch()) values.push_back(StrId::STR_CONFIRM);
  values.push_back(StrId::STR_SAVE_CLIPPING);
  return values;
}

inline uint8_t shortPowerButtonDisplayValue() {
  const uint8_t raw = SETTINGS.shortPwrBtn;
  if (raw <= CrossPointSettings::FOOTNOTES) return raw;
  if (raw == CrossPointSettings::CREATE_CLIPPING) return BoardConfig::hasTouch() ? 6 : 5;
  if (raw == CrossPointSettings::PWR_CONFIRM && BoardConfig::hasTouch()) return 5;
  return CrossPointSettings::IGNORE;
}

inline void setShortPowerButtonFromDisplayValue(const uint8_t displayValue) {
  if (!BoardConfig::hasTouch() && displayValue == 5) {
    SETTINGS.shortPwrBtn = CrossPointSettings::CREATE_CLIPPING;
    return;
  }
  SETTINGS.shortPwrBtn = displayValue;
}

inline SettingInfo buildShortPowerButtonSetting() {
  return SettingInfo::DynamicEnum(StrId::STR_SHORT_PWR_BTN, buildShortPowerButtonValues(), shortPowerButtonDisplayValue,
                                  setShortPowerButtonFromDisplayValue, "shortPwrBtn", StrId::STR_CAT_CONTROLS,
                                  &CrossPointSettings::shortPwrBtn, CrossPointSettings::SHORT_PWRBTN_COUNT);
}

// Per-direction page-turn gestures. The list offers four rows but the stored
// enum is not the row index: the list dropped Inverted Tap, so DISABLED must
// keep its stored value 4 while displaying as row 3. Writing the row index
// directly (the Enum setting's default) stored 3, which is INVERTED_TAP and
// still passes gestureAllowsTap(), so "Disabled" kept turning pages.
inline std::vector<StrId> pageTurnGestureValues() {
  static constexpr StrId VALUES[] = {StrId::STR_TAP_AND_SWIPE, StrId::STR_TAP_ONLY, StrId::STR_SWIPE_ONLY,
                                     StrId::STR_DISABLED};
  return {VALUES, VALUES + std::size(VALUES)};
}

inline uint8_t pageTurnGestureDisplayValue(const uint8_t stored) {
  switch (stored) {
    case CrossPointSettings::TAP_AND_SWIPE:
      return 0;
    case CrossPointSettings::TAP_ONLY:
      return 1;
    case CrossPointSettings::SWIPE_ONLY:
      return 2;
    case CrossPointSettings::PAGE_TURN_GESTURE_DISABLED:
      return 3;
    default:
      // Saves from before Inverted Tap was folded away (and any out-of-range
      // value) act as a tap page turn; show the row that matches the behaviour
      // so the row the user sees is the row that is in effect.
      return 1;
  }
}

inline void setPageTurnGestureFromDisplayValue(uint8_t& field, const uint8_t displayValue) {
  switch (displayValue) {
    case 0:
      field = CrossPointSettings::TAP_AND_SWIPE;
      return;
    case 1:
      field = CrossPointSettings::TAP_ONLY;
      return;
    case 2:
      field = CrossPointSettings::SWIPE_ONLY;
      return;
    default:
      field = CrossPointSettings::PAGE_TURN_GESTURE_DISABLED;
      return;
  }
}

inline std::vector<StrId> homeThemeValues() {
  static constexpr StrId VALUES[] = {StrId::STR_THEME_CLASSIC,       StrId::STR_THEME_LYRA,
                                     StrId::STR_THEME_LYRA_EXTENDED, StrId::STR_THEME_ROUNDEDRAFF,
                                     StrId::STR_THEME_LYRA_CAROUSEL, StrId::STR_THEME_INX,
                                     StrId::STR_THEME_COVER_GRID};
  const size_t count = UITheme::supportsCoverGrid() ? std::size(VALUES) : std::size(VALUES) - 1;
  return {VALUES, VALUES + count};
}

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
using SettingVisitor = void (*)(void* context, SettingInfo& info);
void forEachBaseSetting(SettingVisitor visit, void* context);
template <typename Fn>
void forEachBaseSetting(Fn& fn) {
  forEachBaseSetting([](void* context, SettingInfo& info) { (*static_cast<Fn*>(context))(info); }, &fn);
}
std::vector<SettingInfo> getBaseSettingsList(size_t extraCapacity = 0);

inline bool isSettingAvailableOnBoard(const SettingInfo& setting) {
  if (setting.nameId == StrId::STR_DBL_CLICK_PWR_LIGHT && !BoardConfig::isX4Pro()) return false;
  if (setting.nameId == StrId::STR_TILT_PAGE_TURN && !halTiltSensor.isAvailable()) return false;
  if (!BoardConfig::hasTouch() &&
      (setting.nameId == StrId::STR_TOUCH_READER_CONTROLS || setting.nameId == StrId::STR_TAP_ZONES))
    return false;
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
  auto visit = [&](SettingInfo& setting) {
    if (!isSettingAvailableOnBoard(setting)) return;

    if (setting.nameId == StrId::STR_FONT_FAMILY) {
      const SettingInfo dynamicSetting = buildFontFamilySetting(registry);
      callback(dynamicSetting);
    } else if (setting.nameId == StrId::STR_FONT_SIZE) {
      const SettingInfo dynamicSetting = buildFontSizeSetting(registry);
      callback(dynamicSetting);
    } else {
      callback(setting);
    }
  };
  forEachBaseSetting(visit);
}

// Device settings need an owned list because they retain category entries and
// may insert a dynamically discovered dictionary entry.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr,
                                                const std::vector<DictionaryEntry>* dictionaries = nullptr) {
  std::vector<SettingInfo> v = getBaseSettingsList(dictionaries && !dictionaries->empty() ? 1 : 0);
  if (!BoardConfig::hasTouch()) {
    // The reader menu style stays available on button boards (the toolbar
    // chrome is button-navigable); only the touch controls are hidden.
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_TOUCH_READER_CONTROLS ||
                                    s.nameId == StrId::STR_NEXT_PAGE_GESTURE ||
                                    s.nameId == StrId::STR_PREV_PAGE_GESTURE || s.nameId == StrId::STR_TAP_ZONES;
                           }),
            v.end());
  }
  // The reader-menu gesture choice only makes sense where the menu stays
  // reachable without the tap and the bottom edge is free (the capacitive
  // Home key); everywhere else the bottom-edge up-swipe is Home and the
  // center tap is the primary path, so the setting stays at its Tap default.
  if (!BoardConfig::hasHomeKey()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) { return s.nameId == StrId::STR_SHOW_READER_MENU; }),
            v.end());
  }
  if (BoardConfig::hasHomeKey()) {
    v.reserve(v.size() + 3);
    for (unsigned i = 0; i < 3; ++i) {
      v.push_back(SettingInfo::StaticEnum(home_button::GESTURE_LABELS[i], home_button::FIELDS[i],
                                          home_button::ACTION_LABELS, home_button::KEYS[i], StrId::STR_CAT_CONTROLS));
    }
  }
  if (BoardConfig::hasTouch()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION ||
                                    s.nameId == StrId::STR_SUNLIGHT_FADING_FIX ||
                                    s.nameId == StrId::STR_BACK_SHORT_TO_FILE_BROWSER;
                           }),
            v.end());
  }
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
