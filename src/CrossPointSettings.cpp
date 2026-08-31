#include "CrossPointSettings.h"

#include <BoardConfig.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <Serialization.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

#include "I18nKeys.h"
#include "ReaderFontSizes.h"
#include "SettingsList.h"
#include "fontIds.h"

// Resolved here (not in the header) so I18nKeys.h — auto-generated and
// changed on every translation edit — doesn't pull a recompile of every
// file that includes CrossPointSettings.h.
uint8_t CrossPointSettings::defaultLanguageIndex() { return static_cast<uint8_t>(Language::EN); }

namespace {

constexpr uint8_t SETTINGS_FILE_VERSION = 2;
constexpr char SETTINGS_FILE_BIN[] = "/.crosspoint/settings.bin";
constexpr char SETTINGS_FILE_BAK[] = "/.crosspoint/settings.bin.bak";
constexpr char LANG_FILE_BIN[] = "/.crosspoint/language.bin";
constexpr char LANG_FILE_BAK[] = "/.crosspoint/language.bin.bak";
constexpr uint8_t FAKE_BOLD_VERSION = 1;
constexpr std::array<uint8_t, 3> LEGACY_FAKE_BOLD_MIGRATION = {
    CrossPointSettings::SYNTHETIC_BOLD_OFF,
    CrossPointSettings::SYNTHETIC_BOLD_STANDARD,
    CrossPointSettings::SYNTHETIC_BOLD_HEAVY,
};
static_assert(LEGACY_FAKE_BOLD_MIGRATION[0] == CrossPointSettings::SYNTHETIC_BOLD_OFF);
static_assert(LEGACY_FAKE_BOLD_MIGRATION[1] == CrossPointSettings::SYNTHETIC_BOLD_STANDARD);
static_assert(LEGACY_FAKE_BOLD_MIGRATION[2] == CrossPointSettings::SYNTHETIC_BOLD_HEAVY);

// Stack buffer for "<key>_obf" key construction — avoids a std::string
// allocation per obfuscated setting on every save and load.
constexpr size_t OBF_KEY_BUF = 64;

// Null-terminated copy into a fixed-size settings field.
void copyToField(char* dest, const char* src, const size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

// Convert the legacy aggregate status-bar mode into the split settings used now.
void applyLegacyStatusBarSettings(CrossPointSettings& settings) {
  switch (static_cast<CrossPointSettings::STATUS_BAR_MODE>(settings.statusBar)) {
    case CrossPointSettings::NONE:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::NO_PROGRESS:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::ONLY_BOOK_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 0;
      settings.statusBarProgressBar = CrossPointSettings::BOOK_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::HIDE_TITLE;
      settings.statusBarBattery = 0;
      break;
    case CrossPointSettings::CHAPTER_PROGRESS_BAR:
      settings.statusBarChapterPageCount = 0;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::CHAPTER_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
    case CrossPointSettings::FULL:
    default:
      settings.statusBarChapterPageCount = 1;
      settings.statusBarBookProgressPercentage = 1;
      settings.statusBarProgressBar = CrossPointSettings::HIDE_PROGRESS;
      settings.statusBarTitle = CrossPointSettings::CHAPTER_TITLE;
      settings.statusBarBattery = 1;
      break;
  }
}

// Convert legacy front button layout into explicit logical->hardware mapping.
void applyLegacyFrontButtonLayout(CrossPointSettings& settings) {
  switch (static_cast<CrossPointSettings::FRONT_BUTTON_LAYOUT>(settings.frontButtonLayout)) {
    case CrossPointSettings::LEFT_RIGHT_BACK_CONFIRM:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_LEFT;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_RIGHT;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_CONFIRM;
      break;
    case CrossPointSettings::LEFT_BACK_CONFIRM_RIGHT:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_CONFIRM;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_LEFT;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
      break;
    case CrossPointSettings::BACK_CONFIRM_RIGHT_LEFT:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_RIGHT;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_LEFT;
      break;
    case CrossPointSettings::BACK_CONFIRM_LEFT_RIGHT:
    default:
      settings.frontButtonBack = CrossPointSettings::FRONT_HW_BACK;
      settings.frontButtonConfirm = CrossPointSettings::FRONT_HW_CONFIRM;
      settings.frontButtonLeft = CrossPointSettings::FRONT_HW_LEFT;
      settings.frontButtonRight = CrossPointSettings::FRONT_HW_RIGHT;
      break;
  }
}

bool isSettingAvailableForPersistence(const SettingInfo& setting) {
#if !defined(SIMULATOR)
  // Settings load before Frontlight.begin(), so persistence must use the board profile, not the runtime probe.
  if (setting.valuePtr == &CrossPointSettings::frontlightBrightness ||
      setting.valuePtr == &CrossPointSettings::frontlightOn ||
      setting.valuePtr == &CrossPointSettings::frontlightRestoreOnWake) {
    return BoardConfig::hasPwmFrontlight() || BoardConfig::hasI2cFrontlight();
  }
#if FREEINK_CAP_WARMLIGHT
  if (setting.valuePtr == &CrossPointSettings::frontlightWarmth) {
    return BoardConfig::hasColorTemperatureFrontlight();
  }
#endif
#endif
  return isSettingAvailableOnBoard(setting);
}

}  // namespace

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(const uint8_t legacyValue) {
  switch (legacyValue) {
    case SLEEP_1_MIN:
      return 1;
    case SLEEP_5_MIN:
      return 5;
    case SLEEP_15_MIN:
      return 15;
    case SLEEP_30_MIN:
      return 30;
    case SLEEP_10_MIN:
    default:
      return 10;
  }
}

void CrossPointSettings::toJson(JsonDocument& doc) const {
  const CrossPointSettings& s = *this;

  for (const auto& info : getBaseSettingsList()) {
    if (!isSettingAvailableForPersistence(info)) continue;
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.signedValuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        doc[obfKey] = obfuscation::obfuscateToBase64(strPtr);
      } else {
        doc[info.key] = strPtr;
      }
    } else if (info.signedValuePtr) {
      doc[info.key] = s.*(info.signedValuePtr);
    } else {
      doc[info.key] = s.*(info.valuePtr);
    }
  }

  doc["fakeBoldVersion"] = FAKE_BOLD_VERSION;
  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = frontButtonBack;
  doc["frontButtonConfirm"] = frontButtonConfirm;
  doc["frontButtonLeft"] = frontButtonLeft;
  doc["frontButtonRight"] = frontButtonRight;
  // Apps use stable IDs beyond the uint8_t-only SettingsList, so persist the mask manually.
  doc["hiddenAppsMask"] = hiddenAppsMask;
  doc["appsCatalogVersion"] = appsCatalogVersion;
  doc["buddyClaimed"] = buddyClaimed;
  // Font family and size — both use dynamic getter/setters in SettingsList (the
  // option lists depend on the SD font registry), so the generic loop skips them.
  doc["fontFamily"] = fontFamily;
  doc["fontSize"] = fontPointSize;
  // SD card font family name — not in SettingsList, save manually
  if (sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = sdFontFamilyName;
  }
  doc["sdFontFlashPreload"] = sdFontFlashPreload;
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, save manually
  if (dictionaryName[0] != '\0') {
    doc["dictionaryName"] = dictionaryName;
  }
  doc["otaNightlyEnabled"] = otaNightlyEnabled;

  // Language -- managed by LanguageSelectActivity, not in SettingsList.
  // Stored as ISO code string ("EN", "DE", ...) for stability across enum reorders.
  doc["language"] = (language < getLanguageCount()) ? LANGUAGE_CODES[language] : "EN";
  doc["contentProfile"] = static_cast<uint8_t>(contentProfile);
  doc["onboardingVersion"] = onboardingVersion;
  // Keep the legacy marker current so rollback to a split-SKU firmware retains
  // the selected service region.
  doc["langSku"] = contentProfile == ContentProfile::China ? "cn" : "global";
}

