#include "DateTimeSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "ClockOffsetActivity.h"
#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace {

constexpr int MIN_YEAR = 2024;
constexpr int MAX_YEAR = 2099;

unsigned wrapValue(const unsigned value, const int delta, const unsigned minValue, const unsigned maxValue) {
  const int range = static_cast<int>(maxValue - minValue + 1);
  int offset = static_cast<int>(value - minValue) + delta;
  offset %= range;
  if (offset < 0) offset += range;
  return minValue + static_cast<unsigned>(offset);
}

std::string twoDigits(const unsigned value) {
  char buffer[4];
  snprintf(buffer, sizeof(buffer), "%02u", value);
  return buffer;
}

}  // namespace

void DateTimeSettingsActivity::onEnter() {
  Activity::onEnter();
  if (SETTINGS.clockUtcOffsetQ > 104) SETTINGS.clockUtcOffsetQ = 48;
  if (SETTINGS.clockFormat > 1) SETTINGS.clockFormat = 0;
  if (SETTINGS.clockAutoSync > 1) SETTINGS.clockAutoSync = 1;
  halClock.setAutoSyncEnabled(SETTINGS.clockAutoSync != 0);
  mode = Mode::Menu;
  selectedMenuItem = 0;
  requestUpdate();
}

void DateTimeSettingsActivity::onExit() {
  SETTINGS.saveToFile();
  Activity::onExit();
}

void DateTimeSettingsActivity::loop() {
  switch (mode) {
    case Mode::Menu:
      loopMenu();
      break;
    case Mode::ManualEdit:
      loopManualEdit();
      break;
  }
}

void DateTimeSettingsActivity::loopMenu() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  switch (handleListTouch(selectedMenuItem, MENU_ITEM_COUNT, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      activateMenuItem();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateMenuItem();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedMenuItem = ButtonNavigator::nextIndex(selectedMenuItem, MENU_ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedMenuItem = ButtonNavigator::previousIndex(selectedMenuItem, MENU_ITEM_COUNT);
    requestUpdate();
  });
}

void DateTimeSettingsActivity::activateMenuItem() {
  const auto item = static_cast<MenuItem>(selectedMenuItem);
  switch (item) {
    case MenuItem::AutoTime:
      SETTINGS.clockAutoSync = SETTINGS.clockAutoSync ? 0 : 1;
      halClock.setAutoSyncEnabled(SETTINGS.clockAutoSync != 0);
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case MenuItem::DateTime:
      if (!SETTINGS.clockAutoSync) beginManualEdit();
      break;
    case MenuItem::TimeZone: {
      // ActivityManager owns the picker across frames; stack lifetime is insufficient.
      auto activity = makeUniqueNoThrow<ClockOffsetActivity>(renderer, mappedInput);
      if (!activity) {
        LOG_ERR("CLK", "OOM: ClockOffsetActivity (%u bytes)", static_cast<unsigned>(sizeof(ClockOffsetActivity)));
        return;
      }
      startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
      break;
    }
    case MenuItem::Hour24:
      SETTINGS.clockFormat = SETTINGS.clockFormat == 0 ? 1 : 0;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case MenuItem::SyncNow: {
      // ActivityManager owns the sync screen across frames; stack lifetime is insufficient.
      auto activity = makeUniqueNoThrow<ClockSyncActivity>(renderer, mappedInput);
      if (!activity) {
        LOG_ERR("CLK", "OOM: ClockSyncActivity (%u bytes)", static_cast<unsigned>(sizeof(ClockSyncActivity)));
        return;
      }
      startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
      break;
    }
    case MenuItem::Count:
      break;
  }
}

void DateTimeSettingsActivity::beginManualEdit() {
  std::tm local{};
  const uint32_t now = TimeUtils::getCurrentValidTimestamp();
  if (now && TimeUtils::getLocalDateTime(now, local)) {
    year = std::clamp(local.tm_year + 1900, MIN_YEAR, MAX_YEAR);
    month = static_cast<unsigned>(local.tm_mon + 1);
    day = static_cast<unsigned>(local.tm_mday);
    hour = static_cast<unsigned>(local.tm_hour);
    minute = static_cast<unsigned>(local.tm_min);
  } else {
    year = MIN_YEAR;
    month = 1;
    day = 1;
    hour = 0;
    minute = 0;
  }
  selectedEditField = 0;
  mode = Mode::ManualEdit;
  requestUpdate();
}

