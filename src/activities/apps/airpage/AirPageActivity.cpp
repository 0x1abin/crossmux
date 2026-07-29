#include "AirPageActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>
#include <string>

#include "AirPageDeviceId.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/QrUtils.h"

namespace {

constexpr char kAirPageBase[] =
#ifdef ENABLE_CHINESE_VERSION
    "airpage.yunhug.com";
#else
    "airpage.crossmux.com";
#endif

// Topic convention is shared with the cloud companion and must stay in sync:
// airpage/device/<deviceId>/refresh.
constexpr char kMqttHost[] = "mqtt-cn.uipcat.com";
constexpr uint16_t kMqttPort = 1883;
constexpr uint32_t kReconnectMs = 5000u;

constexpr char kCacheDir[] = "/.crosspoint/airpage";
constexpr char kImagePath[] = "/.crosspoint/airpage/latest.bmp";
constexpr uint32_t kWifiConnectTimeoutMs = 15000u;

constexpr uint8_t kMenuRows = 2;
constexpr StrId kMenuRowIds[kMenuRows] = {StrId::STR_AIRPAGE_MODE_MANUAL, StrId::STR_AIRPAGE_MODE_REALTIME};

// PubSubClient invokes this from mqtt_.loop() on the main task, so one flag is
// sufficient for the single foreground AirPageActivity instance.
volatile bool s_refreshFromPush = false;

void onMqttMessage(char* /*topic*/, uint8_t* /*payload*/, unsigned int /*len*/) { s_refreshFromPush = true; }

std::string refreshTopic() { return std::string("airpage/device/") + airpage::deviceId() + "/refresh"; }

}  // namespace

void AirPageActivity::onEnter() {
  Activity::onEnter();
  view_ = View::Qr;
  phase_ = Phase::Idle;
  pendingError_ = false;
  haveCachedImage_ = Storage.exists(kImagePath);
  menuOpen_ = false;
  menuSel_ = 0;
  mqttState_ = MqttState::Off;
  s_refreshFromPush = false;
  mqtt_.setServer(kMqttHost, kMqttPort);
  mqtt_.setCallback(&onMqttMessage);
  realtimeMode_ = airpage::loadRealtimeMode();
  LOG_DBG("AIRP", "onEnter free=%u largest=%u id=%s cached=%d realtime=%d",
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          airpage::deviceId().c_str(), haveCachedImage_ ? 1 : 0, realtimeMode_ ? 1 : 0);
  if (realtimeMode_) enterLiveMode();
  requestUpdate();
}

void AirPageActivity::onExit() {
  if (mqtt_.connected()) mqtt_.disconnect();
  mqttState_ = MqttState::Off;
  teardownWifi();
  LOG_DBG("AIRP", "onExit free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  Activity::onExit();
}

void AirPageActivity::loop() {
  if (menuOpen_) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      menuOpen_ = false;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      menuSel_ = static_cast<uint8_t>((menuSel_ + kMenuRows - 1) % kMenuRows);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      menuSel_ = static_cast<uint8_t>((menuSel_ + 1) % kMenuRows);
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      applyMenuSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    toggleView();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    requestFetch();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openModeMenu();
    return;
  }

  if (phase_ == Phase::Requested) {
    // The requested render paints the status before this blocking fetch.
    phase_ = Phase::Fetching;
    doFetch();
    phase_ = Phase::Idle;
    requestUpdate();
    return;
  }

  if (realtimeMode_) pumpMqtt();
}

void AirPageActivity::toggleView() {
  if (phase_ != Phase::Idle) return;
  switch (view_) {
    case View::Qr:
      if (!haveCachedImage_) return;
      view_ = View::Image;
      break;
    case View::Image:
      view_ = View::Qr;
      break;
  }
  pendingError_ = false;
  requestUpdate();
}

void AirPageActivity::openModeMenu() {
  if (phase_ != Phase::Idle) return;
  menuOpen_ = true;
  menuSel_ = realtimeMode_ ? 1 : 0;
  requestUpdate();
}