bool CrossPointSettings::fromJson(JsonVariantConst doc) {
  CrossPointSettings& s = *this;
  bool needsResave = false;

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  // Legacy migration: the aggregate statusBar field predates the split fields.
  if (doc["statusBarChapterPageCount"].isNull()) {
    statusBar = clamp(doc["statusBar"] | static_cast<uint8_t>(FULL), STATUS_BAR_MODE_COUNT, FULL);
    applyLegacyStatusBarSettings(s);
    needsResave = true;
  }

  for (const auto& info : getBaseSettingsList()) {
    if (!isSettingAvailableForPersistence(info)) continue;
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.signedValuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      // destPtr starts out holding the struct-initializer default; it stays that
      // way unless the document actually carries a value for this key.
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        needsResave = true;
        continue;
      }

      bool loaded = false;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        bool ok = false;
        bool tooLong = false;
        const std::string decoded =
            obfuscation::deobfuscateFromBase64(doc[obfKey] | "", info.stringMaxLen - 1, &ok, &tooLong);
        if (tooLong) {
          LOG_ERR("CPS", "Oversized obfuscated value for key '%s'", info.key);
          needsResave = true;
        }
        if (ok && !decoded.empty()) {
          copyToField(destPtr, decoded.c_str(), info.stringMaxLen);
          loaded = true;
        }
      }
      if (!loaded) {
        // Read as const char*, never `| std::string(...)`: ArduinoJson's
        // std::string converter drags a per-TU copy of the serializer into
        // flash. See the note in PersistableStore.h.
        const char* raw = doc[info.key].is<const char*>() ? doc[info.key].as<const char*>() : nullptr;
        if (raw) {
          // Obfuscated field recovered from a legacy plaintext value -> resave.
          if (info.obfuscated && strcmp(raw, destPtr) != 0) needsResave = true;
          copyToField(destPtr, raw, info.stringMaxLen);
        }
      }
    } else if (info.signedValuePtr) {
      const int8_t fieldDefault = s.*(info.signedValuePtr);
      int value = doc[info.key] | static_cast<int>(fieldDefault);
      if (value < info.valueRange.min || value > info.valueRange.max) value = fieldDefault;
      s.*(info.signedValuePtr) = static_cast<int8_t>(value);
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      if (info.type == SettingType::ENUM) {
        v = clamp(v, (uint8_t)info.enumValues.size(), fieldDefault);
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  }

  if ((doc["fakeBoldVersion"] | static_cast<uint8_t>(0)) < FAKE_BOLD_VERSION) {
    if (doc["fakeBold"].is<uint8_t>()) {
      const uint8_t legacyFakeBold = doc["fakeBold"].as<uint8_t>();
      if (legacyFakeBold < LEGACY_FAKE_BOLD_MIGRATION.size()) {
        fakeBold = LEGACY_FAKE_BOLD_MIGRATION[legacyFakeBold];
      }
    }
    needsResave = true;
  }

  if (doc["sleepTimeoutMinutes"].isNull() && !doc["sleepTimeout"].isNull()) {
    const uint8_t legacyValue =
        clamp(doc["sleepTimeout"] | (uint8_t)SLEEP_10_MIN, SLEEP_TIMEOUT_COUNT, (uint8_t)SLEEP_10_MIN);
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(legacyValue);
    needsResave = true;
  }
  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  frontButtonBack = clamp(doc["frontButtonBack"] | (uint8_t)FRONT_HW_BACK, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_BACK);
  frontButtonConfirm =
      clamp(doc["frontButtonConfirm"] | (uint8_t)FRONT_HW_CONFIRM, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_CONFIRM);
  frontButtonLeft = clamp(doc["frontButtonLeft"] | (uint8_t)FRONT_HW_LEFT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_LEFT);
  frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)FRONT_HW_RIGHT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_RIGHT);
  validateFrontButtonMapping(s);
  hiddenAppsMask = doc["hiddenAppsMask"].isNull() ? DEFAULT_HIDDEN_APPS_MASK : doc["hiddenAppsMask"].as<uint32_t>();
  const uint8_t storedAppsCatalogVersion = doc["appsCatalogVersion"] | static_cast<uint8_t>(0);
  // Buddy was added at catalog version 1. Hide it exactly once during the
  // upgrade, then preserve the user's visibility choice on later boots.
  if (storedAppsCatalogVersion < APPS_CATALOG_VERSION) {
    hiddenAppsMask |= uint32_t{1} << BUDDY_APP_ID;
    needsResave = true;
  }
  appsCatalogVersion = APPS_CATALOG_VERSION;
  buddyClaimed = clamp(doc["buddyClaimed"] | static_cast<uint8_t>(0), static_cast<uint8_t>(2), static_cast<uint8_t>(0));

  // Reader font size — an actual point size since 1.5. Files written by 1.4 and
  // earlier hold the old SMALL/MEDIUM/LARGE/EXTRA_LARGE slot in 0..3; no font is
  // renderable at those sizes, so the range is unambiguous and folds to the
  // point sizes those slots used to mean. Drop this once 1.4 upgrades are done.
  uint8_t storedFontSize = doc["fontSize"] | DEFAULT_FONT_POINT_SIZE;
  if (storedFontSize <= LEGACY_FONT_SIZE_MAX) {
    storedFontSize = 12 + storedFontSize * 2;  // 0,1,2,3 -> 12,14,16,18
    needsResave = true;
  }
  fontPointSize = storedFontSize;

  // Font family — uses dynamic getter/setter in SettingsList so the generic loop skips it.
  const uint8_t storedFontFamily = doc["fontFamily"] | (uint8_t)0;
  fontFamily = clamp(storedFontFamily, BUILTIN_FONT_COUNT, 0);
  // SD card font family name — not in SettingsList, load manually
  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(sdFontFamilyName, sfn, sizeof(sdFontFamilyName) - 1);
  sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
  sdFontFlashPreload =
      clamp(static_cast<uint8_t>(doc["sdFontFlashPreload"] | 0), static_cast<uint8_t>(2), static_cast<uint8_t>(0));
  if (storedFontFamily == LEGACY_OPENDYSLEXIC && sdFontFamilyName[0] == '\0') {
    fontFamily = NOTOSERIF;
    strncpy(sdFontFamilyName, "OpenDyslexic", sizeof(sdFontFamilyName) - 1);
    sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
    needsResave = true;
  } else if (storedFontFamily >= BUILTIN_FONT_COUNT) {
    needsResave = true;
  }
  if (sdFontFamilyName[0] == '\0' && fontFamily != NOTOSANS) {
    fontFamily = NOTOSANS;
    needsResave = true;
  }
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, load manually
  copyToField(dictionaryName, doc["dictionaryName"] | "", sizeof(dictionaryName));
  otaNightlyEnabled =
      clamp(static_cast<uint8_t>(doc["otaNightlyEnabled"] | 0), static_cast<uint8_t>(2), static_cast<uint8_t>(0));

  // Language -- stored as code string for stability across enum reorders.
  if (doc["language"].is<const char*>()) {
    language = static_cast<uint8_t>(I18n::languageFromCode(doc["language"].as<const char*>()));
  }

  // Migrate the split-build region marker. Language and service region are
  // deliberately independent after onboarding.
  const char* langSku = doc["langSku"] | "";
  const uint8_t storedProfile = doc["contentProfile"].is<uint8_t>() ? doc["contentProfile"].as<uint8_t>() : UINT8_MAX;
  if (storedProfile <= static_cast<uint8_t>(ContentProfile::China)) {
    contentProfile = static_cast<ContentProfile>(storedProfile);
  } else {
    if (strcmp(langSku, "cn") == 0) {
      contentProfile = ContentProfile::China;
    } else if (strcmp(langSku, "global") == 0) {
      contentProfile = ContentProfile::Global;
    } else {
      contentProfile =
          static_cast<Language>(language) == Language::ZH_CN ? ContentProfile::China : ContentProfile::Global;
    }
    needsResave = true;
  }
  if (!I18n::isLanguageAvailable(static_cast<Language>(language))) {
    language = defaultLanguageIndex();
    needsResave = true;
  }
  onboardingVersion = doc["onboardingVersion"].is<uint8_t>() ? doc["onboardingVersion"].as<uint8_t>() : 0;

  if (needsResave) {
    LOG_DBG("CPS", "Resaving settings to update format");
    requestResave();
  }

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

