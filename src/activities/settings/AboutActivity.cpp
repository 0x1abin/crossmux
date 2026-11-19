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
// Use quotient/remainder so formatting also remains defined at UINT64_MAX.
std::string formatBytes(const uint64_t bytes) {
  constexpr uint64_t MIB = 1024ull * 1024ull;
  constexpr uint64_t GIB = 1024ull * MIB;
  char buf[24];
  if (bytes >= GIB) {
    const uint64_t tenths = (bytes / GIB) * 10 + ((bytes % GIB) * 10 + GIB / 2) / GIB;
    snprintf(buf, sizeof(buf), "%llu.%llu GB", static_cast<unsigned long long>(tenths / 10),
             static_cast<unsigned long long>(tenths % 10));
  } else {
    snprintf(buf, sizeof(buf), "%llu MB", static_cast<unsigned long long>((bytes + MIB / 2) / MIB));
  }
  return buf;
}

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
#ifdef SIMULATOR
    case BoardConfig::DisplayController::Epdiy:
      return "EPDiy";
#endif
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
    case BoardConfig::TouchController::Cst816s:
      return "CST816S";
    case BoardConfig::TouchController::Cst836u:
      return "CST836U";
    case BoardConfig::TouchController::Tma525c:
      return "TMA525C";
    case BoardConfig::TouchController::Gslx680:
      return "GSLX680";
  }
  return "?";
}
}  // namespace

AboutActivity::AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("About", renderer, mappedInput) {}

const char* AboutActivity::headerTitle() const { return tr(STR_ABOUT); }

void AboutActivity::onEnter() {
  UiListActivity::onEnter();
  const bool inx = UITheme::getInstance().hasMainTabs();
  // Bounded activity-owned slots; only entry updates strings, never drawing.
  rowCount_ = 0;
  const auto add = [this](const char* label, const char* value) {
    assert(rowCount_ < ITEM_COUNT);
    rowItems_[rowCount_].label = label;
    rowItems_[rowCount_].actionValue = static_cast<int16_t>(rowCount_);
    rowValues_[rowCount_++] = value;
  };
  char buf[64];
  const auto heap = HalSystem::getHeapInfo();
  const auto addChip = [&] {
#ifdef SIMULATOR
    add(inx ? tr(STR_ABOUT_CHIP_MODEL) : tr(STR_ABOUT_CHIP), "Simulator");
#else
    add(inx ? tr(STR_ABOUT_CHIP_MODEL) : tr(STR_ABOUT_CHIP), HalSystem::getChipModel());
#endif
  };
  const auto addTemperature = [&] {
#ifndef SIMULATOR
    float temperature = 0;
    if (HalSystem::getChipTemperatureCelsius(temperature)) {
      snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(temperature)));
      add(tr(STR_ABOUT_CHIP_TEMPERATURE), buf);
      return;
    }
#endif
    add(tr(STR_ABOUT_CHIP_TEMPERATURE), tr(STR_NOT_AVAILABLE));
  };
  const auto addMac = [&] {
    HalSystem::DeviceId mac{};
#ifdef SIMULATOR
    const bool available = HalSystem::getDeviceId(mac);
#else
    const bool available = HalSystem::getWifiStationMac(mac);
#endif
    if (available) {
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      add(inx ? tr(STR_ABOUT_WIFI_MAC_ADDRESS) : tr(STR_ABOUT_MAC), buf);
    } else {
      add(inx ? tr(STR_ABOUT_WIFI_MAC_ADDRESS) : tr(STR_ABOUT_MAC), tr(STR_NOT_AVAILABLE));
    }
  };
  const auto addRuntime = [&] {
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
  };
  const auto addFlash = [&] {
    snprintf(buf, sizeof(buf), "%u MB", static_cast<unsigned>(ESP.getFlashChipSize() / (1024u * 1024u)));
    add("Flash", buf);
  };
  const auto addHardware = [&] {
    const char* liveController = display.controllerName();
    add("Display Controller",
        liveController ? liveController : displayControllerName(BoardConfig::ACTIVE.displayController));
    snprintf(buf, sizeof(buf), "%ux%u", static_cast<unsigned>(renderer.getDisplayWidth()),
             static_cast<unsigned>(renderer.getDisplayHeight()));
    add("Resolution", buf);
    const char* touch = touchControllerName(BoardConfig::ACTIVE.touch.controller);
    add("Touch", touch ? touch : "No");
    add("Frontlight", Frontlight.present() ? "Yes" : "No");
    add("RTC", halClock.isAvailable() ? "Yes" : "No");
    add("Tilt Sensor (IMU)", halTiltSensor.isAvailable() ? "Yes" : "No");
  };
  int storageRow;
  if (inx) {
    add(tr(STR_ABOUT_FIRMWARE_NAME), tr(STR_CROSSPOINT));
    add(tr(STR_ABOUT_FIRMWARE_VERSION), CROSSPOINT_VERSION);
    add(tr(STR_ABOUT_DEVICE_MODEL), BoardConfig::ACTIVE.name);
    addChip();
    addTemperature();
    addMac();
    addRuntime();
    addFlash();
    addHardware();
    storageRow = rowCount_;
    add(tr(STR_ABOUT_SD_USED_TOTAL), tr(STR_LOADING));
  } else {
    add(tr(STR_ABOUT_DEVICE), BoardConfig::ACTIVE.name);
    add(tr(STR_ABOUT_FIRMWARE), CROSSPOINT_VERSION);
    addChip();
    addFlash();
    storageRow = rowCount_;
    add(tr(STR_ABOUT_SD_SIZE), tr(STR_LOADING));
    add(tr(STR_ABOUT_SD_USED), tr(STR_LOADING));
    add(tr(STR_ABOUT_SD_FREE), tr(STR_LOADING));
    addHardware();
    addMac();
    add(tr(STR_ABOUT_FIRMWARE_NAME), tr(STR_CROSSPOINT));
    addTemperature();
    addRuntime();
  }
  requestUpdateAndWait();
  uint64_t total = 0, free = 0;
  const bool available = Storage.getSpace(total, free) && total > 0 && free <= total;
  {
    RenderLock lock(*this);
    if (!available) {
      for (int i = 0; i < (inx ? 1 : 3); ++i) rowValues_[storageRow + i] = tr(STR_NOT_AVAILABLE);
    } else if (inx) {
      constexpr uint64_t tenthGb = 100000000ULL;
      const auto tenths = [](const uint64_t value) {
        return value / tenthGb + (value % tenthGb + tenthGb / 2) / tenthGb;
      };
      const uint64_t usedTenths = tenths(total - free), totalTenths = tenths(total);
      snprintf(buf, sizeof(buf), "%llu.%llu / %llu.%llu", static_cast<unsigned long long>(usedTenths / 10),
               static_cast<unsigned long long>(usedTenths % 10), static_cast<unsigned long long>(totalTenths / 10),
               static_cast<unsigned long long>(totalTenths % 10));
      rowValues_[storageRow] = buf;
    } else {
      rowValues_[storageRow] = formatBytes(total);
      rowValues_[storageRow + 1] = formatBytes(total - free);
      rowValues_[storageRow + 2] = formatBytes(free);
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
