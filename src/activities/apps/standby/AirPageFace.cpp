#include "AirPageFace.h"

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
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "util/QrUtils.h"

namespace {

// Cloud base host (the deployed companion Cloudflare app, custom domain bound).
// To repoint, change this single constant and rebuild. No scheme here; we
// prepend https:// below.
constexpr char kAirPageBase[] = "pages.yunhug.com";

// MQTT broker for live push (plain TCP, anonymous). The cloud /?id=<id>&type=x4
// page publishes a refresh message here (over wss:8084) when "Send" is clicked; the
// device subscribes (over tcp:1883) and fetches the moment one arrives. Topic
// convention is shared with the cloud repo and MUST stay in sync:
//   airpage/device/<deviceId>/refresh
constexpr char kMqttHost[] = "mqtt-cn.uipcat.com";
constexpr uint16_t kMqttPort = 1883;
constexpr uint32_t kReconnectMs = 5000u;  // throttle WiFi/MQTT reconnect attempts

// SD persistence (under the existing .crosspoint cache root). Survives reboot.
constexpr char kCacheDir[] = "/.crosspoint/airpage";
constexpr char kImagePath[] = "/.crosspoint/airpage/latest.bmp";

constexpr uint32_t kWifiConnectTimeoutMs = 15000u;  // matches StandbyActivity

constexpr uint8_t kMenuRows = 2;  // 0 = Manual, 1 = Live push

// Set by the MQTT callback (plain task context — PubSubClient delivers from
// mqtt_.loop() on the main task), drained in tick(). We only subscribe to this
// device's own refresh topic, so any message means "refetch". A plain volatile
// flag suffices: single writer, single reader, one AirPageFace alive at a time.
volatile bool s_refreshFromPush = false;

void onMqttMessage(char* /*topic*/, uint8_t* /*payload*/, unsigned int /*len*/) { s_refreshFromPush = true; }

std::string refreshTopic() { return std::string("airpage/device/") + airpage::deviceId() + "/refresh"; }

}  // namespace

void AirPageFace::onEnter() {
  view_ = View::Qr;
  phase_ = Phase::Idle;
  pendingError_ = false;
  haveCachedImage_ = Storage.exists(kImagePath);
  needsRedraw_ = true;
  menuOpen_ = false;
  menuSel_ = 0;
  mqttState_ = MqttState::Off;
  s_refreshFromPush = false;
  mqtt_.setServer(kMqttHost, kMqttPort);
  mqtt_.setCallback(&onMqttMessage);
  realtimeMode_ = airpage::loadRealtimeMode();
  LOG_DBG("AIRP", "onEnter id=%s cached=%d realtime=%d", airpage::deviceId().c_str(), haveCachedImage_ ? 1 : 0,
          realtimeMode_ ? 1 : 0);
  if (realtimeMode_) enterLiveMode();  // restore live push: arm reconnect + initial fetch
}

void AirPageFace::onExit() {
  if (mqtt_.connected()) mqtt_.disconnect();
  mqttState_ = MqttState::Off;
  teardownWifi();
}

void AirPageFace::teardownWifi() {
  // Only release WiFi we ourselves brought up — never stomp a connection NTP
  // sync or another path established. Mirror StandbyActivity::finishTimeSync so
  // HalPowerManager can drop the CPU to low frequency afterwards.
  if (weBroughtWifiUp_) {
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_OFF);
    esp_wifi_deinit();
    weBroughtWifiUp_ = false;
  }
}

StrId AirPageFace::titleId() const { return StrId::STR_FACE_AIRPAGE; }

void AirPageFace::onPagePrev() {
  // Menu open: move the cursor up. Menu closed: toggle QR <-> image (the role
  // Confirm used to play before Confirm became the menu opener).
  if (menuOpen_) {
    menuSel_ = static_cast<uint8_t>((menuSel_ + kMenuRows - 1) % kMenuRows);
    needsRedraw_ = true;
    return;
  }
  if (phase_ != Phase::Idle) return;  // ignore mid-fetch
  pendingError_ = false;
  if (view_ == View::Qr) {
    if (haveCachedImage_) view_ = View::Image;  // nothing to show yet -> stay on QR
  } else {
    view_ = View::Qr;
  }
  needsRedraw_ = true;
}

void AirPageFace::onPageNext() {
  if (menuOpen_) {
    menuSel_ = static_cast<uint8_t>((menuSel_ + 1) % kMenuRows);
    needsRedraw_ = true;
    return;
  }
  requestFetch();  // DOWN: manual fetch (also an override while in live mode)
}

bool AirPageFace::handleConfirm() {
  if (menuOpen_) {
    applyMenuSelection();
    return true;
  }
  if (phase_ != Phase::Idle) return true;  // consume, ignore mid-fetch
  // Confirm opens the mode menu (formerly a long-press; the QR<->image toggle
  // it used to do moved to UP/onPagePrev).
  menuOpen_ = true;
  menuSel_ = realtimeMode_ ? 1 : 0;  // start the cursor on the active mode
  needsRedraw_ = true;
  return true;
}