void DateTimeSettingsActivity::loopManualEdit() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    mode = Mode::Menu;
    requestUpdate();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (applyManualTime()) {
      mode = Mode::Menu;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this] {
    selectedEditField = ButtonNavigator::nextIndex(selectedEditField, EDIT_FIELD_COUNT);
    requestUpdate();
  });
  buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this] {
    selectedEditField = ButtonNavigator::previousIndex(selectedEditField, EDIT_FIELD_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustEditField(-1); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustEditField(1); });
}

void DateTimeSettingsActivity::adjustEditField(const int delta) {
  const auto field = static_cast<EditField>(selectedEditField);
  switch (field) {
    case EditField::Year:
      year = std::clamp(year + delta, MIN_YEAR, MAX_YEAR);
      day = std::min(day, TimeUtils::getDaysInMonth(year, month));
      break;
    case EditField::Month:
      month = wrapValue(month, delta, 1, 12);
      day = std::min(day, TimeUtils::getDaysInMonth(year, month));
      break;
    case EditField::Day:
      day = wrapValue(day, delta, 1, TimeUtils::getDaysInMonth(year, month));
      break;
    case EditField::Hour:
      hour = wrapValue(hour, delta, 0, 23);
      break;
    case EditField::Minute:
      minute = wrapValue(minute, delta, 0, 59);
      break;
    case EditField::Count:
      break;
  }
  requestUpdate();
}

bool DateTimeSettingsActivity::applyManualTime() {
  uint32_t epoch = 0;
  if (!TimeUtils::localDateTimeToUtcEpoch(year, month, day, hour, minute, epoch) ||
      !halClock.setUtcTime(static_cast<time_t>(epoch))) {
    LOG_ERR("CLK", "Rejected manual date/time");
    return false;
  }
  return true;
}

void DateTimeSettingsActivity::render(RenderLock&&) {
  switch (mode) {
    case Mode::Menu:
      renderMenu();
      break;
    case Mode::ManualEdit:
      renderManualEdit();
      break;
  }
}

void DateTimeSettingsActivity::renderMenu() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DATE_AND_TIME));
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, MENU_ITEM_COUNT, selectedMenuItem,
      [](int index) {
        static constexpr StrId NAMES[] = {StrId::STR_AUTO_TIME, StrId::STR_DATE_AND_TIME, StrId::STR_TIME_ZONE,
                                          StrId::STR_24_HOUR_TIME, StrId::STR_CLOCK_SYNC_NOW};
        return std::string(I18N.get(NAMES[index]));
      },
      nullptr, nullptr,
      [](int index) {
        const auto item = static_cast<MenuItem>(index);
        switch (item) {
          case MenuItem::AutoTime:
            return std::string(SETTINGS.clockAutoSync ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
          case MenuItem::DateTime: {
            char buffer[24];
            return TimeUtils::formatCurrentDateTime(buffer, sizeof(buffer), SETTINGS.clockFormat == 1)
                       ? std::string(buffer)
                       : std::string(tr(STR_NOT_SET));
          }
          case MenuItem::TimeZone: {
            char buffer[16];
            TimeUtils::formatUtcOffset(SETTINGS.clockUtcOffsetQ, buffer, sizeof(buffer));
            return std::string(buffer);
          }
          case MenuItem::Hour24:
            return std::string(SETTINGS.clockFormat == 0 ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
          case MenuItem::SyncNow:
            switch (halClock.syncState()) {
              case ClockSyncState::Idle:
                return std::string();
              case ClockSyncState::Syncing:
                return std::string(tr(STR_CLOCK_SYNCING));
              case ClockSyncState::Succeeded:
                return std::string(tr(STR_CLOCK_SYNC_OK));
              case ClockSyncState::Failed:
                return std::string(tr(STR_CLOCK_SYNC_FAIL));
            }
          case MenuItem::Count:
            return std::string();
        }
        return std::string();
      },
      true, [](int index) { return index == static_cast<int>(MenuItem::DateTime) && SETTINGS.clockAutoSync; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void DateTimeSettingsActivity::renderManualEdit() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SET_DATE_AND_TIME));
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, EDIT_FIELD_COUNT, selectedEditField,
      [](int index) {
        static constexpr StrId NAMES[] = {StrId::STR_YEAR, StrId::STR_MONTH, StrId::STR_DAY, StrId::STR_HOUR,
                                          StrId::STR_MINUTE};
        return std::string(I18N.get(NAMES[index]));
      },
      nullptr, nullptr,
      [this](int index) {
        switch (static_cast<EditField>(index)) {
          case EditField::Year:
            return std::to_string(year);
          case EditField::Month:
            return twoDigits(month);
          case EditField::Day:
            return twoDigits(day);
          case EditField::Hour:
            return twoDigits(hour);
          case EditField::Minute:
            return twoDigits(minute);
          case EditField::Count:
            return std::string();
        }
        return std::string();
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