bool CrossPointSettings::loadFromFile() {
  if (Storage.exists(getFilePath()) && PersistableStore<CrossPointSettings>::loadFromFile()) {
    migrateLanguageBinaryFile();
    return true;
  }

  if (Storage.exists(SETTINGS_FILE_BIN) && loadFromBinaryFile()) {
    migrateLanguageBinaryFile();
    if (saveToFile()) {
      Storage.rename(SETTINGS_FILE_BIN, SETTINGS_FILE_BAK);
      LOG_DBG("CPS", "Migrated settings.bin to settings.json");
      return true;
    }
    LOG_ERR("CPS", "Failed to save migrated settings to JSON");
    return false;
  }

  // No settings file: an old standalone language selection can still be migrated.
  return migrateLanguageBinaryFile();
}

bool CrossPointSettings::migrateLanguageBinaryFile() {
  // V1_LANGUAGES / V1_LANGUAGE_COUNT are emitted by gen_i18n.py with the
  // frozen enum order from the original binary format.
  if (!Storage.exists(LANG_FILE_BIN)) return false;

  HalFile file;
  if (!Storage.openFileForRead("CPS", LANG_FILE_BIN, file)) return false;

  uint8_t version = 0;
  uint8_t oldIndex = 0;
  if (!serialization::readPod(file, version) || version != 1 || !serialization::readPod(file, oldIndex) ||
      oldIndex >= V1_LANGUAGE_COUNT) {
    LOG_ERR("CPS", "Invalid or truncated language.bin");
    return false;
  }

  language = static_cast<uint8_t>(V1_LANGUAGES[oldIndex]);
  contentProfile = static_cast<Language>(language) == Language::ZH_CN ? ContentProfile::China : ContentProfile::Global;
  Storage.rename(LANG_FILE_BIN, LANG_FILE_BAK);
  saveToFile();
  LOG_DBG("CPS", "Migrated language.bin into settings.json");
  return true;
}

