#include "PixelSwitchActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "../../../NetworkStartup.h"
#include "../../../WifiCredentialStore.h"
#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../../network/WifiSelectionActivity.h"
#include "../airpage/AirPageDeviceId.h"

namespace {

constexpr uint16_t MQTT_BUFFER_SIZE = 1600;
constexpr char MQTT_HOST[] = "mqtt-cn.uipcat.com";
constexpr uint16_t MQTT_PORT = 1883;
constexpr uint32_t MQTT_RECONNECT_MS = 5000u;
constexpr uint32_t SNAPSHOT_WAIT_MS = 1500u;
constexpr uint32_t PALETTE_LONG_PRESS_MS = 1000u;
constexpr uint32_t COOLDOWN_POPUP_MS = 2000u;
constexpr int PALETTE_SIZE = 4;
constexpr int INTRO_LINE_COUNT = 5;

pixel_switch::PixelSwitchRateLimiter s_rateLimiter;

static_assert(pixel_switch::PixelSwitchState::BYTE_COUNT + sizeof(pixel_switch::MQTT_TOPIC) + 8 <= MQTT_BUFFER_SIZE,
              "Pixel Switch MQTT buffer must hold one complete canvas packet");

Color shadeColor(const pixel_switch::Shade shade) {
  switch (shade) {
    case pixel_switch::Shade::White:
      return Color::White;
    case pixel_switch::Shade::LightGray:
      return Color::LightGray;
    case pixel_switch::Shade::DarkGray:
      return Color::DarkGray;
    case pixel_switch::Shade::Black:
      return Color::Black;
  }
  return Color::White;
}

Rect canvasRect(const GfxRenderer& renderer) {
  int marginTop = 0;
  int marginRight = 0;
  int marginBottom = 0;
  int marginLeft = 0;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  const int viewWidth = std::max(0, renderer.getScreenWidth() - marginLeft - marginRight);
  const int viewHeight = std::max(0, renderer.getScreenHeight() - marginTop - marginBottom);
  const auto size = pixel_switch::fitDisplaySize(viewWidth, viewHeight);
  return Rect{marginLeft + (viewWidth - size.width) / 2, marginTop + (viewHeight - size.height) / 2, size.width,
              size.height};
}

struct PaletteLayout {
  Rect panel;
  Rect cells;
  int cellSize;
  int gap;
};

PaletteLayout paletteLayout(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int cellSize =
      std::max(metrics.menuRowHeight, std::min(renderer.getScreenWidth(), renderer.getScreenHeight()) / 8);
  const int gap = std::max(2, metrics.menuSpacing / 2);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing;
  const int width = metrics.contentSidePadding * 2 + cellSize * 2 + gap;
  const int height = metrics.contentSidePadding * 2 + titleHeight + cellSize * 2 + gap;
  const Rect panel{(renderer.getScreenWidth() - width) / 2, (renderer.getScreenHeight() - height) / 2, width, height};
  return {panel,
          Rect{panel.x + metrics.contentSidePadding, panel.y + metrics.contentSidePadding + titleHeight,
               cellSize * 2 + gap, cellSize * 2 + gap},
          cellSize, gap};
}

}  // namespace

PixelSwitchActivity* PixelSwitchActivity::active_ = nullptr;

void PixelSwitchActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  canvas_.clear();
  pending_.clear();
  cursorX_ = pixel_switch::DISPLAY_WIDTH / 2;
  cursorY_ = pixel_switch::DISPLAY_HEIGHT / 2;
  defaultShade_ = pixel_switch::Shade::Black;
  paletteSelection_ = defaultShade_;
  view_ = View::Intro;
  broughtWifiUp_ = false;
  snapshotReady_ = false;
  ignoreConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  waitForWifiInputRelease_ = false;
  wifiSelectionFailed_ = false;
  wifiRetryActive_ = false;
  wifiRetryPaused_ = false;
  showCooldown_ = false;
  subscribedAtMs_ = 0;
  cooldownStartedMs_ = 0;
  lastPlacementMs_ = 0;
  wifiRetryStartedMs_ = 0;
  active_ = this;

  mqtt_.setServer(MQTT_HOST, MQTT_PORT);
  mqtt_.setCallback(&PixelSwitchActivity::mqttCallback);
  lastConnectAttemptMs_ = millis() - MQTT_RECONNECT_MS;
  LOG_DBG("PXSW", "onEnter activity=%u free=%u largest=%u", static_cast<unsigned>(sizeof(*this)),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  requestUpdate();
}

