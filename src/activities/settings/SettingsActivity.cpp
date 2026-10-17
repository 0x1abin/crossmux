#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#if (FREEINK_DEVICE_MURPHY_M4 || FREEINK_CAP_HAPTIC) && !defined(SIMULATOR)
#include <HalGPIO.h>
#endif
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AboutActivity.h"
#include "AppVisibilitySettingsActivity.h"
#include "BluetoothSettingsActivity.h"
#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "ClockSettingsActivity.h"
#include "CrossPointSettings.h"
#include "DictionaryDownloadActivity.h"
#include "FontDownloadActivity.h"
#include "HomeButtonSettingsActivity.h"
#include "InxItemLayout.h"
#include "KOReaderSettingsActivity.h"
#include "KeyboardLayoutsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "ReadingStatsSettingsActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsList.h"
#include "SilentRestart.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/plugins/PluginCatalogActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"
#include "util/ReadingBackground.h"
#include "util/SystemSettingsReset.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId OK_OPTION[] = {StrId::STR_OK_BUTTON};
}  // namespace

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiTabListActivity("Settings", renderer, mappedInput) {}

void SettingsActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  if (usesAccordion()) {
    moveSelectionTo(MainTabs::contentEdgeIndex(edge, listCount()));
    return;
  }
  moveRingTo(MainTabs::contentEdgeIndex(edge, settingsCount) + (settingsCount > 0 ? 1 : 0));
}

void SettingsActivity::rebuildSettingsLists() {
  RenderLock lock(*this);
  closeRouting();
  SettingInfo dictionarySetting;
  bool hasDictionary = false;
  if (!resourcesDirty) {
    const auto dictionary = std::find_if(readerSettings.begin(), readerSettings.end(), [](const SettingInfo& setting) {
      return setting.nameId == StrId::STR_DICTIONARY;
    });
    if (dictionary != readerSettings.end()) {
      dictionarySetting = std::move(*dictionary);
      if (!dictionarySetting.enumStringValues.empty()) dictionarySetting.enumStringValues[0] = tr(STR_NONE_OPT);
      hasDictionary = true;
    }
  }
  displaySettings.clear();
  readerSettings.clear();
  controlsSettings.clear();
  systemSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  if (resourcesDirty) sdFontSystem.refreshIfDirty();

  std::vector<DictionaryEntry> dictionaries;
  if (resourcesDirty) DictionaryRegistry::discover(dictionaries);

  for (auto& setting : getSettingsList(&sdFontSystem.registry(), &dictionaries)) {
    if (setting.category == StrId::STR_NONE_OPT || home_button::isSetting(setting.valuePtr)) continue;
    if (!usesAccordion() && (setting.valuePtr == &CrossPointSettings::inxRecentLayout ||
                             setting.valuePtr == &CrossPointSettings::inxLibraryLayout ||
                             setting.valuePtr == &CrossPointSettings::inxAppsLayout ||
                             setting.valuePtr == &CrossPointSettings::inxTabPosition)) {
      continue;
    }
    if (setting.category == StrId::STR_CAT_DISPLAY) {
      // The sunlight fading fix is a grayscale-waveform compensation that does
      // not apply on the X4 Pro / X4 Classic (plain OTP waveform, same panels).
      if (setting.valuePtr == &CrossPointSettings::fadingFix && (BoardConfig::isX4Pro() || FREEINK_DEVICE_X4CLASSIC)) {
        continue;
      }
      displaySettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_READER) {
      // Settings merged into "Text Settings"
      // (they stay in the shared list for the web settings API)
      if (setting.inTextSettings || setting.inReadingStatsSettings) continue;
      readerSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_CONTROLS) {
      if (BoardConfig::hasHomeKey() && setting.valuePtr == &CrossPointSettings::longPressMenuFunction) continue;
      if (setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
          SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
        continue;
      }
      controlsSettings.push_back(setting);
    } else if (setting.category == StrId::STR_CAT_SYSTEM) {
      // These stay in the shared list for persistence and the web API, but the
      // device UI owns them in the Date & Time submenu.
      if (setting.valuePtr == &CrossPointSettings::clockAutoSync ||
          setting.valuePtr == &CrossPointSettings::clockUtcOffsetQ ||
          setting.valuePtr == &CrossPointSettings::clockFormat) {
        continue;
      }
      systemSettings.push_back(setting);
    }
  }

  if (hasDictionary) readerSettings.push_back(std::move(dictionarySetting));
  resourcesDirty = false;

  // Append device-only ACTION items
  if (!BoardConfig::hasTouch()) {
    controlsSettings.insert(controlsSettings.begin(),
                            SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS, SettingAction::RemapFrontButtons));
  }