bool CrossPointSettings::loadFromBinaryFile() {
  HalFile inputFile;
  if (!Storage.openFileForRead("CPS", SETTINGS_FILE_BIN, inputFile)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(storeMutex);

  uint8_t version = 0;
  if (!serialization::readPod(inputFile, version) || version != SETTINGS_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    return false;
  }

  uint8_t fileSettingsCount = 0;
  constexpr size_t LEGACY_SETTING_COUNT = 28;
  std::array<uint8_t, LEGACY_SETTING_COUNT> values{};
  if (!serialization::readPod(inputFile, fileSettingsCount) || fileSettingsCount > values.size()) {
    LOG_ERR("CPS", "Deserialization failed: Invalid settings count %u", fileSettingsCount);
    return false;
  }
  for (uint8_t i = 0; i < fileSettingsCount; i++) {
    if (!serialization::readPod(inputFile, values[i])) {
      LOG_ERR("CPS", "Deserialization failed: Truncated setting %u", i);
      return false;
    }
  }

  auto value = [&](const size_t index, const uint8_t current) {
    return index < fileSettingsCount ? values[index] : current;
  };
  auto validated = [&](const size_t index, const uint8_t current, const uint8_t maxValue) {
    const uint8_t next = value(index, current);
    return next < maxValue ? next : current;
  };

  sleepScreen = validated(0, sleepScreen, SLEEP_SCREEN_MODE_COUNT);
  extraParagraphSpacing = value(1, extraParagraphSpacing);
  shortPwrBtn = validated(2, shortPwrBtn, SHORT_PWRBTN_COUNT);
  statusBar = validated(3, statusBar, STATUS_BAR_MODE_COUNT);
  orientation = validated(4, orientation, ORIENTATION_COUNT);
  frontButtonLayout = validated(5, frontButtonLayout, FRONT_BUTTON_LAYOUT_COUNT);
  sideButtonLayout = validated(6, sideButtonLayout, SIDE_BUTTON_LAYOUT_COUNT);
  if (fileSettingsCount > 7) {
    const uint8_t legacyFontFamily = values[7];
    if (legacyFontFamily < BUILTIN_FONT_COUNT) {
      fontFamily = legacyFontFamily;
    } else if (legacyFontFamily == LEGACY_OPENDYSLEXIC) {
      fontFamily = NOTOSERIF;
      copyToField(sdFontFamilyName, "OpenDyslexic", sizeof(sdFontFamilyName));
    }
  }
  if (fileSettingsCount > 8) {
    const uint8_t legacyFontSize = validated(8, 1, LEGACY_FONT_SIZE_MAX + 1);
    fontPointSize = 12 + legacyFontSize * 2;
  }
  lineSpacing = validated(9, lineSpacing, LINE_COMPRESSION_COUNT);
  paragraphAlignment = validated(10, paragraphAlignment, PARAGRAPH_ALIGNMENT_COUNT);
  if (fileSettingsCount > 11) {
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(validated(11, SLEEP_10_MIN, SLEEP_TIMEOUT_COUNT));
  }
  refreshFrequency = validated(12, refreshFrequency, REFRESH_FREQUENCY_COUNT);
  screenMargin = value(13, screenMargin);
  sleepScreenCoverMode = validated(14, sleepScreenCoverMode, SLEEP_SCREEN_COVER_MODE_COUNT);
  textAntiAliasing = value(15, textAntiAliasing);
  hideBatteryPercentage = validated(16, hideBatteryPercentage, HIDE_BATTERY_PERCENTAGE_COUNT);
  longPressButtonBehavior = validated(17, longPressButtonBehavior, LONG_PRESS_BUTTON_BEHAVIOR_COUNT);
  hyphenationEnabled = value(18, hyphenationEnabled);
  sleepScreenCoverFilter = validated(19, sleepScreenCoverFilter, SLEEP_SCREEN_COVER_FILTER_COUNT);
  uiTheme = value(20, uiTheme);
  frontButtonBack = validated(21, frontButtonBack, FRONT_BUTTON_HARDWARE_COUNT);
  frontButtonConfirm = validated(22, frontButtonConfirm, FRONT_BUTTON_HARDWARE_COUNT);
  frontButtonLeft = validated(23, frontButtonLeft, FRONT_BUTTON_HARDWARE_COUNT);
  frontButtonRight = validated(24, frontButtonRight, FRONT_BUTTON_HARDWARE_COUNT);
  fadingFix = value(25, fadingFix);
  embeddedStyle = value(26, embeddedStyle);
  frontButtonFollowOrientation = value(27, frontButtonFollowOrientation);

  if (fileSettingsCount >= 25) {
    validateFrontButtonMapping(*this);
  } else {
    applyLegacyFrontButtonLayout(*this);
  }
  applyLegacyStatusBarSettings(*this);

  LOG_DBG("CPS", "Settings loaded from binary file");
  return true;
}

CrossPointSettings::StatusBarSpec CrossPointSettings::statusBarSpec() const {
  StatusBarSpec spec;
  spec.showChapterPageCount = statusBarChapterPageCount != 0;
  spec.showBookProgressPercent = statusBarBookProgressPercentage != 0;
  spec.titleMode = statusBarTitle;
  spec.showBattery = statusBarBattery != 0;
  spec.showBatteryPercent = hideBatteryPercentage == HIDE_NEVER;
  spec.clockMode = statusBarClock;
  spec.clock12h = clockFormat == 1;
  spec.clockUtcOffsetQ = clockUtcOffsetQ;
  spec.progressBarMode = statusBarProgressBar;
  spec.progressBarHeightPx =
      statusBarProgressBar != HIDE_PROGRESS ? static_cast<uint8_t>((statusBarProgressBarThickness + 1) * 2) : 0;
  spec.xtcMode = xtcStatusBarMode;
  return spec;
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth,
                                                      const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = getReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.extraParagraphSpacing = extraParagraphSpacing != 0;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  spec.imageRendering = imageRendering;
  spec.focusReadingEnabled = focusReadingEnabled != 0;
  return spec;
}

float CrossPointSettings::getReaderLineCompression() const {
  // SD card fonts use same compression as Bookerly (the most neutral values)
  if (sdFontFamilyName[0] != '\0') {
    switch (lineSpacing) {
      case TIGHT:
        return 0.95f;
      case NORMAL:
      default:
        return 1.0f;
      case WIDE:
        return 1.1f;
      case EXTRA_WIDE:
        return 1.2f;
    }
  }

  switch (fontFamily) {
    case NOTOSERIF:
    default:
      switch (lineSpacing) {
        case TIGHT:
          return 0.95f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
        case EXTRA_WIDE:
          return 1.2f;
      }
    case NOTOSANS:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
        case EXTRA_WIDE:
          return 1.05f;
      }
  }
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  if (sleepTimeoutMinutes >= SLEEP_TIMEOUT_NEVER_MINUTES) return 0UL;
  const uint8_t minutes =
      std::clamp(sleepTimeoutMinutes, MIN_SLEEP_TIMEOUT_MINUTES, static_cast<uint8_t>(SLEEP_TIMEOUT_NEVER_MINUTES - 1));
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
}

