#include "DateTimeSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/TimeUtils.h"

namespace fui = freeink::ui;

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
  resetUi();
  app.on(ACTION_STEP, &DateTimeSettingsActivity::onStep, this);
  app.on(ACTION_CANCEL, &DateTimeSettingsActivity::onCancel, this);
  app.on(ACTION_OK, &DateTimeSettingsActivity::onOk, this);
  app.setScreen(&DateTimeSettingsActivity::manualScreen, this);
  waitForConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  beginManualEdit();
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
  closeRouting();
  requestUpdate();
}

void DateTimeSettingsActivity::loop() {
  if (waitForConfirmRelease_) {
    waitForConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
    return;
  }
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancelManualEdit();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmManualEdit();
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

void DateTimeSettingsActivity::cancelManualEdit() {
  closeRouting();
  finish();
}

void DateTimeSettingsActivity::confirmManualEdit() {
  if (!applyManualTime()) return;
  closeRouting();
  finish();
}

void DateTimeSettingsActivity::manualScreen(UiScreen& screen, void* user) {
  auto& self = *static_cast<DateTimeSettingsActivity*>(user);
  self.buildManualScreen(screen);
}

void DateTimeSettingsActivity::buildManualScreen(UiScreen& screen) {
  if (!mappedInput.hasTouch()) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing),
      static_cast<int16_t>(metrics.contentSidePadding), 0, static_cast<int16_t>(metrics.contentSidePadding)});
  addDialogCancelOk(screen, ACTION_CANCEL, ACTION_OK);

  static constexpr StrId names[] = {StrId::STR_YEAR, StrId::STR_MONTH, StrId::STR_DAY, StrId::STR_HOUR,
                                    StrId::STR_MINUTE};
  char values[EDIT_FIELD_COUNT][5];
  snprintf(values[0], sizeof(values[0]), "%d", year);
  snprintf(values[1], sizeof(values[1]), "%02u", month);
  snprintf(values[2], sizeof(values[2]), "%02u", day);
  snprintf(values[3], sizeof(values[3]), "%02u", hour);
  snprintf(values[4], sizeof(values[4]), "%02u", minute);
  for (int index = 0; index < EDIT_FIELD_COUNT; ++index) {
    fui::StepperRowProps props;
    props.row.label = I18N.get(names[index]);
    props.row.labelText = screen.theme().bodyText;
    props.row.valueText = screen.theme().bodyText;
    props.row.minTouchSize = screen.theme().minTouchSize;
    props.row.state = index == selectedEditField ? fui::StateFocused : fui::StateNormal;
    props.value = values[index];
    props.widestValue = index == 0 ? "2099" : "00";
    props.decrement = ACTION_STEP;
    props.increment = ACTION_STEP;
    props.decrementValue = static_cast<int16_t>(-(index + 1));
    props.incrementValue = static_cast<int16_t>(index + 1);
    screen.stepperRow(props);
  }
}

void DateTimeSettingsActivity::onStep(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<DateTimeSettingsActivity*>(user);
  if (event.value == 0 || event.value < -EDIT_FIELD_COUNT || event.value > EDIT_FIELD_COUNT) return;
  self->selectedEditField = event.value > 0 ? event.value - 1 : -event.value - 1;
  self->adjustEditField(event.value > 0 ? 1 : -1);
}

void DateTimeSettingsActivity::onCancel(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<DateTimeSettingsActivity*>(user);
  self->app.clearTapFlash();
  self->cancelManualEdit();
}

void DateTimeSettingsActivity::onOk(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<DateTimeSettingsActivity*>(user);
  self->app.clearTapFlash();
  self->confirmManualEdit();
}

void DateTimeSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SET_DATE_AND_TIME));
  if (mappedInput.hasTouch()) {
    renderUi();
  } else {
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
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