void AirPageActivity::applyMenuSelection() {
  const bool wantRealtime = (menuSel_ == 1);
  menuOpen_ = false;
  if (wantRealtime != realtimeMode_) {
    airpage::saveRealtimeMode(wantRealtime);
    if (wantRealtime) {
      enterLiveMode();
    } else {
      exitLiveMode();
    }
  }
  requestUpdate();
}

void AirPageActivity::requestFetch() {
  if (phase_ != Phase::Idle || menuOpen_) return;
  phase_ = Phase::Requested;
  requestUpdate();
}

void AirPageActivity::enterLiveMode() {
  realtimeMode_ = true;
  mqttState_ = MqttState::Connecting;
  lastConnectAttemptMs_ = millis() - kReconnectMs;
  view_ = haveCachedImage_ ? View::Image : View::Qr;
  requestUpdate();
}

void AirPageActivity::exitLiveMode() {
  if (mqtt_.connected()) mqtt_.disconnect();
  mqttState_ = MqttState::Off;
  realtimeMode_ = false;
  teardownWifi();
  requestUpdate();
}

bool AirPageActivity::connectBroker() {
  const std::string clientId = std::string("x4-") + airpage::deviceId();
  if (!mqtt_.connect(clientId.c_str())) {
    LOG_ERR("AIRP", "MQTT connect failed (state=%d)", mqtt_.state());
    return false;
  }
  const std::string topic = refreshTopic();
  if (!mqtt_.subscribe(topic.c_str())) {
    LOG_ERR("AIRP", "MQTT subscribe failed: %s", topic.c_str());
    mqtt_.disconnect();
    return false;
  }
  LOG_INF("AIRP", "MQTT online, subscribed %s", topic.c_str());
  return true;
}

void AirPageActivity::pumpMqtt() {
  if (!mqtt_.connected()) {
    mqttState_ = MqttState::Connecting;
    const uint32_t now = millis();
    if (now - lastConnectAttemptMs_ < kReconnectMs) return;
    lastConnectAttemptMs_ = now;
    if (WiFi.status() != WL_CONNECTED) {
      startWifiAssociation();
      return;
    }
    if (!connectBroker()) return;
    mqttState_ = MqttState::Online;
    requestUpdate();
  }

  mqtt_.loop();
  if (s_refreshFromPush) {
    s_refreshFromPush = false;
    LOG_INF("AIRP", "push received -> fetch");
    requestFetch();
  }
}

bool AirPageActivity::startWifiAssociation() {
  std::string ssid;
  std::string pass;
  if (WIFI_STORE.getCredentials().empty()) WIFI_STORE.loadFromFile();
  const std::string& last = WIFI_STORE.getLastConnectedSsid();
  if (!last.empty()) {
    const WifiCredential* cred = WIFI_STORE.findCredential(last);
    if (cred) {
      ssid = cred->ssid;
      pass = cred->password;
    }
  }
  if (ssid.empty()) {
    LOG_ERR("AIRP", "No saved WiFi credential");
    return false;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  if (pass.empty()) {
    WiFi.begin(ssid.c_str());
  } else {
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
  weBroughtWifiUp_ = true;
  return true;
}

bool AirPageActivity::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!startWifiAssociation()) return false;

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < kWifiConnectTimeoutMs) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("AIRP", "WiFi connect failed");
    return false;
  }
  return true;
}

void AirPageActivity::teardownWifi() {
  // Do not tear down a connection established by another activity.
  if (!weBroughtWifiUp_) return;
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  esp_wifi_deinit();
  weBroughtWifiUp_ = false;
}

void AirPageActivity::doFetch() {
  pendingError_ = false;
  if (!ensureWifi()) {
    pendingError_ = true;
    view_ = View::Qr;
    return;
  }

  Storage.ensureDirectoryExists(kCacheDir);
  const std::string url =
      std::string("https://") + kAirPageBase + "/api/device/" + airpage::deviceId() + "/latest.bmp";
  const HttpDownloader::DownloadError err = HttpDownloader::downloadToFile(url, kImagePath);
  if (err != HttpDownloader::OK) {
    LOG_ERR("AIRP", "Download failed: %d", static_cast<int>(err));
    pendingError_ = true;
    view_ = View::Qr;
    return;
  }
  LOG_INF("AIRP", "Fetched latest image");
  haveCachedImage_ = true;
  view_ = View::Image;
}