bool AirPageFace::handleBack() {
  if (!menuOpen_) return false;
  menuOpen_ = false;
  needsRedraw_ = true;
  return true;
}

void AirPageFace::applyMenuSelection() {
  const bool wantRealtime = (menuSel_ == 1);
  menuOpen_ = false;
  needsRedraw_ = true;
  if (wantRealtime == realtimeMode_) return;  // no change
  airpage::saveRealtimeMode(wantRealtime);
  if (wantRealtime) {
    enterLiveMode();
  } else {
    exitLiveMode();
  }
}

void AirPageFace::requestFetch() {
  // Single entry point for "fetch the latest image" (DOWN, an MQTT push, and
  // live-mode entry all call here). Phase::Requested lets render() paint the
  // "Fetching…" status this frame; tick() runs the blocking download next frame.
  if (phase_ != Phase::Idle || menuOpen_) return;
  phase_ = Phase::Requested;
  needsRedraw_ = true;
}

void AirPageFace::enterLiveMode() {
  realtimeMode_ = true;
  mqttState_ = MqttState::Connecting;
  lastConnectAttemptMs_ = millis() - kReconnectMs;  // first reconnect attempt is due immediately
  requestFetch();                                   // show the current image while the broker connects
  needsRedraw_ = true;
}

void AirPageFace::exitLiveMode() {
  if (mqtt_.connected()) mqtt_.disconnect();
  mqttState_ = MqttState::Off;
  realtimeMode_ = false;
  teardownWifi();  // drop WiFi we raised so the CPU can downclock again
  needsRedraw_ = true;
}

bool AirPageFace::connectBroker() {
  const std::string clientId = std::string("x4-") + airpage::deviceId();
  if (!mqtt_.connect(clientId.c_str())) {
    LOG_ERR("AIRP", "MQTT connect failed (state=%d)", mqtt_.state());
    return false;
  }
  const std::string topic = refreshTopic();
  mqtt_.subscribe(topic.c_str());
  LOG_INF("AIRP", "MQTT online, subscribed %s", topic.c_str());
  return true;
}

void AirPageFace::pumpMqtt() {
  // Bring WiFi + broker up as one throttled unit so a quick WiFi reconnect isn't
  // followed by a needless wait before the broker connect.
  if (!mqtt_.connected()) {
    mqttState_ = MqttState::Connecting;
    const uint32_t now = millis();
    if (now - lastConnectAttemptMs_ < kReconnectMs) return;
    lastConnectAttemptMs_ = now;
    if (!ensureWifi() || !connectBroker()) return;  // retry on the next window
    mqttState_ = MqttState::Online;
    needsRedraw_ = true;  // indicator: connecting -> live
  }

  mqtt_.loop();

  if (s_refreshFromPush) {
    s_refreshFromPush = false;
    LOG_INF("AIRP", "push received -> fetch");
    requestFetch();
  }
}

bool AirPageFace::tick() {
  if (phase_ == Phase::Requested) {
    // The "Fetching…" status was painted on the prior frame (requestFetch set
    // Requested and StandbyActivity::loop requested a redraw). Now run the
    // blocking fetch with the status already on the e-ink panel.
    phase_ = Phase::Fetching;
    doFetch();
    phase_ = Phase::Idle;
    return true;  // redraw: image on success, or QR with error hint
  }

  if (realtimeMode_ && !menuOpen_) pumpMqtt();  // maintain broker; may queue a fetch / flip mqttState_

  if (needsRedraw_) {
    needsRedraw_ = false;
    return true;
  }
  return false;
}

bool AirPageFace::ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;  // reuse e.g. NTP's connection

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
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < kWifiConnectTimeoutMs) {
    delay(200);  // delay() yields to the scheduler, feeding the watchdog
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("AIRP", "WiFi connect failed");
    return false;
  }
  weBroughtWifiUp_ = true;
  return true;
}

void AirPageFace::doFetch() {
  pendingError_ = false;

  if (!ensureWifi()) {
    pendingError_ = true;
    return;
  }

  // Download to SD on the main thread — the e-ink panel is idle during this
  // blocking call, so the shared SPI bus has no SD/render contention (same
  // pattern as FontDownloadActivity).
  Storage.ensureDirectoryExists(kCacheDir);
  std::string url = std::string("https://") + kAirPageBase + "/api/device/" + airpage::deviceId() + "/latest.bmp";
  const HttpDownloader::DownloadError err = HttpDownloader::downloadToFile(url, kImagePath);
  if (err != HttpDownloader::OK) {
    LOG_ERR("AIRP", "Download failed: %d", static_cast<int>(err));
    pendingError_ = true;
    return;
  }
  LOG_INF("AIRP", "Fetched latest image");
  haveCachedImage_ = true;
  view_ = View::Image;
}

void AirPageFace::render(GfxRenderer& renderer, const Rect& viewport) {
  if (menuOpen_) {
    renderMenu(renderer, viewport);  // upstream already cleared the screen
    return;
  }
  if (phase_ != Phase::Idle) {
    renderStatus(renderer, viewport, tr(STR_AIRPAGE_LOADING));
    return;
  }
  if (view_ == View::Image) {
    if (!renderImage(renderer, viewport)) {
      renderStatus(renderer, viewport, tr(STR_AIRPAGE_NO_IMAGE));
    }
    return;
  }
  renderQr(renderer, viewport);
}