#if FREEINK_CAP_BLE_HID_HOST
  controlsSettings.push_back(SettingInfo::Action(StrId::STR_BLUETOOTH, SettingAction::Bluetooth));
#endif
  systemSettings.push_back(SettingInfo::Action(StrId::STR_APP_VISIBILITY, SettingAction::AppVisibility));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_DATE_AND_TIME, SettingAction::ClockSettings));
  if (BoardConfig::hasHomeKey()) {
    controlsSettings.insert(controlsSettings.begin(),
                            SettingInfo::Action(StrId::STR_HOME_BUTTON, SettingAction::HomeButton));
  }
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
  systemSettings.push_back(
      SettingInfo::Action(StrId::STR_RESTORE_SYSTEM_SETTINGS, SettingAction::RestoreSystemSettings));
#if FREEINK_DEVICE_MURPHY_M4 && !defined(SIMULATOR)
  systemSettings.push_back(
      SettingInfo::DynamicEnum(
          StrId::STR_M4_HARDWARE_BATCH, {StrId::STR_M4_BATCH_1, StrId::STR_M4_BATCH_2},
          [] { return gpio.murphyM4Batch() == freeink::MurphyM4Batch::First ? 0 : 1; },
          [this](const uint8_t value) {
            const auto batch = value == 0 ? freeink::MurphyM4Batch::First : freeink::MurphyM4Batch::Second;
            if (gpio.saveMurphyM4Batch(batch)) {
              silentRestart();
              return;
            }
            optionPopup.show(StrId::STR_FAILED_LOWER, OK_OPTION, static_cast<int>(std::size(OK_OPTION)), 0, [](int) {});
            requestUpdate();
          })
          .withManagedEnumPicker());