void AirPageActivity::render(RenderLock&&) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const Rect fullScreen{0, 0, screenWidth, screenHeight};

  renderer.clearScreen();
  if (!menuOpen_ && phase_ == Phase::Idle && view_ == View::Image && renderImage(fullScreen)) {
    renderer.displayBuffer();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, true);
  const char* title = menuOpen_ ? tr(STR_AIRPAGE_MENU_TITLE) : tr(STR_AIRPAGE_TITLE);
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 title);

  const int contentTop = safeArea.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      safeArea.height - metrics.topPadding - metrics.headerHeight - metrics.verticalSpacing * 2;
  const Rect content{safeArea.x, contentTop, safeArea.width, contentHeight};

  if (menuOpen_) {
    renderMenu(content);
  } else if (phase_ != Phase::Idle) {
    renderStatus(content, tr(STR_AIRPAGE_LOADING));
  } else if (view_ == View::Image) {
    renderStatus(content, tr(STR_AIRPAGE_NO_IMAGE));
  } else {
    renderQr(content);
  }

  if (menuOpen_) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const char* previous = "";
    if (view_ == View::Image) {
      previous = tr(STR_AIRPAGE_SHOW_QR);
    } else if (haveCachedImage_) {
      previous = tr(STR_AIRPAGE_SHOW_IMAGE);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_AIRPAGE_MODE_ACTION), previous,
                                              tr(STR_AIRPAGE_REFRESH));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}

void AirPageActivity::renderQr(const Rect& viewport) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int reservedTextHeight = lineHeight * 2 + metrics.verticalSpacing * 2;
  const int qrSize = std::max(1, std::min(viewport.width - metrics.contentSidePadding * 2,
                                         viewport.height - reservedTextHeight));
  const Rect qrBounds{viewport.x + (viewport.width - qrSize) / 2, viewport.y, qrSize, qrSize};
  const std::string url = std::string("https://") + kAirPageBase + "/?id=" + airpage::deviceId() + "&type=x4";
  QrUtils::drawQrCode(renderer, qrBounds, url);

  const int hintY = qrBounds.y + qrBounds.height + metrics.verticalSpacing;
  const char* hint = pendingError_ ? tr(STR_AIRPAGE_FETCH_FAILED) : tr(STR_AIRPAGE_QR_HINT);
  GUI.drawHelpText(renderer, Rect{viewport.x, hintY, viewport.width, lineHeight}, hint);

  if (realtimeMode_) {
    const char* state =
        mqttState_ == MqttState::Online ? tr(STR_AIRPAGE_REALTIME_LIVE) : tr(STR_AIRPAGE_REALTIME_CONNECTING);
    GUI.drawHelpText(renderer,
                     Rect{viewport.x, hintY + lineHeight + metrics.verticalSpacing, viewport.width, lineHeight}, state);
  }
}

void AirPageActivity::renderStatus(const Rect& viewport, const char* msg) {
  UITheme::drawCenteredWrappedText(renderer, viewport, UI_12_FONT_ID, msg, 2);
}

void AirPageActivity::renderMenu(const Rect& viewport) {
  GUI.drawList(
      renderer, viewport, kMenuRows, menuSel_,
      [](int index) { return std::string(I18N.get(kMenuRowIds[index])); }, nullptr, nullptr,
      [this](int index) {
        const bool active = (index == 1) == realtimeMode_;
        return active ? std::string(tr(STR_SELECTED)) : std::string();
      });
}

bool AirPageActivity::renderImage(const Rect& viewport) {
  HalFile file;
  if (!Storage.openFileForRead("AIRP", kImagePath, file)) return false;
  Bitmap bitmap(file, /*dithering=*/false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;

  int x = viewport.x + (viewport.width - bitmap.getWidth()) / 2;
  int y = viewport.y + (viewport.height - bitmap.getHeight()) / 2;
  if (x < viewport.x) x = viewport.x;
  if (y < viewport.y) y = viewport.y;
  renderer.drawBitmap(bitmap, x, y, viewport.width, viewport.height, 0, 0);
  return true;
}
