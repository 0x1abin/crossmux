#pragma once

#include <PubSubClient.h>
#include <WiFi.h>

#include <cstdint>

class GfxRenderer;

namespace airpage {

class AirPageConnection final {
 public:
  enum class State : uint8_t {
    Off,
    WifiConnecting,
    WifiOnline,
    BrokerConnecting,
    Online,
    Backoff,
    Paused,
  };

  enum class Event : uint8_t {
    None,
    StateChanged,
    WifiRequired,
    WifiFailed,
    RealtimeRetrying,
    RealtimePaused,
    PushRequested,
  };

  explicit AirPageConnection(GfxRenderer& renderer) : renderer_(renderer), mqtt_(mqttNet_) {}

  Event begin(bool realtime);
  void stop();
  Event setRealtime(bool enabled);
  Event acceptWifiSelection(bool connected);
  Event pump(bool allowPush);
  Event handleWifiFailure();
  void prepareRefresh();

  bool wifiConnected() const { return WiFi.status() == WL_CONNECTED; }
  bool realtime() const { return realtime_; }
  bool preventsAutoSleep() const;
  State state() const { return state_; }

 private:
  bool startWifiAssociation();
  void teardownWifi();
  bool connectBroker();
  void disconnectBroker();
  void resetRetryWindow();
  Event scheduleRetry();
  Event pause(Event event);

  bool realtime_ = false;
  bool ownsWifi_ = false;
  State state_ = State::Off;
  uint8_t retryCount_ = 0;
  uint32_t wifiAttemptStartedMs_ = 0;
  uint32_t retryWindowStartedMs_ = 0;
  uint32_t nextRetryAtMs_ = 0;

  GfxRenderer& renderer_;
  WiFiClient mqttNet_;
  PubSubClient mqtt_;
};

}  // namespace airpage