void PixelSwitchActivity::onExit() {
  active_ = nullptr;
  if (!pending_.empty()) {
    if (canPublish()) flushPending(millis());
    if (!pending_.empty()) {
      LOG_ERR("PXSW", "Discarded %u unpublished pixel(s) on exit", static_cast<unsigned>(pending_.size()));
      pending_.rollback(canvas_);
    }
  }
  if (mqtt_.connected()) mqtt_.disconnect();
  mqtt_.setCallback(nullptr);
  teardownOwnedWifi();
  LOG_DBG("PXSW", "onExit free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  Activity::onExit();
}

// PubSubClient fixes this callback signature to mutable pointers even though the handler only reads them.
// cppcheck-suppress constParameterCallback
void PixelSwitchActivity::mqttCallback(char* topic, uint8_t* payload, const unsigned int length) {
  if (active_) active_->handleMqttMessage(topic, payload, length);
}

void PixelSwitchActivity::handleMqttMessage(const char* topic, const uint8_t* payload, const size_t length) {
  if (!pixel_switch::isValidCanvasMessage(topic, length)) {
    LOG_ERR("PXSW", "Ignored MQTT message topic=%s length=%u", topic ? topic : "(null)", static_cast<unsigned>(length));
    return;
  }

  RenderLock lock(*this);
  if (!pixel_switch::importCanvasMessage(canvas_, topic, payload, length)) return;
  pending_.rebase(canvas_);
  snapshotReady_ = true;
  if (s_rateLimiter.remaining(millis()) > pending_.size()) showCooldown_ = false;
  requestUpdate();
}

bool PixelSwitchActivity::canPublish() { return snapshotReady_ && mqtt_.connected(); }

bool PixelSwitchActivity::prepareMqttBuffer() {
  if (mqttBufferReady_) return true;

  // Whole-canvas Retain needs one bounded activity-lifetime buffer; 1600 bytes
  // cannot live on the task stack and PubSubClient reports allocation failure.
  mqttBufferReady_ = mqtt_.setBufferSize(MQTT_BUFFER_SIZE);
  if (!mqttBufferReady_) {
    LOG_ERR("PXSW", "OOM: MQTT buffer (%u bytes)", static_cast<unsigned>(MQTT_BUFFER_SIZE));
  }
  return mqttBufferReady_;
}

void PixelSwitchActivity::startCanvas() {
  if (WiFi.status() != WL_CONNECTED) {
    launchWifiSelection(true);
    return;
  }

  NetworkStartup::prepare(renderer);
  broughtWifiUp_ = true;
  wifiSelectionFailed_ = false;
  wifiRetryActive_ = false;
  wifiRetryPaused_ = false;
  if (!prepareMqttBuffer()) {
    wifiSelectionFailed_ = true;
    requestUpdate();
    return;
  }
  view_ = View::Canvas;
  requestUpdate();
}

void PixelSwitchActivity::launchWifiSelection(const bool enterCanvasAfterConnect) {
  // ActivityManager owns the picker across frames; stack lifetime is insufficient.
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, true);
  if (!wifi) {
    LOG_ERR("PXSW", "OOM: WifiSelectionActivity (%u bytes)", static_cast<unsigned>(sizeof(WifiSelectionActivity)));
    wifiSelectionFailed_ = true;
    requestUpdate();
    return;
  }

  broughtWifiUp_ = true;
  startActivityForResult(std::move(wifi), [this, enterCanvasAfterConnect](const ActivityResult& result) {
    waitForWifiInputRelease_ = mappedInput.isPressed(MappedInputManager::Button::Back) ||
                               mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                               mappedInput.isPressed(MappedInputManager::Button::NavPrevious) ||
                               mappedInput.isPressed(MappedInputManager::Button::NavNext);
    const bool connected = !result.isCancelled && WiFi.status() == WL_CONNECTED;
    wifiSelectionFailed_ = !connected;
    wifiRetryActive_ = false;
    wifiRetryPaused_ = !connected;
    if (connected && !prepareMqttBuffer()) {
      wifiSelectionFailed_ = true;
      wifiRetryPaused_ = true;
    } else if (connected && enterCanvasAfterConnect) {
      view_ = View::Canvas;
    }
    requestUpdate();
  });
}

