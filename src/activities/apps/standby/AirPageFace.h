#pragma once

#include <PubSubClient.h>
#include <WiFi.h>

#include <cstdint>

#include "StandbyFace.h"

class GfxRenderer;

// Standby face that shows a QR code pointing at this device's cloud upload page,
// fetches a cloud-rendered image over WiFi on the side DOWN button, and displays
// it full-screen in 4-level grayscale. Confirm cycles between the QR page and
// the last-downloaded image (cached on SD, survives reboot).
//
// Long-pressing Confirm opens a small mode menu (Manual / Live push). In Live
// push mode the face holds an MQTT connection to the cloud broker (plain TCP,
// anonymous) and auto-fetches the moment the cloud publishes a refresh message
// for this device — no button press needed. Manual mode is unchanged: DOWN
// fetches on demand. The live mode flag is persisted on SD (airpage::*RealtimeMode).
//
// Interactive face (see StandbyFace::isInteractive): owns Confirm and renders
// its image full-screen. The network fetch runs synchronously on the main
// thread (the established FontDownloadActivity pattern) so the SD write never
// races the e-ink SPI bus — the "Fetching…" status is drawn first, then the
// blocking download runs on the next tick with the screen already painted.
class AirPageFace final : public StandbyFace {
 public:
  void onEnter() override;
  void onExit() override;
  bool tick() override;
  void render(GfxRenderer& renderer, const Rect& viewport) override;
  StrId titleId() const override;
  uint32_t secondsUntilNextWake() const override { return 3600; }

  bool isInteractive() const override { return true; }
  bool handleConfirm() override;           // Confirm: menu select, or toggle QR <-> image
  bool handleConfirmLongPress() override;  // long Confirm: open the mode menu
  bool wantsExclusiveInput() const override { return menuOpen_; }
  bool handleBack() override;  // Back: close the menu
  void onPagePrev() override;  // UP: move menu selection (menu open)
  void onPageNext() override;  // DOWN: menu selection, or request a cloud fetch
  bool wantsImmediateGrayscale() const override { return view_ == View::Image && !menuOpen_; }

 private:
  enum class View : uint8_t { Qr, Image };
  // Idle -> Requested (status painted) -> Fetching (blocking) -> Idle.
  enum class Phase : uint8_t { Idle, Requested, Fetching };
  // Off (manual mode) -> Connecting -> Online. Only used in live-push mode.
  enum class MqttState : uint8_t { Off, Connecting, Online };

  void requestFetch();        // queue a fetch (DOWN / push / live-mode entry all funnel here)
  void doFetch();             // blocking: connect WiFi (if needed) + download to SD
  bool ensureWifi();          // blocking: reuse/raise WiFi from saved credentials
  void teardownWifi();        // drop only WiFi we ourselves raised (lets CPU downclock)
  void pumpMqtt();            // live mode: maintain the broker connection + deliver pushes
  bool connectBroker();       // connect + subscribe (assumes WiFi already up)
  void enterLiveMode();       // turn live push on: arm reconnect + initial fetch
  void exitLiveMode();        // turn live push off: drop MQTT + WiFi
  void applyMenuSelection();  // commit the highlighted menu row

  bool renderImage(GfxRenderer& renderer, const Rect& viewport);
  void renderQr(GfxRenderer& renderer, const Rect& viewport);
  void renderStatus(GfxRenderer& renderer, const Rect& viewport, const char* msg);
  void renderMenu(GfxRenderer& renderer, const Rect& viewport);

  View view_ = View::Qr;
  Phase phase_ = Phase::Idle;
  bool haveCachedImage_ = false;
  bool pendingError_ = false;
  bool weBroughtWifiUp_ = false;  // only tear down WiFi we ourselves brought up
  bool needsRedraw_ = true;

  // Mode menu (long-press Confirm). Two rows: 0 = Manual, 1 = Live push.
  bool menuOpen_ = false;
  uint8_t menuSel_ = 0;

  // Live-push mode + MQTT connection state.
  bool realtimeMode_ = false;
  MqttState mqttState_ = MqttState::Off;
  uint32_t lastConnectAttemptMs_ = 0;  // throttles WiFi+broker reconnect as one unit
  WiFiClient mqttNet_;
  PubSubClient mqtt_{mqttNet_};
};
