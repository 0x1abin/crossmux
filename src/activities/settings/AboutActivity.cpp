#include "AboutActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalFrontlight.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>

#include <cassert>
#include <cmath>
#include <cstdio>

#include "I18n.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
// Chip part numbers, not user prose — deliberately untranslated.
const char* displayControllerName(const BoardConfig::DisplayController c) {
  switch (c) {
    case BoardConfig::DisplayController::SSD1677:
      return "SSD1677";
    case BoardConfig::DisplayController::UC8253:
      return "UC8253";
    case BoardConfig::DisplayController::ED2208:
      return "ED2208";
    case BoardConfig::DisplayController::LgfxEpd:
      return "LovyanGFX EPD";
    case BoardConfig::DisplayController::IT8951:
      return "IT8951";
    case BoardConfig::DisplayController::UC8279:
      return "UC8279";
    case BoardConfig::DisplayController::UC8179:
      return "UC8179";
    case BoardConfig::DisplayController::UC8279C:
      return "UC8279C";
  }
  return "?";
}

const char* touchControllerName(const BoardConfig::TouchController c) {
  switch (c) {
    case BoardConfig::TouchController::None:
      return nullptr;
    case BoardConfig::TouchController::Chsc6x:
      return "CHSC6X";
    case BoardConfig::TouchController::Gt911:
      return "GT911";
    case BoardConfig::TouchController::Ft5x06:
      return "FT5x06";
    case BoardConfig::TouchController::Ft6336u:
      return "FT6336U";
    case BoardConfig::TouchController::Gslx680:
      return "GSLX680";
  }
  return nullptr;
}
}  // namespace

AboutActivity::AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("About", renderer, mappedInput) {}

const char* AboutActivity::headerTitle() const { return tr(STR_ABOUT); }

void AboutActivity::onEnter() {
  UiListActivity::onEnter();
  // Fixed activity-owned slots replace both About implementations; no per-render snapshot allocations.
  rowCount_ = 0;
  const auto add = [this](const char* label, const char* value) {
    assert(rowCount_ < ITEM_COUNT);
    rowItems_[rowCount_].label = label;
    rowItems_[rowCount_].actionValue = static_cast<int16_t>(rowCount_);
    rowValues_[rowCount_++] = value;
  };
  char buf[64];
  const auto heap = HalSystem::getHeapInfo();
  add(tr(STR_ABOUT_FIRMWARE_NAME), tr(STR_CROSSPOINT));
  add(tr(STR_ABOUT_FIRMWARE_VERSION), CROSSPOINT_VERSION);
  add(tr(STR_ABOUT_DEVICE_MODEL), BoardConfig::ACTIVE.name);
#ifdef SIMULATOR
  add(tr(STR_ABOUT_CHIP_MODEL), "Simulator");
#else
  add(tr(STR_ABOUT_CHIP_MODEL), HalSystem::getChipModel());
#endif
#ifndef SIMULATOR
  float temperature = 0;
  if (HalSystem::getChipTemperatureCelsius(temperature)) {
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(temperature)));
    add(tr(STR_ABOUT_CHIP_TEMPERATURE), buf);
  } else
#endif
  {
    add(tr(STR_ABOUT_CHIP_TEMPERATURE), tr(STR_NOT_AVAILABLE));
  }
  HalSystem::DeviceId mac{};
#ifdef SIMULATOR
  const bool macAvailable = HalSystem::getDeviceId(mac);
#else
  const bool macAvailable = HalSystem::getWifiStationMac(mac);
#endif
  if (macAvailable) {
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    add(tr(STR_ABOUT_WIFI_MAC_ADDRESS), buf);
  } else {
    add(tr(STR_ABOUT_WIFI_MAC_ADDRESS), tr(STR_NOT_AVAILABLE));
  }
#ifdef SIMULATOR
  const uint64_t minutes = millis() / 60000;
#else
  const uint64_t minutes = HalSystem::getUptimeSeconds() / 60;