bool PixelSwitchActivity::consumeWifiInputReleaseBarrier() {
  if (!waitForWifiInputRelease_) return false;

  const bool anyPressed = mappedInput.isPressed(MappedInputManager::Button::Back) ||
                          mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
                          mappedInput.isPressed(MappedInputManager::Button::NavPrevious) ||
                          mappedInput.isPressed(MappedInputManager::Button::NavNext);
  if (!anyPressed) waitForWifiInputRelease_ = false;
  return true;
}

bool PixelSwitchActivity::connectBroker() {
  char clientId[40];
  {
    // First use may load or create the shared AirPage id on SD, which shares
    // the display SPI bus.
    RenderLock lock(*this);
    snprintf(clientId, sizeof(clientId), "pixel-switch-%s", airpage::deviceId().c_str());
  }
  if (!mqtt_.connect(clientId)) {
    LOG_ERR("PXSW", "MQTT connect failed (state=%d)", mqtt_.state());
    return false;
  }
  if (!mqtt_.subscribe(pixel_switch::MQTT_TOPIC)) {
    LOG_ERR("PXSW", "MQTT subscribe failed");
    mqtt_.disconnect();
    return false;
  }

  subscribedAtMs_ = millis();
  snapshotReady_ = false;
  wifiSelectionFailed_ = false;
  wifiRetryActive_ = false;
  wifiRetryPaused_ = false;
  LOG_INF("PXSW", "MQTT online, subscribed %s", pixel_switch::MQTT_TOPIC);
  return true;
}

