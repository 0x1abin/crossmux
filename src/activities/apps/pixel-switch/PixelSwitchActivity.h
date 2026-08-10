#pragma once

#include <PubSubClient.h>
#include <WiFi.h>

#include <cstdint>

#include "../../Activity.h"
#include "PixelSwitchState.h"

class PixelSwitchActivity final : public Activity {
 public:
  PixelSwitchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("PixelSwitch", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  bool preventAutoSleep() override { return true; }

 private:
  enum class View : uint8_t {
    Intro,
    Canvas,
    Palette,
  };

  static PixelSwitchActivity* active_;
  static void mqttCallback(char* topic, uint8_t* payload, unsigned int length);

  void handleMqttMessage(const char* topic, const uint8_t* payload, size_t length);
  bool canPublish();
  bool prepareMqttBuffer();
  void startCanvas();
  void launchWifiSelection(bool enterCanvasAfterConnect);
  bool consumeWifiInputReleaseBarrier();
  void pumpNetwork();
  bool connectBroker();
  bool startSavedWifiAssociation();
  void teardownOwnedWifi();
  void handleIntroInput();
  void handleCanvasInput();
  void handlePaletteInput();
  void moveCursor(int dx, int dy);
  void movePalette(int delta);
  void attemptPlacement(pixel_switch::Shade shade);
  pixel_switch::FlushResult flushPending(uint32_t now);

  void drawCanvas() const;
  void drawPalette() const;
  void drawIntro() const;
  void drawCooldown() const;

  pixel_switch::PixelSwitchState canvas_;
  pixel_switch::PixelSwitchPendingBatch pending_;
  int cursorX_ = pixel_switch::DISPLAY_WIDTH / 2;
  int cursorY_ = pixel_switch::DISPLAY_HEIGHT / 2;
  pixel_switch::Shade defaultShade_ = pixel_switch::Shade::Black;
  pixel_switch::Shade paletteSelection_ = pixel_switch::Shade::Black;
  View view_ = View::Canvas;
  bool mqttBufferReady_ = false;
  bool broughtWifiUp_ = false;
  bool snapshotReady_ = false;
  bool ignoreConfirmRelease_ = false;
  bool waitForWifiInputRelease_ = false;
  bool wifiSelectionFailed_ = false;
  bool wifiRetryActive_ = false;
  bool wifiRetryPaused_ = false;
  bool showCooldown_ = false;
  uint32_t lastConnectAttemptMs_ = 0;
  uint32_t wifiRetryStartedMs_ = 0;
  uint32_t subscribedAtMs_ = 0;
  uint32_t cooldownStartedMs_ = 0;
  uint32_t lastPlacementMs_ = 0;

  WiFiClient mqttNet_;
  PubSubClient mqtt_{mqttNet_};
};