#endif
  // Keep the existing CrossMux OTA proxy flow. Build-only boards compile this
  // UI but are intentionally absent from release assets in this sync.
  systemSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_PLUGINS, SettingAction::Plugins));
  systemSettings.push_back(SettingInfo::Action(StrId::STR_KEYBOARD_LAYOUTS, SettingAction::KeyboardLayouts));
  if (usesAccordion()) {
    systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
    systemSettings.push_back(SettingInfo::Action(StrId::STR_ABOUT, SettingAction::About));
  } else {
    systemSettings.push_back(SettingInfo::Action(StrId::STR_ABOUT, SettingAction::About));
    systemSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  }
  readerSettings.insert(readerSettings.begin(),
                        SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
  readerSettings.insert(readerSettings.begin() + 1,
                        SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
  readerSettings.insert(readerSettings.begin() + 2,
                        SettingInfo::Action(StrId::STR_MANAGE_DICTIONARIES, SettingAction::ManageDictionaries));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
  readerSettings.push_back(SettingInfo::Action(StrId::STR_READING_STATS, SettingAction::ReadingStatsSettings));

  // Update currentSettings pointer and count for the active category
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
  rebuildRowItems();
}

void SettingsActivity::onEnter() {
  UiTabListActivity::onEnter();

  // Reset selection to first category (ring position 0, the tab bar, comes
  // from the base's per-tab nav reset)
  selectedCategoryIndex = 0;
  expandedCategories = 0;
  resourcesDirty = true;
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  rebuildSettingsLists();
}

void SettingsActivity::selectCategory(const int categoryIndex) {
  RenderLock lock(*this);
  closeRouting();
  selectedCategoryIndex = categoryIndex;
  switch (selectedCategoryIndex) {
    case 0:
      currentSettings = &displaySettings;
      break;
    case 1:
      currentSettings = &readerSettings;
      break;
    case 2:
      currentSettings = &controlsSettings;
      break;
    case 3:
      currentSettings = &systemSettings;
      break;
  }
  settingsCount = static_cast<int>(currentSettings->size());
  activeNav().top = 0;  // category switches start the list at the top (no per-tab memory here)
  rebuildRowItems();
}

// Rebuilds rowValues_/rowItems_ (label + actionValue) for *currentSettings.
// Structural — call only when the active category or a category's setting
// list changes, never from buildScreen(), which only refreshes rowValues_
// content and rowItems_[].value pointers in place.
void SettingsActivity::rebuildRowItems() {
  if (usesAccordion()) {
    rebuildAccordionRows();
    return;
  }
  const auto& settings = *currentSettings;
  rowValues_.assign(settings.size(), std::string());
  rowItems_.clear();
  rowItems_.reserve(settings.size());
  for (size_t i = 0; i < settings.size(); i++) {
    fui::ListItem item;
    item.label = I18N.get(settings[i].nameId);
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

int SettingsActivity::listCount() const {
  return usesAccordion() ? InxAccordionGeometry::visibleCount(accordionSettingCounts(), expandedCategories)
                         : settingsCount;
}

freeink::ui::ListNav& SettingsActivity::activeNav() { return usesAccordion() ? nav : UiTabListActivity::activeNav(); }

void SettingsActivity::rebuildAccordionRows() {
  const auto counts = accordionSettingCounts();
  const int count = InxAccordionGeometry::visibleCount(counts, expandedCategories);
  rowLabels_.resize(static_cast<size_t>(count));
  rowValues_.resize(static_cast<size_t>(count));
  rowItems_.clear();
  rowItems_.reserve(static_cast<size_t>(count));
  for (int index = 0; index < count; ++index) {
    const auto row = InxAccordionGeometry::rowAt(counts, expandedCategories, index);
    const bool category = row.isCategory();
    rowLabels_[index] = category ? I18N.get(categoryNames[row.category])
                                 : std::string("  ") + I18N.get(settingsForCategory(row.category)[row.setting].nameId);
    fui::ListItem item;
    item.label = rowLabels_[index].c_str();
    item.actionValue = static_cast<int16_t>(index);
    if (category) item.state = fui::StateEmphasized;
    rowItems_.push_back(item);
  }
}

void SettingsActivity::onTabAction(const int index) {
  if (optionPopup.isActive()) return;
  selectCategory(index);
  activeNav().selected = 0;  // tab taps land with the tab bar focused
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
}

void SettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  if (usesAccordion()) {
    const auto row = InxAccordionGeometry::rowAt(accordionSettingCounts(), expandedCategories, index);
    if (row.isCategory())
      toggleAccordionCategory(row.category);
    else
      toggleAccordionSetting(row.category, row.setting);
    app.clearTapFlash();
    return;
  }
  // Most rows repaint a different surface (popup, sub-activity, new value);
  // a lingering tap flash would gray an unrelated element.
  app.clearTapFlash();
  toggleSetting((*currentSettings)[index]);
  // Tap-first: a tapped row is not a cursor position. Leaving it focused
  // (inverted) after the tap meant the row stayed black once its sub-screen or
  // popup closed, and Back then had to clear that focus before a second Back
  // left Settings. Hand the focus back to the tab band; the viewport stays put.
  if (mappedInput.hasTouch()) {
    activeNav().selected = 0;
  }
}

void SettingsActivity::onRowAction(const fui::ActionEvent& event) {
  if (!usesAccordion()) {
    UiTabListActivity::onRowAction(event);
    return;
  }
  nav.selected = event.value;
  activateIndex(event.value);
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::applyUiSettingChange(uint8_t CrossPointSettings::* valuePtr) {
  // Theme changes take effect immediately, on this screen — reload the theme
  // and re-derive the app's tokens so the very next repaint is in the new look.
  if (valuePtr != &CrossPointSettings::uiTheme) {
    return;
  }
  {
    RenderLock lock(*this);
    UITheme::getInstance().reload();
    expandedCategories = 0;
    clearContentSelection();
    nav.reset();
    resetUi();
  }
  rebuildSettingsLists();
}

bool SettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void SettingsActivity::navigateButtons() {
  if (usesAccordion()) {
    UiListActivity::navigateButtons();
    return;
  }
  UiTabListActivity::navigateButtons();
}

void SettingsActivity::stepTab(const int direction) {
  // Ring position 0 stays on the tab bar; a row selection collapses to the
  // new category's first row (per-tab memory is deliberately not kept here).
  const bool onTabBar = ringPos() == 0;
  selectedCategoryIndex = direction > 0 ? ButtonNavigator::nextIndex(selectedCategoryIndex, categoryCount)
                                        : ButtonNavigator::previousIndex(selectedCategoryIndex, categoryCount);
  selectCategory(selectedCategoryIndex);
  activeNav().selected = onTabBar ? 0 : 1;
  requestUpdate();
}

bool SettingsActivity::handleButtons() {
  if (usesAccordion()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activateIndex(nav.selected);
      return true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      if (expandedCategories != 0) {
        RenderLock lock(*this);
        closeRouting();
        const auto row = InxAccordionGeometry::rowAt(accordionSettingCounts(), expandedCategories, nav.selected);
        const uint8_t mask = row.category >= 0 ? static_cast<uint8_t>(uint8_t{1} << row.category) : 0;
        expandedCategories =
            (mask != 0 && (expandedCategories & mask) != 0) ? static_cast<uint8_t>(expandedCategories & ~mask) : 0;
        rebuildAccordionRows();
        nav.selected = std::min<int>(nav.selected, listCount() - 1);
        nav.follow(listCount());
        requestUpdate();
      }
      return true;
    }
    return false;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      stepTab(1);
    } else {
      activateIndex(ringPos() - 1);
      requestUpdate();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (ringPos() > 0) {
      activeNav().selected = 0;
      requestUpdate();
    } else {
      SETTINGS.saveToFile();
      onGoHome();
    }
    return true;
  }

  return false;
}

bool SettingsActivity::usesAccordion() const { return UITheme::getInstance().hasMainTabs(); }

const std::vector<SettingInfo>& SettingsActivity::settingsForCategory(const int categoryIndex) const {
  switch (categoryIndex) {
    case 0:
      return displaySettings;
    case 1:
      return readerSettings;
    case 2:
      return controlsSettings;
    case 3:
      return systemSettings;
  }
  return displaySettings;
}

std::array<int, SettingsActivity::categoryCount> SettingsActivity::accordionSettingCounts() const {
  return {static_cast<int>(displaySettings.size()), static_cast<int>(readerSettings.size()),
          static_cast<int>(controlsSettings.size()), static_cast<int>(systemSettings.size())};
}

void SettingsActivity::toggleAccordionCategory(const int categoryIndex) {
  const auto started = millis();
  if (categoryIndex < 0 || categoryIndex >= categoryCount) return;
  const uint8_t mask = static_cast<uint8_t>(uint8_t{1} << categoryIndex);
  RenderLock lock(*this);
  const auto locked = millis();
  closeRouting();
  expandedCategories ^= mask;
  rebuildAccordionRows();
  nav.selected = InxAccordionGeometry::categoryRow(accordionSettingCounts(), expandedCategories, categoryIndex);
  nav.follow(listCount());
  requestUpdate();
  LOG_DBG("SET", "Category %d expanded=%u rows=%d handler=%lu lockWait=%lu ms", categoryIndex,
          static_cast<unsigned>(expandedCategories), listCount(), millis() - started, locked - started);
}

void SettingsActivity::toggleAccordionSetting(const int categoryIndex, const int settingIndex) {
  if (categoryIndex < 0 || categoryIndex >= categoryCount) return;
  const auto& settings = settingsForCategory(categoryIndex);
  if (settingIndex < 0 || settingIndex >= static_cast<int>(settings.size())) return;
  toggleSetting(settings[settingIndex]);
  requestUpdate();
}

void SettingsActivity::toggleSetting(const SettingInfo& setting) {
  mappedInput.resetHomeButtonInput();
  const auto changedValuePtr = setting.valuePtr;
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.valuePtr == &CrossPointSettings::readingBackgroundEnabled) {
    openReadingBackgroundMenu();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    const auto enumLabels = setting.enumLabels();
    if (enumLabels.size() > 2) {
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(setting.nameId, enumLabels.data(), static_cast<int>(enumLabels.size()), currentValue,
                       [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
                         SETTINGS.*valuePtr = idx;
                         syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
                         SETTINGS.saveToFile();
                         refreshAfterSettingChange(valuePtr);
                       });
      requestUpdate();
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(enumLabels.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumLabels().size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    if (totalValues > 2 || setting.managedEnumPicker) {
      const auto valueSetter = setting.valueSetter;
      const bool managedPicker = setting.managedEnumPicker;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged, managedPicker,
                       cur](const int idx) {
        if (idx == cur) return;
        valueSetter(static_cast<uint8_t>(idx));
        if (managedPicker) return;
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        requestUpdate();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        const auto enumLabels = setting.enumLabels();
        optionPopup.show(setting.nameId, enumLabels.data(), static_cast<int>(enumLabels.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    // Every child page returns here through ActivityManager's Pop; make sure the
    // settings list is both rebuilt (values may have changed) and repainted.
    auto resultHandler = [this](const ActivityResult&) {
      SETTINGS.saveToFile();
      rebuildSettingsLists();
      requestUpdate();
    };

    switch (setting.action) {
      case SettingAction::HomeButton: {
        // Activities must outlive this call and are owned by the activity stack.
        auto activity = makeUniqueNoThrow<HomeButtonSettingsActivity>(renderer, mappedInput);
        if (!activity) {
          LOG_ERR("SET", "OOM: Home button settings");
          return;
        }
        startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
        return;
      }
      case SettingAction::RemapFrontButtons:
        startActivityForResultWith<ButtonRemapActivity>(resultHandler);
        break;
      case SettingAction::Bluetooth:
#if FREEINK_CAP_BLE_HID_HOST
        releaseListsForMemoryHungryChild();
        if (!startActivityForResultWith<BluetoothSettingsActivity>(resultHandler)) {
          rebuildSettingsLists();
          requestUpdate();
        }
#endif
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResultWith<StatusBarSettingsActivity>(resultHandler);
        break;
      case SettingAction::ReadingStatsSettings:
        startActivityForResultWith<ReadingStatsSettingsActivity>(resultHandler);
        break;
      case SettingAction::AppVisibility:
        startActivityForResultWith<AppVisibilitySettingsActivity>(resultHandler);
        break;
      case SettingAction::ClockSettings:
        if (auto activity = makeUniqueNoThrow<ClockSettingsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), resultHandler);
        } else {
          LOG_ERR("SETTINGS", "OOM: ClockSettingsActivity");
        }
        break;
      case SettingAction::KOReaderSync:
        startActivityForResultWith<KOReaderSettingsActivity>(resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResultWith<OpdsServerListActivity>(resultHandler);
        break;
      case SettingAction::Network: {
        auto activity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, false);
        if (!activity) {
          LOG_ERR("SETTINGS", "OOM: WifiSelectionActivity");
          return;
        }
        startActivityForResult(std::move(activity), [](const ActivityResult&) {
          SETTINGS.saveToFile();
          // Every other WiFi consumer hands the radio to a session it owns;
          // these rows only save credentials, so nothing here would ever
          // release the driver's heap. The scan alone brings it up, so tear
          // down whether or not the user joined a network.
          if (WiFi.getMode() == WIFI_MODE_NULL) return;
          WiFi.disconnect(false);
          delay(30);
          // Unlike the onExit() teardowns, this runs from the loop task with
          // no lock held; the restart popup paints straight to the panel.
          RenderLock lock;
          silentRestartToSettings();
        });

        break;
      }
      case SettingAction::ClearCache:
        startActivityForResultWith<ClearCacheActivity>(resultHandler);
        break;
      case SettingAction::RestoreSystemSettings:
        confirmRestoreSystemSettings();
        break;
      case SettingAction::CheckForUpdates:
        openOtaUpdate();
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResultWith<SdFirmwareUpdateActivity>(resultHandler);
        break;
      case SettingAction::DownloadFonts:
        releaseListsForMemoryHungryChild();
        if (!startActivityForResultWith<FontDownloadActivity>(resultHandler)) {
          rebuildSettingsLists();
          requestUpdate();
        }
        break;
      case SettingAction::ManageDictionaries:
        startActivityForResultWith<DictionaryDownloadActivity>([this](const ActivityResult&) {
          resourcesDirty = true;
          SETTINGS.saveToFile();
          rebuildSettingsLists();
          requestUpdate();
        });
        break;
      case SettingAction::TextSettings:
        startActivityForResultWith<TextSettingsActivity>(
            [this](const ActivityResult&) {
              // TextSettingsActivity saves on each change; no save needed here.
              {
                RenderLock lock(*this);
                sdFontSystem.ensureLoaded(renderer);
              }
              rebuildSettingsLists();
              requestUpdate();
            },
            &sdFontSystem.registry(), TextSettingsActivity::Tab::Family);
        break;
      case SettingAction::Language:
        // Row labels are translated once in rebuildRowItems() and don't
        // re-run on Pop (see ActivityManager::loop()), so a language switch
        // needs an explicit rebuild here rather than the generic resultHandler.
        startActivityForResultWith<LanguageSelectActivity>([this](const ActivityResult&) {
          SETTINGS.saveToFile();
          rebuildSettingsLists();
        });
        break;
      case SettingAction::About:
        startActivityForResultWith<AboutActivity>(resultHandler);
        break;
      case SettingAction::Plugins:
        startActivityForResultWith<PluginCatalogActivity>(resultHandler);
        break;
      case SettingAction::KeyboardLayouts:
        if (auto activity = makeUniqueNoThrow<KeyboardLayoutsActivity>(renderer, mappedInput)) {
          startActivityForResult(std::move(activity), nullptr);
        } else {
          LOG_ERR("SETTINGS", "OOM: KeyboardLayoutsActivity");
        }
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  refreshAfterSettingChange(changedValuePtr);
}

void SettingsActivity::refreshAfterSettingChange(uint8_t CrossPointSettings::* valuePtr) {
  if (valuePtr == &CrossPointSettings::uiTheme) {
    applyUiSettingChange(valuePtr);
  } else if (valuePtr == &CrossPointSettings::shortPwrBtn) {
    rebuildSettingsLists();
    activeNav().selected = std::min(activeNav().selected.load(), std::max(0, listCount() - (usesAccordion() ? 1 : 0)));
    activeNav().follow(listCount());
  }
  requestUpdate();
}

void SettingsActivity::confirmRestoreSystemSettings() {
  const bool started = startActivityForResultWith<ConfirmationActivity>(
      [this](const ActivityResult& firstResult) {
        if (firstResult.isCancelled) return;

        const bool finalStarted = startActivityForResultWith<ConfirmationActivity>(
            [this](const ActivityResult& finalResult) {
              if (finalResult.isCancelled) return;
              if (systemSettingsReset::clearPersistedSettings()) {
                silentRestart();
                return;
              }
              optionPopup.show(StrId::STR_RESTORE_SYSTEM_SETTINGS_FAILED, OK_OPTION,
                               static_cast<int>(std::size(OK_OPTION)), 0, [](int) {});
              requestUpdate();
            },
            tr(STR_RESTORE_SYSTEM_SETTINGS), tr(STR_RESTORE_SYSTEM_SETTINGS_FINAL_WARNING));
        if (finalStarted) return;
        optionPopup.show(StrId::STR_MEMORY_ERROR, OK_OPTION, static_cast<int>(std::size(OK_OPTION)), 0, [](int) {});
        requestUpdate();
      },
      tr(STR_RESTORE_SYSTEM_SETTINGS), tr(STR_RESTORE_SYSTEM_SETTINGS_WARNING));
  if (started) return;
  optionPopup.show(StrId::STR_MEMORY_ERROR, OK_OPTION, static_cast<int>(std::size(OK_OPTION)), 0, [](int) {});
  requestUpdate();
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}

void SettingsActivity::releaseListsForMemoryHungryChild() {
  resourcesDirty = true;
  RenderLock lock(*this);
  closeRouting();
  // clear() keeps vector capacity; swap it out so a stacked Settings activity
  // leaves that heap to the child.
  std::vector<freeink::ui::ListItem>().swap(rowItems_);
  std::vector<std::string>().swap(rowValues_);
  std::vector<std::string>().swap(rowLabels_);
  std::vector<SettingInfo>().swap(displaySettings);
  std::vector<SettingInfo>().swap(readerSettings);
  std::vector<SettingInfo>().swap(controlsSettings);
  std::vector<SettingInfo>().swap(systemSettings);
  settingsCount = 0;
}

void SettingsActivity::openOtaUpdate() {
  releaseListsForMemoryHungryChild();

  if (startActivityForResultWith<OtaUpdateActivity>([this](const ActivityResult&) {
        SETTINGS.saveToFile();
        rebuildSettingsLists();
      })) {
    return;
  }

  rebuildSettingsLists();
  requestUpdate();
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResultWith<IntervalSelectionActivity>(
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      },
      "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
      CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
      StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, StrId::STR_SLEEP_NEVER);
}

void SettingsActivity::openReadingBackgroundMenu() {
  static constexpr StrId OPTIONS[] = {StrId::STR_STATE_OFF, StrId::STR_CUSTOM_IMAGE};
  optionPopup.show(StrId::STR_READING_BACKGROUND, OPTIONS, static_cast<int>(std::size(OPTIONS)),
                   SETTINGS.readingBackgroundEnabled ? 1 : 0, [this](const int selected) {
                     if (selected == 0) {
                       SETTINGS.readingBackgroundEnabled = 0;
                       SETTINGS.saveToFile();
                       requestUpdate();
                       return;
                     }
                     openReadingBackgroundPicker();
                   });
  requestUpdate();
}

void SettingsActivity::openReadingBackgroundPicker() {
  const bool started = startActivityForResultWith<FileBrowserActivity>(
      [this](const ActivityResult& result) {
        if (result.isCancelled) return;
        const auto* selected = std::get_if<FilePathResult>(&result.data);
        if (!selected) {
          LOG_ERR("SET", "PNG picker returned no path");
          return;
        }

        GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
        if (readingBackground::createCacheFromPng(renderer, selected->path.c_str())) {
          SETTINGS.readingBackgroundEnabled = 1;
          SETTINGS.saveToFile();
          requestUpdate();
        } else {
          optionPopup.show(StrId::STR_FAILED_LOWER, OK_OPTION, static_cast<int>(std::size(OK_OPTION)), 0, [](int) {});
        }
      },
      "/", FileBrowserActivity::Mode::PickPng);
  if (!started) {
    optionPopup.show(StrId::STR_MEMORY_ERROR, OK_OPTION, static_cast<int>(std::size(OK_OPTION)), 0, [](int) {});
    requestUpdate();
  }
}

std::string SettingsActivity::settingValueText(const SettingInfo& setting) {
  if (setting.valuePtr == &CrossPointSettings::readingBackgroundEnabled) {
    return SETTINGS.readingBackgroundEnabled ? tr(STR_CUSTOM_IMAGE) : tr(STR_STATE_OFF);
  }
  if (setting.action == SettingAction::HomeButton) return tr(STR_CONFIGURE);
  if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    // Guard like the valueGetter branch below: a corrupt/migrated settings
    // byte must not index past the enum table.
    const uint8_t value = setting.valuePtr == &CrossPointSettings::uiTheme &&
                                  SETTINGS.uiTheme == CrossPointSettings::COVER_GRID && !UITheme::supportsCoverGrid()
                              ? CrossPointSettings::LYRA
                              : SETTINGS.*(setting.valuePtr);
    const auto enumLabels = setting.enumLabels();
    if (value >= enumLabels.size()) return "";
    return I18N.get(enumLabels[value]);
  }
  if (setting.type == SettingType::ENUM && setting.valueGetter) {
    const uint8_t value = setting.valueGetter();
    if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
      return setting.enumStringValues[value];
    }
    const auto enumLabels = setting.enumLabels();
    if (value < enumLabels.size()) {
      return I18N.get(enumLabels[value]);
    }
    return "";
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
      if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
        return tr(STR_SLEEP_NEVER);
      }
      char valueBuffer[32];
      snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
               static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
      return valueBuffer;
    }
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  return "";
}

void SettingsActivity::buildScreen(UiScreen& screen) {
  const auto applyCheckbox = [](const SettingInfo& setting, fui::ListItem& item) {
    item.toggle = false;
    const auto labels = setting.enumLabels();
    const bool checkbox = setting.type == SettingType::TOGGLE ||
                          (setting.type == SettingType::ENUM && setting.enumStringValues.empty() &&
                           labels.size() == 2 && labels[0] == StrId::STR_STATE_OFF && labels[1] == StrId::STR_STATE_ON);
    if (checkbox && (setting.valuePtr || setting.valueGetter)) {
      const bool checked = setting.valuePtr ? SETTINGS.*(setting.valuePtr) != 0 : setting.valueGetter() != 0;
      GUI.setCheckboxRow(item, checked);
    }
  };
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool boldChineseCategories = usesAccordion() && I18N.getLanguage() == Language::ZH_CN;
  const Rect content = pageContentRect();
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(content.y), static_cast<int16_t>(renderer.getScreenWidth() - content.x - content.width),
      static_cast<int16_t>(renderer.getScreenHeight() - content.y - content.height), static_cast<int16_t>(content.x)});

  if (usesAccordion()) {
    const auto counts = accordionSettingCounts();
    for (int i = 0; i < listCount(); ++i) {
      const auto row = InxAccordionGeometry::rowAt(counts, expandedCategories, i);
      rowValues_[i] = row.isCategory() ? ((expandedCategories & (uint8_t{1} << row.category)) != 0 ? "-" : "+")
                                       : settingValueText(settingsForCategory(row.category)[row.setting]);
      rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
      rowItems_[i].toggle = false;
      if (!row.isCategory()) applyCheckbox(settingsForCategory(row.category)[row.setting], rowItems_[i]);
    }
    fui::ListProps props;
    props.items = rowItems_.data();
    props.count = static_cast<uint16_t>(rowItems_.size());
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    props.labelText = screen.theme().bodyText;
    props.valueText = screen.theme().bodyText;
    syncListViewport(screen, props);
    if (!showMainTabContentSelection()) props.selectedIndex = -1;
    if (boldChineseCategories) {
      props.labelText.bold = false;
      props.valueText.bold = false;
    }
    GfxRenderer::SyntheticBoldScope syntheticBold(renderer, boldChineseCategories
                                                                ? CrossPointSettings::SYNTHETIC_BOLD_STANDARD
                                                                : CrossPointSettings::SYNTHETIC_BOLD_OFF);
    screen.list(props);
    return;
  }

  {
    GfxRenderer::SyntheticBoldScope syntheticBold(renderer, boldChineseCategories
                                                                ? CrossPointSettings::SYNTHETIC_BOLD_STANDARD
                                                                : CrossPointSettings::SYNTHETIC_BOLD_OFF);
    buildTabBar(screen, boldChineseCategories);
  }

  // rowItems_ (label/actionValue) was built by rebuildRowItems() when the
  // category was last selected/rebuilt; only the live value text needs
  // refreshing here, by assigning into the existing rowValues_ strings (no
  // vector growth) rather than building a new items/values vector on every
  // render.
  const auto& settings = *currentSettings;
  for (size_t i = 0; i < settings.size(); i++) {
    rowValues_[i] = settingValueText(settings[i]);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
    rowItems_[i].toggle = false;
    applyCheckbox(settings[i], rowItems_[i]);
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Titles match the value's font size (smallText) so both sides of a row
  // read as one unit; labels that still don't fit wrap onto a second line.
  // maxLines=2 also marks the style explicitly set (an all-default smallText
  // fails textStyleUnset and the list would substitute bodyText back); the
  // common fits-on-one-line case takes the renderer's fast path anyway.
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void SettingsActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  // Version rides in the header's trailing label slot: the footer position
  // conflicts with button hints on non-touch devices.
  drawPageHeader(Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);
}

void SettingsActivity::drawFooter() {
  if (usesAccordion()) {
    const auto labels = mainTabButtonLabels(tr(STR_BACK), tr(STR_TOGGLE), listCount() > 1);
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  const int ring = ringPos();
  const auto confirmLabel =
      (ring == 0) ? I18N.get(categoryNames[(selectedCategoryIndex + 1) % categoryCount])
                  : (ring > 0 && (*currentSettings)[ring - 1].nameId == StrId::STR_TIME_TO_SLEEP ? tr(STR_SELECT)
                                                                                                 : tr(STR_TOGGLE));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void SettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
#if LOG_LEVEL >= 2
  const auto started = millis();
  if (auto* fonts = renderer.getFontCacheManager()) fonts->resetStats();
#endif
  UiListActivity::render(std::move(lock));
#if LOG_LEVEL >= 2
  LOG_DBG("SET", "Settings render including display: %lu ms", millis() - started);
  if (auto* fonts = renderer.getFontCacheManager()) fonts->logStats("settings");
#endif
}