bool PixelSwitchActivity::startSavedWifiAssociation() {
  if (WiFi.status() == WL_CONNECTED) return true;

  std::string ssid;
  std::string pass;
  {
    RenderLock lock(*this);
    if (WIFI_STORE.getCredentialCount() == 0) WIFI_STORE.loadFromFile();
    const std::string last = WIFI_STORE.getLastConnectedSsid();
    if (!last.empty()) {
      const auto credential = WIFI_STORE.findCredential(last);
      if (credential) {
        ssid = credential->ssid;
        pass = credential->password;
      }
    }
  }

  if (ssid.empty()) {
    LOG_ERR("PXSW", "No saved WiFi credential");
    return false;
  }

  WiFi.persistent(false);
  NetworkStartup::setMode(renderer, WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  if (pass.empty()) {
    WiFi.begin(ssid.c_str());
  } else {
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
  broughtWifiUp_ = true;
  return true;
}

void PixelSwitchActivity::teardownOwnedWifi() {
  if (!broughtWifiUp_) return;
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  esp_wifi_deinit();
  broughtWifiUp_ = false;
}

void PixelSwitchActivity::pumpNetwork() {
  if (!mqttBufferReady_ || wifiRetryPaused_) return;

  if (mqtt_.connected()) {
    mqtt_.loop();
    if (!snapshotReady_ && static_cast<uint32_t>(millis() - subscribedAtMs_) >= SNAPSHOT_WAIT_MS) {
      snapshotReady_ = true;
    }
    return;
  }

  snapshotReady_ = false;
  const uint32_t now = millis();
  if (!wifiRetryActive_) {
    wifiRetryActive_ = true;
    wifiRetryStartedMs_ = now;
  } else if (pixel_switch::reconnectWindowExpired(wifiRetryStartedMs_, now)) {
    wifiRetryPaused_ = true;
    wifiSelectionFailed_ = true;
    teardownOwnedWifi();
    requestUpdate();
    return;
  }
  if (static_cast<uint32_t>(now - lastConnectAttemptMs_) < MQTT_RECONNECT_MS) return;
  lastConnectAttemptMs_ = now;

  if (WiFi.status() != WL_CONNECTED) {
    startSavedWifiAssociation();
    return;
  }

  connectBroker();
}

void PixelSwitchActivity::loop() {
  if (consumeWifiInputReleaseBarrier()) return;

  pumpNetwork();
  const uint32_t now = millis();
  if (showCooldown_ && pixel_switch::hasElapsed(cooldownStartedMs_, now, COOLDOWN_POPUP_MS)) {
    showCooldown_ = false;
    requestUpdate();
  }

  bool handleInput = true;
  if (ignoreConfirmRelease_) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) ignoreConfirmRelease_ = false;
    handleInput = false;
  }

  if (handleInput) {
    switch (view_) {
      case View::Intro:
        handleIntroInput();
        break;
      case View::Canvas:
        handleCanvasInput();
        break;
      case View::Palette:
        handlePaletteInput();
        break;
    }
  }

  const uint32_t publishNow = millis();
  if (!pending_.empty() && canPublish() &&
      pixel_switch::hasElapsed(lastPlacementMs_, publishNow, pixel_switch::PUBLISH_DEBOUNCE_MS)) {
    RenderLock lock(*this);
    switch (flushPending(publishNow)) {
      case pixel_switch::FlushResult::NothingPending:
        break;
      case pixel_switch::FlushResult::BlankRejected:
        showCooldown_ = false;
        requestUpdate();
        break;
      case pixel_switch::FlushResult::Published:
        if (showCooldown_) {
          showCooldown_ = false;
          requestUpdate();
        }
        break;
      case pixel_switch::FlushResult::PublishFailed:
        lastPlacementMs_ = publishNow;
        break;
    }
  }
}

void PixelSwitchActivity::handleIntroInput() {
  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    startCanvas();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    startCanvas();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
  }
}

void PixelSwitchActivity::moveCursor(const int dx, const int dy) {
  cursorX_ = (cursorX_ + dx + pixel_switch::DISPLAY_WIDTH) % pixel_switch::DISPLAY_WIDTH;
  cursorY_ = (cursorY_ + dy + pixel_switch::DISPLAY_HEIGHT) % pixel_switch::DISPLAY_HEIGHT;
  requestUpdate();
}

