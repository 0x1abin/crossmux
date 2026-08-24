#include "AirPageConnection.h"

#include <Arduino.h>
#include <HalGPIO.h>
#include <Logging.h>
#include <esp_wifi.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <iterator>

#include "AirPageDeviceId.h"
#include "NetworkStartup.h"
#include "WifiCredentialStore.h"

namespace airpage {

namespace {

constexpr char kMqttHost[] = "mqtt-cn.uipcat.com";
constexpr uint16_t kMqttPort = 1883;
constexpr uint16_t kMqttSocketTimeoutSeconds = 5;
constexpr uint16_t kMqttKeepAliveSeconds = 57;
constexpr char kMqttOnline[] = "online";
constexpr char kMqttOffline[] = "offline";
constexpr uint32_t kWifiConnectTimeoutMs = 15000u;
constexpr uint32_t kLiveRetryWindowMs = 120000u;
constexpr uint32_t kRetryDelaysMs[] = {5000u, 10000u, 20000u, 30000u};

// PubSubClient invokes this callback synchronously from mqtt_.loop().
volatile bool sPushPending = false;

void onMqttMessage(char* /*topic*/, uint8_t* /*payload*/, unsigned int /*length*/) { sPushPending = true; }

bool timeReached(const uint32_t now, const uint32_t target) { return static_cast<int32_t>(now - target) >= 0; }

}  // namespace

AirPageConnection::Event AirPageConnection::begin(const bool realtime) {
  realtime_ = realtime;
  ownsWifi_ = false;
  state_ = State::Off;
  retryCount_ = 0;
  sPushPending = false;

  mqtt_.setServer(kMqttHost, kMqttPort);
  mqtt_.setCallback(&onMqttMessage);
  mqtt_.setSocketTimeout(kMqttSocketTimeoutSeconds);
  mqtt_.setKeepAlive(kMqttKeepAliveSeconds);

  resetRetryWindow();
  if (wifiConnected()) {
    NetworkStartup::prepare(renderer_);
    ownsWifi_ = realtime_;
    state_ = realtime_ ? State::BrokerConnecting : State::WifiOnline;
    return Event::StateChanged;
  }
  state_ = State::Off;
  return Event::WifiRequired;
}

void AirPageConnection::stop() {
  disconnectBroker();
  state_ = State::Off;
  sPushPending = false;
  teardownWifi();
}

bool AirPageConnection::preventsAutoSleep() const {
  switch (state_) {
    case State::Off:
    case State::WifiOnline:
    case State::Paused:
      return false;
    case State::WifiConnecting:
      return true;
    case State::BrokerConnecting:
    case State::Online:
    case State::Backoff:
      return realtime_;
  }
  return false;
}

AirPageConnection::Event AirPageConnection::setRealtime(const bool enabled) {
  if (enabled == realtime_) return Event::None;
  realtime_ = enabled;
  retryCount_ = 0;

  if (!realtime_) {
    disconnectBroker();
    if (wifiConnected()) {
      state_ = State::WifiOnline;
    } else {
      state_ = State::Off;
    }
    return Event::StateChanged;
  }

  resetRetryWindow();
  if (wifiConnected()) {
    ownsWifi_ = true;
    state_ = State::BrokerConnecting;
    return Event::StateChanged;
  }
  return pause(Event::WifiRequired);
}

void AirPageConnection::prepareRefresh() {
  if (wifiConnected()) ownsWifi_ = true;
  if (!realtime_ || state_ == State::Online) return;
  resetRetryWindow();
  state_ = State::BrokerConnecting;
}

AirPageConnection::Event AirPageConnection::acceptWifiSelection(const bool connected) {
  if (!connected || !wifiConnected()) {
    if (realtime_) return pause(Event::WifiRequired);
    state_ = State::Off;
    return Event::WifiRequired;
  }

  ownsWifi_ = true;
  if (realtime_) {
    resetRetryWindow();
    state_ = State::BrokerConnecting;
  } else {
    state_ = State::WifiOnline;
  }
  return Event::StateChanged;
}

void AirPageConnection::resetRetryWindow() {
  retryCount_ = 0;
  retryWindowStartedMs_ = millis();
  nextRetryAtMs_ = retryWindowStartedMs_;
}

AirPageConnection::Event AirPageConnection::scheduleRetry() {
  const uint32_t now = millis();
  if (now - retryWindowStartedMs_ >= kLiveRetryWindowMs) return pause(Event::RealtimePaused);

  const size_t delayIndex = std::min<size_t>(retryCount_, std::size(kRetryDelaysMs) - 1);
  nextRetryAtMs_ = now + kRetryDelaysMs[delayIndex];
  if (retryCount_ < UINT8_MAX) ++retryCount_;
  state_ = State::Backoff;
  return Event::RealtimeRetrying;
}

AirPageConnection::Event AirPageConnection::pause(const Event event) {
  disconnectBroker();
  state_ = State::Paused;
  teardownWifi();
  return event;
}

bool AirPageConnection::connectBroker() {
  char clientId[24];
  snprintf(clientId, sizeof(clientId), "%s-%s", gpio.deviceIsX3() ? "x3" : "x4", deviceId().c_str());
  char statusTopic[64];
  snprintf(statusTopic, sizeof(statusTopic), "airpage/device/%s/status", deviceId().c_str());
  if (!mqtt_.connect(clientId, statusTopic, 0, true, kMqttOffline)) {
    LOG_ERR("AIRP", "MQTT connect failed (state=%d)", mqtt_.state());
    return false;
  }

  char refreshTopic[64];
  snprintf(refreshTopic, sizeof(refreshTopic), "airpage/device/%s/refresh", deviceId().c_str());
  if (!mqtt_.subscribe(refreshTopic)) {
    LOG_ERR("AIRP", "MQTT subscribe failed: %s", refreshTopic);
    disconnectBroker();
    return false;
  }
  if (!mqtt_.publish(statusTopic, kMqttOnline, true)) {
    LOG_ERR("AIRP", "MQTT online status failed: %s", statusTopic);
    disconnectBroker();
    return false;
  }
  LOG_INF("AIRP", "MQTT online, subscribed %s", refreshTopic);
  return true;
}

void AirPageConnection::disconnectBroker() {
  if (!mqtt_.connected()) return;
  char statusTopic[64];
  snprintf(statusTopic, sizeof(statusTopic), "airpage/device/%s/status", deviceId().c_str());
  if (!mqtt_.publish(statusTopic, kMqttOffline, true)) {
    LOG_ERR("AIRP", "MQTT offline status failed: %s", statusTopic);
  }
  mqtt_.disconnect();
}

AirPageConnection::Event AirPageConnection::pump(const bool allowPush) {
  const uint32_t now = millis();
  switch (state_) {
    case State::Off:
    case State::Paused:
      return Event::None;

    case State::WifiConnecting: {
      const wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED) {
        state_ = realtime_ ? State::BrokerConnecting : State::WifiOnline;
        return Event::StateChanged;
      }
      if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL ||
          now - wifiAttemptStartedMs_ >= kWifiConnectTimeoutMs) {
        LOG_ERR("AIRP", "WiFi association failed (status=%d)", static_cast<int>(status));
        if (realtime_) return scheduleRetry();
        state_ = State::Off;
        teardownWifi();
        return Event::WifiFailed;
      }
      return Event::None;
    }

