#pragma once

#include <cstdint>

#include "StandbyFace.h"

class GfxRenderer;

// Standby face that shows a QR code pointing at this device's cloud upload page,
// fetches a cloud-rendered image over WiFi on the side DOWN button, and displays
// it full-screen in 4-level grayscale. Confirm cycles between the QR page and
// the last-downloaded image (cached on SD, survives reboot).
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
  bool handleConfirm() override;  // Confirm: toggle QR <-> image
  void onPageNext() override;     // DOWN: request a cloud fetch
  bool wantsImmediateGrayscale() const override { return view_ == View::Image; }

 private:
  enum class View : uint8_t { Qr, Image };
  // Idle -> Requested (status painted) -> Fetching (blocking) -> Idle.
  enum class Phase : uint8_t { Idle, Requested, Fetching };

  void doFetch();  // blocking: connect WiFi (if needed) + download to SD
  bool renderImage(GfxRenderer& renderer, const Rect& viewport);
  void renderQr(GfxRenderer& renderer, const Rect& viewport);
  void renderStatus(GfxRenderer& renderer, const Rect& viewport, const char* msg);

  View view_ = View::Qr;
  Phase phase_ = Phase::Idle;
  bool haveCachedImage_ = false;
  bool pendingError_ = false;
  bool weBroughtWifiUp_ = false;  // only tear down WiFi we ourselves brought up
  bool needsRedraw_ = true;
};