void PixelSwitchActivity::handleCanvasInput() {
  const Rect board = canvasRect(renderer);
  const auto selectPixel = [&](const int x, const int y) {
    if (board.width <= 0 || board.height <= 0 || x < board.x || x >= board.x + board.width || y < board.y ||
        y >= board.y + board.height) {
      return false;
    }
    cursorX_ = std::min(pixel_switch::DISPLAY_WIDTH - 1, (x - board.x) * pixel_switch::DISPLAY_WIDTH / board.width);
    cursorY_ = std::min(pixel_switch::DISPLAY_HEIGHT - 1, (y - board.y) * pixel_switch::DISPLAY_HEIGHT / board.height);
    return true;
  };
  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY) && selectPixel(touchX, touchY)) {
    requestUpdate();
    return;
  }
  if (mappedInput.wasScreenTapped(touchX, touchY) && selectPixel(touchX, touchY)) {
    if (mappedInput.getHeldTime() >= PALETTE_LONG_PRESS_MS) {
      paletteSelection_ = defaultShade_;
      view_ = View::Palette;
      requestUpdate();
    } else {
      attemptPlacement(defaultShade_);
    }
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= PALETTE_LONG_PRESS_MS) {
    paletteSelection_ = defaultShade_;
    view_ = View::Palette;
    ignoreConfirmRelease_ = true;
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    moveCursor(0, -1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    moveCursor(0, 1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    moveCursor(-1, 0);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    moveCursor(1, 0);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    attemptPlacement(defaultShade_);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
  }
}

void PixelSwitchActivity::movePalette(const int delta) {
  const int selection = static_cast<int>(paletteSelection_);
  paletteSelection_ = static_cast<pixel_switch::Shade>((selection + delta + PALETTE_SIZE) % PALETTE_SIZE);
  requestUpdate();
}

void PixelSwitchActivity::handlePaletteInput() {
  const PaletteLayout layout = paletteLayout(renderer);
  const auto selectShade = [&](const int x, const int y) {
    const int relativeX = x - layout.cells.x;
    const int relativeY = y - layout.cells.y;
    if (relativeX < 0 || relativeY < 0) return false;
    const int column = relativeX / (layout.cellSize + layout.gap);
    const int row = relativeY / (layout.cellSize + layout.gap);
    if (column >= 2 || row >= 2 || relativeX % (layout.cellSize + layout.gap) >= layout.cellSize ||
        relativeY % (layout.cellSize + layout.gap) >= layout.cellSize) {
      return false;
    }
    paletteSelection_ = static_cast<pixel_switch::Shade>(row * 2 + column);
    return true;
  };
  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchDown(touchX, touchY) && selectShade(touchX, touchY)) {
    requestUpdate();
    return;
  }
  if (mappedInput.wasScreenTapped(touchX, touchY) && selectShade(touchX, touchY)) {
    defaultShade_ = paletteSelection_;
    attemptPlacement(defaultShade_);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    movePalette(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    movePalette(1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    movePalette(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    movePalette(1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    defaultShade_ = paletteSelection_;
    attemptPlacement(defaultShade_);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    view_ = View::Canvas;
    requestUpdate();
  }
}

void PixelSwitchActivity::attemptPlacement(const pixel_switch::Shade shade) {
  if (!canPublish()) {
    if (WiFi.status() != WL_CONNECTED && (wifiRetryPaused_ || wifiSelectionFailed_)) launchWifiSelection(false);
    LOG_ERR("PXSW", "Placement ignored while offline");
    return;
  }

  const uint32_t now = millis();
  RenderLock lock(*this);
  const auto point = pixel_switch::displayToCanvas(cursorX_, cursorY_);
  const auto result = pending_.place(canvas_, s_rateLimiter, point.x, point.y, shade, now);
  switch (result) {
    case pixel_switch::PlacementResult::Changed:
      lastPlacementMs_ = now;
      break;
    case pixel_switch::PlacementResult::Unchanged:
      break;
    case pixel_switch::PlacementResult::RateLimited:
      showCooldown_ = true;
      cooldownStartedMs_ = now;
      requestUpdate();
      return;
  }
  view_ = View::Canvas;
  showCooldown_ = false;
  requestUpdate();
}

pixel_switch::FlushResult PixelSwitchActivity::flushPending(const uint32_t now) {
  const auto result =
      pending_.flush(canvas_, s_rateLimiter, now, [this](const pixel_switch::PixelSwitchState::Bytes& bytes) {
        return mqtt_.publish(pixel_switch::MQTT_TOPIC, bytes.data(), static_cast<unsigned int>(bytes.size()), true);
      });
  switch (result) {
    case pixel_switch::FlushResult::NothingPending:
      return result;
    case pixel_switch::FlushResult::BlankRejected:
      LOG_ERR("PXSW", "Rejected blank canvas publish");
      return result;
    case pixel_switch::FlushResult::Published:
      return result;
    case pixel_switch::FlushResult::PublishFailed:
      LOG_ERR("PXSW", "MQTT batch publish failed");
      return result;
  }
  return result;
}

void PixelSwitchActivity::drawCanvas() const {
  const Rect board = canvasRect(renderer);
  if (board.width == 0 || board.height == 0) return;

  for (int y = 0; y < pixel_switch::DISPLAY_HEIGHT; ++y) {
    const int top = board.y + pixel_switch::scaledDisplayEdge(y, board.height, pixel_switch::DISPLAY_HEIGHT);
    const int bottom = board.y + pixel_switch::scaledDisplayEdge(y + 1, board.height, pixel_switch::DISPLAY_HEIGHT);
    int x = 0;
    while (x < pixel_switch::DISPLAY_WIDTH) {
      const auto point = pixel_switch::displayToCanvas(x, y);
      const auto shade = canvas_.shadeAt(point.x, point.y);
      int runEnd = x + 1;
      while (runEnd < pixel_switch::DISPLAY_WIDTH) {
        const auto runPoint = pixel_switch::displayToCanvas(runEnd, y);
        if (canvas_.shadeAt(runPoint.x, runPoint.y) != shade) break;
        ++runEnd;
      }
      const int left = board.x + pixel_switch::scaledDisplayEdge(x, board.width, pixel_switch::DISPLAY_WIDTH);
      const int right = board.x + pixel_switch::scaledDisplayEdge(runEnd, board.width, pixel_switch::DISPLAY_WIDTH);
      renderer.fillRectDither(left, top, right - left, bottom - top, shadeColor(shade));
      x = runEnd;
    }
  }

  const int cursorLeft = board.x + pixel_switch::scaledDisplayEdge(cursorX_, board.width, pixel_switch::DISPLAY_WIDTH);
  const int cursorRight =
      board.x + pixel_switch::scaledDisplayEdge(cursorX_ + 1, board.width, pixel_switch::DISPLAY_WIDTH);
  const int cursorTop = board.y + pixel_switch::scaledDisplayEdge(cursorY_, board.height, pixel_switch::DISPLAY_HEIGHT);
  const int cursorBottom =
      board.y + pixel_switch::scaledDisplayEdge(cursorY_ + 1, board.height, pixel_switch::DISPLAY_HEIGHT);
  renderer.drawRect(cursorLeft - 2, cursorTop - 2, cursorRight - cursorLeft + 4, cursorBottom - cursorTop + 4, 2, true);
  renderer.drawRect(cursorLeft - 1, cursorTop - 1, cursorRight - cursorLeft + 2, cursorBottom - cursorTop + 2, false);
}

void PixelSwitchActivity::drawPalette() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const PaletteLayout layout = paletteLayout(renderer);

  renderer.fillRoundedRect(layout.panel.x, layout.panel.y, layout.panel.width, layout.panel.height,
                           metrics.popupCornerRadius, Color::White);
  renderer.drawRoundedRect(layout.panel.x, layout.panel.y, layout.panel.width, layout.panel.height, 2,
                           metrics.popupCornerRadius, true);
  renderer.drawCenteredText(UI_12_FONT_ID, layout.panel.y + metrics.contentSidePadding / 2,
                            tr(STR_PIXEL_SWITCH_CHOOSE_SHADE));

  for (uint8_t i = 0; i < 4; ++i) {
    const auto shade = static_cast<pixel_switch::Shade>(i);
    const int row = i / 2;
    const int column = i % 2;
    const int x = layout.cells.x + column * (layout.cellSize + layout.gap);
    const int y = layout.cells.y + row * (layout.cellSize + layout.gap);
    renderer.fillRectDither(x, y, layout.cellSize, layout.cellSize, shadeColor(shade));
    renderer.drawRect(x, y, layout.cellSize, layout.cellSize, true);
    if (shade == paletteSelection_) {
      renderer.drawRect(x - 3, y - 3, layout.cellSize + 6, layout.cellSize + 6, 2, true);
      renderer.drawRect(x - 1, y - 1, layout.cellSize + 2, layout.cellSize + 2, false);
    }
  }
}

void PixelSwitchActivity::drawIntro() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  const int hintsReservedHeight =
      metrics.buttonHintsHeight > 0 ? metrics.buttonHintsHeight + metrics.verticalSpacing : 0;
  const Rect view{marginLeft, marginTop, renderer.getScreenWidth() - marginLeft - marginRight,
                  renderer.getScreenHeight() - marginTop - marginBottom - hintsReservedHeight};
  const char* lines[INTRO_LINE_COUNT] = {
      tr(STR_PIXEL_SWITCH_INTRO_SHARED),
      I18n::getInstance().get(mappedInput.hasTouch() ? StrId::STR_PIXEL_SWITCH_TOUCH_MOVE
                                                     : StrId::STR_PIXEL_SWITCH_INTRO_MOVE),
      I18n::getInstance().get(mappedInput.hasTouch() ? StrId::STR_PIXEL_SWITCH_TOUCH_PLACE
                                                     : StrId::STR_PIXEL_SWITCH_INTRO_PLACE),
      I18n::getInstance().get(mappedInput.hasTouch() ? StrId::STR_PIXEL_SWITCH_TOUCH_SHADE
                                                     : StrId::STR_PIXEL_SWITCH_INTRO_SHADE),
      I18n::getInstance().get(mappedInput.hasTouch() ? StrId::STR_PIXEL_SWITCH_TOUCH_START
                                                     : StrId::STR_PIXEL_SWITCH_INTRO_START)};
  const int padding = metrics.contentSidePadding;
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int bodyLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int startLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  int textWidth = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_PIXEL_SWITCH_TITLE), EpdFontFamily::BOLD);
  for (int i = 0; i < INTRO_LINE_COUNT - 1; ++i) {
    textWidth = std::max(textWidth, renderer.getTextWidth(UI_10_FONT_ID, lines[i]));
  }
  textWidth =
      std::max(textWidth, renderer.getTextWidth(UI_12_FONT_ID, lines[INTRO_LINE_COUNT - 1], EpdFontFamily::BOLD));

  const int boxWidth = std::min(view.width, textWidth + padding * 2);
  const int boxHeight = padding * 2 + titleHeight + metrics.verticalSpacing * 2 +
                        bodyLineHeight * (INTRO_LINE_COUNT - 1) + startLineHeight;
  const int boxX = view.x + (view.width - boxWidth) / 2;
  const int boxY = view.y + (view.height - boxHeight) / 2;
  renderer.fillRoundedRect(boxX, boxY, boxWidth, boxHeight, metrics.popupCornerRadius, Color::White);
  renderer.drawRoundedRect(boxX, boxY, boxWidth, boxHeight, std::max(1, metrics.popupFrameThickness),
                           metrics.popupCornerRadius, true);

  int y = boxY + padding;
  UITheme::drawCenteredText(renderer, Rect{boxX, y, boxWidth, titleHeight}, UI_12_FONT_ID, y,
                            tr(STR_PIXEL_SWITCH_TITLE), true, EpdFontFamily::BOLD);
  y += titleHeight + metrics.verticalSpacing;
  for (int i = 0; i < INTRO_LINE_COUNT - 1; ++i) {
    UITheme::drawCenteredText(renderer, Rect{boxX, y, boxWidth, bodyLineHeight}, UI_10_FONT_ID, y, lines[i]);
    y += bodyLineHeight;
  }
  y += metrics.verticalSpacing;
  UITheme::drawCenteredText(renderer, Rect{boxX, y, boxWidth, startLineHeight}, UI_12_FONT_ID, y,
                            lines[INTRO_LINE_COUNT - 1], true, EpdFontFamily::BOLD);
}

void PixelSwitchActivity::drawCooldown() const {
  char message[48];
  const uint32_t seconds = (s_rateLimiter.retryAfterMs(millis(), pending_.size()) + 999u) / 1000u;
  snprintf(message, sizeof(message), tr(STR_PIXEL_SWITCH_RATE_LIMIT_FMT), static_cast<unsigned long>(seconds));
  GUI.drawPopup(renderer, message);
}

void PixelSwitchActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawCanvas();
  switch (view_) {
    case View::Intro:
      drawIntro();
      {
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
      if (wifiSelectionFailed_) GUI.drawPopup(renderer, tr(STR_WIFI_CONN_FAILED));
      break;
    case View::Canvas:
      if (wifiRetryPaused_ || wifiSelectionFailed_) {
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        GUI.drawPopup(renderer, tr(STR_WIFI_CONN_FAILED));
      }
      break;
    case View::Palette:
      drawPalette();
      break;
  }
  if (showCooldown_) {
    drawCooldown();
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