#endif
  snprintf(buf, sizeof(buf), "%llu:%02llu:%02llu", static_cast<unsigned long long>(minutes / 1440),
           static_cast<unsigned long long>(minutes / 60 % 24), static_cast<unsigned long long>(minutes % 60));
  add(tr(STR_ABOUT_UPTIME), buf);
  snprintf(buf, sizeof(buf), "%lu / %lu", static_cast<unsigned long>(heap.freeBytes / 1024),
           static_cast<unsigned long>(heap.totalBytes / 1024));
  add(tr(STR_ABOUT_HEAP_FREE_TOTAL), buf);
  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(heap.largestFreeBlockBytes / 1024));
  add(tr(STR_ABOUT_LARGEST_HEAP_BLOCK), buf);
  if (heap.totalPsramBytes > 0) {
    snprintf(buf, sizeof(buf), "%lu / %lu", static_cast<unsigned long>(heap.freePsramBytes / 1024),
             static_cast<unsigned long>(heap.totalPsramBytes / 1024));
    add(tr(STR_ABOUT_PSRAM_HEAP_FREE_TOTAL), buf);
  }
  // Hardware support labels keep the existing language-independent spelling.
  snprintf(buf, sizeof(buf), "%u MB", static_cast<unsigned>(ESP.getFlashChipSize() / (1024u * 1024u)));
  add("Flash", buf);
  add("Display Controller", displayControllerName(BoardConfig::ACTIVE.displayController));
  snprintf(buf, sizeof(buf), "%ux%u", BoardConfig::ACTIVE.displayWidth, BoardConfig::ACTIVE.displayHeight);
  add("Resolution", buf);
  const char* touch = touchControllerName(BoardConfig::ACTIVE.touch.controller);
  add("Touch", touch ? touch : "No");
  add("Frontlight", Frontlight.present() ? "Yes" : "No");
  add("RTC", halClock.isAvailable() ? "Yes" : "No");
  add("Tilt Sensor (IMU)", halTiltSensor.isAvailable() ? "Yes" : "No");
  const int storageRow = rowCount_;
  add(tr(STR_ABOUT_SD_USED_TOTAL), tr(STR_LOADING));
  requestUpdateAndWait();
  uint64_t total = 0;
  uint64_t free = 0;
  const bool available = Storage.getSpace(total, free);
  {
    RenderLock lock(*this);
    if (available) {
      constexpr uint64_t tenthGb = 100000000ULL;
      const uint64_t usedTenths = (total - free + tenthGb / 2) / tenthGb;
      const uint64_t totalTenths = (total + tenthGb / 2) / tenthGb;
      snprintf(buf, sizeof(buf), "%llu.%llu / %llu.%llu", static_cast<unsigned long long>(usedTenths / 10),
               static_cast<unsigned long long>(usedTenths % 10), static_cast<unsigned long long>(totalTenths / 10),
               static_cast<unsigned long long>(totalTenths % 10));
      rowValues_[storageRow] = buf;
    } else {
      rowValues_[storageRow] = tr(STR_NOT_AVAILABLE);
    }
  }
  requestUpdate();
}

Rect AboutActivity::contentRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  return Rect{safeArea.x, safeArea.y + metrics.topPadding + metrics.headerHeight, safeArea.width,
              safeArea.height - metrics.topPadding - metrics.headerHeight};
}

void AboutActivity::buildScreen(UiScreen& screen) {
  const Rect content = contentRect();
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(content.y), static_cast<int16_t>(renderer.getScreenWidth() - content.x - content.width),
      static_cast<int16_t>(renderer.getScreenHeight() - content.y - content.height), static_cast<int16_t>(content.x)});
  if (!UITheme::getInstance().hasMainTabs())
    screen.spacer(static_cast<int16_t>(UITheme::getInstance().getMetrics().verticalSpacing));

  for (int i = 0; i < rowCount_; i++) {
    rowItems_[i].value = rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(rowCount_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = UITheme::getInstance().hasMainTabs() ? screen.theme().bodyText : screen.theme().smallText;
  props.labelText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}