void AirPageFace::renderQr(GfxRenderer& renderer, const Rect& viewport) {
  const std::string url = std::string("https://") + kAirPageBase + "/?id=" + airpage::deviceId() + "&type=x4";
  const int qr = std::min(viewport.width, viewport.height) * 3 / 5;
  const Rect box(viewport.x + (viewport.width - qr) / 2, viewport.y + (viewport.height - qr) / 2 - 24, qr, qr);
  QrUtils::drawQrCode(renderer, box, url);

  const char* hint = pendingError_ ? tr(STR_AIRPAGE_FETCH_FAILED) : tr(STR_AIRPAGE_QR_HINT);
  renderer.drawCenteredText(SMALL_FONT_ID, box.y + box.height + 20, hint, /*black=*/true);

  // Live-push indicator below the hint. Manual mode shows nothing.
  if (realtimeMode_) {
    const char* ind =
        (mqttState_ == MqttState::Online) ? tr(STR_AIRPAGE_REALTIME_LIVE) : tr(STR_AIRPAGE_REALTIME_CONNECTING);
    renderer.drawCenteredText(SMALL_FONT_ID, box.y + box.height + 20 + renderer.getLineHeight(SMALL_FONT_ID) + 4, ind,
                              /*black=*/true);
  }
}

void AirPageFace::renderStatus(GfxRenderer& renderer, const Rect& viewport, const char* msg) {
  renderer.drawCenteredText(UI_12_FONT_ID, viewport.y + viewport.height / 2, msg, /*black=*/true);
}

void AirPageFace::renderMenu(GfxRenderer& renderer, const Rect& viewport) {
  constexpr int kRowH = 44;
  constexpr int kPadX = 26;
  constexpr int kTitleH = 40;
  constexpr int kHintH = 28;
  constexpr int kDotD = 8;

  const int boxW = std::min(viewport.width - 60, 360);
  const int boxH = kTitleH + kMenuRows * kRowH + kHintH + 16;
  const int boxX = viewport.x + (viewport.width - boxW) / 2;
  const int boxY = viewport.y + (viewport.height - boxH) / 2;

  // White panel with a black border so it reads as a popup over the screen.
  renderer.fillRoundedRect(boxX, boxY, boxW, boxH, 10, Color::White);
  renderer.drawRoundedRect(boxX, boxY, boxW, boxH, /*lineWidth=*/2, /*cornerRadius=*/10, /*state=*/true);

  renderer.drawCenteredText(UI_12_FONT_ID, boxY + 12, tr(STR_AIRPAGE_MENU_TITLE), /*black=*/true);

  const StrId rowIds[kMenuRows] = {StrId::STR_AIRPAGE_MODE_MANUAL, StrId::STR_AIRPAGE_MODE_REALTIME};
  const int lineH = renderer.getLineHeight(UI_12_FONT_ID);
  int rowY = boxY + kTitleH;
  for (uint8_t i = 0; i < kMenuRows; ++i) {
    const bool selected = (i == menuSel_);
    const bool active = ((i == 1) == realtimeMode_);  // marker on the active mode
    if (selected) {
      renderer.fillRoundedRect(boxX + 10, rowY + 4, boxW - 20, kRowH - 8, 6, Color::Black);
    }
    // Active-mode dot. White on the highlighted (black) row, black otherwise.
    if (active) {
      renderer.fillRoundedRect(boxX + kPadX, rowY + (kRowH - kDotD) / 2, kDotD, kDotD, kDotD / 2,
                               selected ? Color::White : Color::Black);
    }
    const int textY = rowY + (kRowH - lineH) / 2;
    renderer.drawText(UI_12_FONT_ID, boxX + kPadX + kDotD + 12, textY, I18n::getInstance().get(rowIds[i]),
                      /*black=*/!selected);
    rowY += kRowH;
  }

  renderer.drawCenteredText(SMALL_FONT_ID, boxY + boxH - kHintH + 4, tr(STR_AIRPAGE_MENU_HINT), /*black=*/true);
}

bool AirPageFace::renderImage(GfxRenderer& renderer, const Rect& viewport) {
  // Re-open + parse on every call so the BW / GRAYSCALE_LSB / GRAYSCALE_MSB
  // passes each stream the file independently (drawBitmap reads rows
  // sequentially). drawBitmap honors the renderer's current renderMode.
  FsFile file;
  if (!Storage.openFileForRead("AIRP", kImagePath, file)) return false;
  Bitmap bitmap(file, /*dithering=*/false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    file.close();
    return false;
  }
  int x = viewport.x + (viewport.width - bitmap.getWidth()) / 2;
  int y = viewport.y + (viewport.height - bitmap.getHeight()) / 2;
  if (x < viewport.x) x = viewport.x;
  if (y < viewport.y) y = viewport.y;
  renderer.drawBitmap(bitmap, x, y, viewport.width, viewport.height, 0, 0);
  file.close();  // close before the next pass re-opens the same path
  return true;
}
