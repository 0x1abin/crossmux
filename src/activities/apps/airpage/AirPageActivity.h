#pragma once

#include <PubSubClient.h>
#include <WiFi.h>

#include <cstdint>

#include "../../Activity.h"

struct Rect;

// Standalone AirPage app: shows the device upload QR, fetches the latest
// cloud-rendered image, and optionally keeps an MQTT live-push connection while
// the app is open.
class AirPageActivity final : public Activity {
 public:
  explicit AirPageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AirPage", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return realtimeMode_; }

 private:
  enum class View : uint8_t { Qr, Image };
  // Idle -> Requested (status painted) -> Fetching (blocking) -> Idle.
  enum class Phase : uint8_t { Idle, Requested, Fetching };
  // Off (manual mode) -> Connecting -> Online. Only used in live-push mode.
  enum class MqttState : uint8_t { Off, Connecting, Online };

  void toggleView();
  void openModeMenu();
  void applyMenuSelection();
  void requestFetch();
  void doFetch();
  bool ensureWifi();
  bool startWifiAssociation();
  void teardownWifi();
  void pumpMqtt();
  bool connectBroker();
  void enterLiveMode();
  void exitLiveMode();

  bool renderImage(const Rect& viewport);
  void renderQr(const Rect& viewport);
  void renderStatus(const Rect& viewport, const char* msg);
  void renderMenu(const Rect& viewport);

  View view_ = View::Qr;
  Phase phase_ = Phase::Idle;
  bool haveCachedImage_ = false;
  bool pendingError_ = false;
  bool weBroughtWifiUp_ = false;

  bool menuOpen_ = false;
  uint8_t menuSel_ = 0;

  bool realtimeMode_ = false;
  MqttState mqttState_ = MqttState::Off;
  uint32_t lastConnectAttemptMs_ = 0;
  WiFiClient mqttNet_;
  PubSubClient mqtt_{mqttNet_};
};