    case State::WifiOnline:
      if (!wifiConnected()) {
        state_ = State::Off;
        teardownWifi();
        return Event::WifiFailed;
      }
      return Event::None;

    case State::BrokerConnecting:
      if (!wifiConnected() || !connectBroker()) return scheduleRetry();
      state_ = State::Online;
      retryCount_ = 0;
      return Event::StateChanged;

    case State::Online:
      if (!mqtt_.connected()) {
        resetRetryWindow();
        return scheduleRetry();
      }
      mqtt_.loop();
      if (sPushPending && allowPush) {
        sPushPending = false;
        LOG_INF("AIRP", "push received -> fetch");
        return Event::PushRequested;
      }
      return Event::None;

    case State::Backoff:
      if (now - retryWindowStartedMs_ >= kLiveRetryWindowMs) return pause(Event::RealtimePaused);
      if (!timeReached(now, nextRetryAtMs_)) return Event::None;
      if (wifiConnected()) {
        state_ = State::BrokerConnecting;
        return Event::StateChanged;
      }
      if (!startWifiAssociation()) return pause(Event::WifiRequired);
      return Event::StateChanged;
  }
  return Event::None;
}

AirPageConnection::Event AirPageConnection::handleWifiFailure() {
  if (realtime_) {
    resetRetryWindow();
    return scheduleRetry();
  }
  state_ = State::Off;
  teardownWifi();
  return Event::WifiFailed;
}

bool AirPageConnection::startWifiAssociation() {
  if (WIFI_STORE.getCredentialCount() == 0) WIFI_STORE.loadFromFile();
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const auto credential = WIFI_STORE.findCredential(lastSsid);
  if (!credential) {
    LOG_ERR("AIRP", "No saved WiFi credential");
    return false;
  }

  WiFi.persistent(false);
  NetworkStartup::setMode(renderer_, WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  if (credential->password.empty()) {
    WiFi.begin(credential->ssid.c_str());
  } else {
    WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  }
  ownsWifi_ = true;
  wifiAttemptStartedMs_ = millis();
  state_ = State::WifiConnecting;
  return true;
}

void AirPageConnection::teardownWifi() {
  if (!ownsWifi_) return;
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  esp_wifi_deinit();
  ownsWifi_ = false;
}

}  // namespace airpage
