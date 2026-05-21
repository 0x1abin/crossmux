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
constexpr char kAirPageBase[] = "pages.uipcat.com";

// SD persistence (under the existing .crosspoint cache root). Survives reboot.
constexpr char kCacheDir[] = "/.crosspoint/airpage";
constexpr char kImagePath[] = "/.crosspoint/airpage/latest.bmp";

constexpr uint32_t kWifiConnectTimeoutMs = 15000u;  // matches StandbyActivity

}  // namespace

void AirPageFace::onEnter() {
  view_ = View::Qr;
  phase_ = Phase::Idle;
  pendingError_ = false;
  haveCachedImage_ = Storage.exists(kImagePath);
  needsRedraw_ = true;
  LOG_DBG("AIRP", "onEnter id=%s cached=%d", airpage::deviceId().c_str(), haveCachedImage_ ? 1 : 0);
}

void AirPageFace::onExit() {
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

void AirPageFace::onPageNext() {
  // DOWN: request a fetch. Phase::Requested lets render() paint the "Fetching…"
  // status this frame; tick() runs the blocking download on the next frame.
  if (phase_ != Phase::Idle) return;
  phase_ = Phase::Requested;
  needsRedraw_ = true;
}

bool AirPageFace::handleConfirm() {
  if (phase_ != Phase::Idle) return true;  // consume, ignore mid-fetch
  pendingError_ = false;
  if (view_ == View::Qr) {
    if (haveCachedImage_) view_ = View::Image;  // nothing to show yet -> stay on QR
  } else {
    view_ = View::Qr;
  }
  needsRedraw_ = true;
  return true;
}

bool AirPageFace::tick() {
  if (phase_ == Phase::Requested) {
    // The "Fetching…" status was painted on the prior frame (onPageNext set
    // Requested and StandbyActivity::loop requested a redraw). Now run the
    // blocking fetch with the status already on the e-ink panel.
    phase_ = Phase::Fetching;
    doFetch();
    phase_ = Phase::Idle;
    return true;  // redraw: image on success, or QR with error hint
  }
  if (needsRedraw_) {
    needsRedraw_ = false;
    return true;
  }
  return false;
}

void AirPageFace::doFetch() {
  pendingError_ = false;

  // 1. Ensure WiFi. Reuse an existing connection (e.g. left up by NTP sync);
  //    otherwise connect with the last-used saved credentials.
  if (WiFi.status() != WL_CONNECTED) {
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
      LOG_ERR("AIRP", "No saved WiFi credential for fetch");
      pendingError_ = true;
      return;
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
      pendingError_ = true;
      return;
    }
    weBroughtWifiUp_ = true;
  }

  // 2. Download to SD on the main thread — the e-ink panel is idle during this
  //    blocking call, so the shared SPI bus has no SD/render contention (same
  //    pattern as FontDownloadActivity).
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
  const std::string url = std::string("https://") + kAirPageBase + "/x4/" + airpage::deviceId();
  const int qr = std::min(viewport.width, viewport.height) * 3 / 5;
  const Rect box(viewport.x + (viewport.width - qr) / 2, viewport.y + (viewport.height - qr) / 2 - 24, qr, qr);
  QrUtils::drawQrCode(renderer, box, url);

  const char* hint = pendingError_ ? tr(STR_AIRPAGE_FETCH_FAILED) : tr(STR_AIRPAGE_QR_HINT);
  renderer.drawCenteredText(SMALL_FONT_ID, box.y + box.height + 20, hint, /*black=*/true);
}

void AirPageFace::renderStatus(GfxRenderer& renderer, const Rect& viewport, const char* msg) {
  renderer.drawCenteredText(UI_12_FONT_ID, viewport.y + viewport.height / 2, msg, /*black=*/true);
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
