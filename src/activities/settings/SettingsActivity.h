#pragma once
#include <I18n.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/UiTabListActivity.h"
#include "components/OptionPopup.h"

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING };

enum class SettingAction {
  None,
  RemapFrontButtons,
  CustomiseStatusBar,
  ReadingStatsSettings,
  AppVisibility,
  KOReaderSync,
  OPDSBrowser,
  Network,
  DateTime,
  ClearCache,
  RestoreSystemSettings,
  CheckForUpdates,
  SdFirmwareUpdate,
  Language,
  DownloadFonts,
  ManageDictionaries,
  TextSettings,
  About,
};

struct SettingInfo {
  StrId nameId;
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  int8_t CrossPointSettings::* signedValuePtr = nullptr;
  std::vector<StrId> enumValues;
  std::vector<std::string> enumStringValues;  // runtime alternative to StrId enumValues (for SD card fonts etc.)
  SettingAction action = SettingAction::None;

  struct ValueRange {
    int16_t min;
    int16_t max;
    int16_t step;
  };
  ValueRange valueRange = {};

  const char* key = nullptr;             // JSON API key (nullptr for ACTION types)
  StrId category = StrId::STR_NONE_OPT;  // Category for web UI grouping
  bool obfuscated = false;               // Save/load via base64 obfuscation (passwords)
  bool inTextSettings = false;           // Surfaced in the Text Settings screen; hidden from the flat Reader list
  bool inReadingStatsSettings = false;   // Surfaced in Reading Stats Settings; hidden from the flat Reader list
  bool managedEnumPicker = false;        // Always open a picker; the dynamic setter owns the change lifecycle

  // Direct char[] string fields (for settings stored in CrossPointSettings)
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;

  // Dynamic accessors (for settings stored outside CrossPointSettings, e.g. KOReaderCredentialStore)
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  std::function<std::string()> stringGetter;
  std::function<void(const std::string&)> stringSetter;

  SettingInfo& withObfuscated() {
    obfuscated = true;
    return *this;
  }

  SettingInfo& withTextSettings() {
    inTextSettings = true;
    return *this;
  }

  SettingInfo& withReadingStatsSettings() {
    inReadingStatsSettings = true;
    return *this;
  }

  SettingInfo& withManagedEnumPicker() {
    managedEnumPicker = true;
    return *this;
  }

  static SettingInfo Toggle(StrId nameId, uint8_t CrossPointSettings::* ptr, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::TOGGLE;
    s.valuePtr = ptr;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Enum(StrId nameId, uint8_t CrossPointSettings::* ptr, std::vector<StrId> values,
                          const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.valuePtr = ptr;
    s.enumValues = std::move(values);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Action(StrId nameId, SettingAction action) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ACTION;
    s.action = action;
    return s;
  }

  static SettingInfo Value(StrId nameId, uint8_t CrossPointSettings::* ptr, const ValueRange valueRange,
                           const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::VALUE;
    s.valuePtr = ptr;
    s.valueRange = valueRange;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo SignedValue(StrId nameId, int8_t CrossPointSettings::* ptr, const ValueRange valueRange,
                                 const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::VALUE;
    s.signedValuePtr = ptr;
    s.valueRange = valueRange;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo String(StrId nameId, char* ptr, size_t maxLen, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringOffset = (size_t)ptr - (size_t)&SETTINGS;
    s.stringMaxLen = maxLen;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, std::function<uint8_t()> getter,
                                 std::function<void(uint8_t)> setter, const char* key = nullptr,
                                 StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.enumValues = std::move(values);
    s.valueGetter = std::move(getter);
    s.valueSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicString(StrId nameId, std::function<std::string()> getter,
                                   std::function<void(const std::string&)> setter, const char* key = nullptr,
                                   StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringGetter = std::move(getter);
    s.stringSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }
};

class SettingsActivity final : public UiTabListActivity {
  int selectedCategoryIndex = 0;  // Currently selected category
  int settingsCount = 0;
  uint8_t expandedCategories = 0;

  // Per-category settings derived from shared list + device-only actions
  std::vector<SettingInfo> displaySettings;
  std::vector<SettingInfo> readerSettings;
  std::vector<SettingInfo> controlsSettings;
  std::vector<SettingInfo> systemSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;

  bool preserveQuickResumeTimeoutOn = false;
  bool quickResumeTimeoutAutoEnabled = false;
  bool dictionariesLoaded = false;

  OptionPopup optionPopup;

  // Row structure (label/actionValue) for *currentSettings, rebuilt only when
  // the active category or a category's setting list changes
  // (rebuildRowItems(), called from selectCategory()/rebuildSettingsLists())
  // — not on every repaint. rowValues_ holds the live per-row value text,
  // refreshed every buildScreen() call by assigning into the existing
  // strings (no vector growth).
  std::vector<std::string> rowValues_;
  std::vector<std::string> rowLabels_;
  std::vector<freeink::ui::ListItem> rowItems_;
  void rebuildRowItems();
  void rebuildAccordionRows();

  static constexpr int categoryCount = 4;
  static const StrId categoryNames[categoryCount];

  // --- UiTabListActivity contract ---
  int listCount() const override;
  int tabCount() const override { return categoryCount; }
  int activeTab() const override { return selectedCategoryIndex; }
  const char* tabLabel(int index) const override { return I18N.get(categoryNames[index]); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  freeink::ui::ListNav& activeNav() override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;
  void navigateButtons() override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleButtons() override;
  bool handleCustomInput() override;

  static std::string settingValueText(const SettingInfo& setting);
  void selectCategory(int categoryIndex);
  void applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr);

  void toggleCurrentSetting();
  void toggleAccordionSetting(int categoryIndex, int settingIndex);
  void toggleAccordionCategory(int categoryIndex);
  bool usesAccordion() const;
  const std::vector<SettingInfo>& settingsForCategory(int categoryIndex) const;
  std::array<int, categoryCount> accordionSettingCounts() const;
  void openOtaUpdate();
  void openSleepTimeoutPicker();
  void openReadingBackgroundMenu();
  void openReadingBackgroundPicker();
  void confirmRestoreSystemSettings();
  void rebuildSettingsLists();
  void syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged);

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  MainTab mainTab() const override { return MainTab::Settings; }
  bool mainTabBackReturnsToTabs() const override { return !usesAccordion() || expandedCategories == 0; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;
};