uint64_t CrossPointSettings::getDailyGoalMs() const {
  switch (dailyGoalTarget) {
    case DAILY_GOAL_15_MIN:
      return 15ULL * 60ULL * 1000ULL;
    case DAILY_GOAL_30_MIN:
    default:
      return 30ULL * 60ULL * 1000ULL;
    case DAILY_GOAL_45_MIN:
      return 45ULL * 60ULL * 1000ULL;
    case DAILY_GOAL_60_MIN:
      return 60ULL * 60ULL * 1000ULL;
  }
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
    case REFRESH_NEVER:
      // Effectively disables the periodic full refresh; the page counter
      // counts down from here and never reaches the threshold in practice.
      return std::numeric_limits<int>::max();
  }
}

void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = '\0';
  sdFontFlashPreload = 0;
  fontFamily = NOTOSANS;
  fontPointSize =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  saveToFile();
}

int CrossPointSettings::getReaderFontId() const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontPointSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  // A built-in family only exists at BUILTIN_READER_POINT_SIZES, so a size
  // carried over from an SD family may not be one of them. ensureLoaded()
  // normally persists the snap; snap again here (without allocating — this runs
  // in the page render loop) so rendering is correct even before it has run.
  const uint8_t pt =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  const bool sans = (fontFamily == NOTOSANS);
  switch (pt) {
    case 12:
      return sans ? NOTOSANS_12_FONT_ID : NOTOSERIF_12_FONT_ID;
    case 16:
      return sans ? NOTOSANS_16_FONT_ID : NOTOSERIF_16_FONT_ID;
    case 18:
      return sans ? NOTOSANS_18_FONT_ID : NOTOSERIF_18_FONT_ID;
    case 14:
    default:
      return sans ? NOTOSANS_14_FONT_ID : NOTOSERIF_14_FONT_ID;
  }
}
