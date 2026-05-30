#include "StandbyActivity.h"

#include <Arduino.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <string>

#include "../../ActivityResult.h"
#include "../../network/WifiSelectionActivity.h"
#ifdef ENABLE_CHINESE_VERSION
#include "ChineseCalendarFace.h"
#endif
#include "AirPageFace.h"
#include "SloppyClockFace.h"
#include "StandbyTime.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr long kTzOffsetSec = 8 * 3600;      // UTC+8 (Beijing)
constexpr uint32_t kWifiTimeoutMs = 15000u;  // Same as WifiSelectionActivity
constexpr uint32_t kNtpTimeoutMs = 12000u;   // SNTP poll budget (multi-server DNS + handshake)

// Face factory table. Add new faces by appending a row here and including the
// corresponding header above. Each entry also declares an isAvailable()
// predicate so faces can be hidden under specific runtime conditions (e.g.
// orientation-only). When a face is unavailable it is skipped during cycling
// and the bottom pager-dot strip collapses accordingly.
struct FaceEntry {
  std::unique_ptr<StandbyFace> (*create)();
  bool (*isAvailable)(int sw, int sh);
};
constexpr FaceEntry kFaces[] = {
    {[]() -> std::unique_ptr<StandbyFace> { return makeUniqueNoThrow<SloppyClockFace>(); },
     [](int, int) { return true; }},
#ifdef ENABLE_CHINESE_VERSION
    {[]() -> std::unique_ptr<StandbyFace> { return makeUniqueNoThrow<ChineseCalendarFace>(); },
     [](int sw, int sh) { return sh > sw; }},  // portrait only
#endif
    // AirPage: cloud-rendered image pages. Available in every build; sits after
    // the Chinese calendar entry when that is compiled in, else after the clock.
    {[]() -> std::unique_ptr<StandbyFace> { return makeUniqueNoThrow<AirPageFace>(); }, [](int, int) { return true; }},
};
constexpr uint8_t kFaceCount = static_cast<uint8_t>(sizeof(kFaces) / sizeof(kFaces[0]));

uint8_t countAvailableFaces(int sw, int sh) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kFaceCount; ++i) {
    if (kFaces[i].isAvailable(sw, sh)) ++n;
  }
  return n;
}

// Map a "rank among available faces" (0-based) back to a kFaces[] index.
// Returns kFaceCount if none match (only happens when no face is available,
// which can't currently occur since SloppyClockFace is always available).
uint8_t availableFaceIndexFromRank(int sw, int sh, uint8_t rank) {
  uint8_t seen = 0;
  for (uint8_t i = 0; i < kFaceCount; ++i) {
    if (!kFaces[i].isAvailable(sw, sh)) continue;
    if (seen == rank) return i;
    ++seen;
  }
  return kFaceCount;
}

uint8_t rankOfAvailableFace(int sw, int sh, uint8_t faceIdx) {
  uint8_t seen = 0;
  for (uint8_t i = 0; i < kFaceCount; ++i) {
    if (!kFaces[i].isAvailable(sw, sh)) continue;
    if (i == faceIdx) return seen;
    ++seen;
  }
  return 0;  // faceIdx is unavailable — caller treats it as "use the first"
}

// Once per power-on, Standby may push the WiFi selection UI to help the user
// get online for NTP sync. After it has been shown (regardless of whether the
// user connected or cancelled), subsequent Standby entries within the same
// session will not re-prompt — the explicit way to retry is Settings → WiFi.
// This module-scope flag persists across activity destroy/recreate.
bool g_promptedForWifiThisSession = false;

// Bottom-center page indicator dots. One dot per face, filled for the current
// face. Drawn only in Normal mode — hidden once we go Immersive.
constexpr int kDotDiameter = 6;
constexpr int kDotSpacing = 14;      // gap between dot centers
constexpr int kDotBottomInset = 24;  // distance from bottom edge to top of dots

void drawFaceDots(const GfxRenderer& renderer, int sw, int sh, uint8_t total, uint8_t current) {
  if (total == 0) return;
  const int totalWidth = total * kDotDiameter + (total - 1) * kDotSpacing;
  const int startX = (sw - totalWidth) / 2;
  const int y = sh - kDotBottomInset;
  for (uint8_t i = 0; i < total; ++i) {
    const int x = startX + i * (kDotDiameter + kDotSpacing);
    if (i == current) {
      renderer.fillRoundedRect(x, y, kDotDiameter, kDotDiameter, kDotDiameter / 2, Color::Black);
    } else {
      // 1 px outlined dot for inactive faces.
      renderer.drawRoundedRect(x, y, kDotDiameter, kDotDiameter, /*lineWidth=*/1, kDotDiameter / 2,
                               /*state=*/true);
    }
  }
}

}  // namespace

void StandbyActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("STANDBY", "onEnter free heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
  // Always default to face 0 (Sloppy Clock). If face 0 is somehow unavailable
  // (currently impossible), fall back to the first available index.
  faceIndex_ = 0;
  if (!kFaces[0].isAvailable(renderer.getScreenWidth(), renderer.getScreenHeight())) {
    const uint8_t idx = availableFaceIndexFromRank(renderer.getScreenWidth(), renderer.getScreenHeight(), 0);
    if (idx < kFaceCount) faceIndex_ = idx;
  }
  inverseMode_ = false;
  currentFace_ = kFaces[faceIndex_].create();
  if (!currentFace_) {
    LOG_ERR("STANDBY", "OOM allocating face");
    activityManager.goToApps();
    return;
  }
  currentFace_->onEnter();
  mode_ = DisplayMode::Normal;
  lastInputMs_ = millis();
  startTimeSync();
  requestUpdate();
}

void StandbyActivity::onExit() {
  if (syncState_ != SyncState::Idle) {
    // User backed out while sync was running — tear down cleanly.
    if (esp_sntp_enabled()) esp_sntp_stop();
    WiFi.disconnect(false);
    delay(100);
    WiFi.mode(WIFI_OFF);
    syncState_ = SyncState::Idle;
  }
  if (currentFace_) {
    currentFace_->onExit();
    currentFace_.reset();
  }
  LOG_DBG("STANDBY", "onExit free heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
  Activity::onExit();
}

void StandbyActivity::switchFace(int8_t delta) {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const uint8_t avail = countAvailableFaces(sw, sh);
  if (avail <= 1) return;

  const uint8_t curRank = rankOfAvailableFace(sw, sh, faceIndex_);
  const uint8_t newRank = static_cast<uint8_t>((curRank + avail + delta) % avail);
  const uint8_t newIdx = availableFaceIndexFromRank(sw, sh, newRank);
  if (newIdx >= kFaceCount || newIdx == faceIndex_) return;

  if (currentFace_) currentFace_->onExit();
  currentFace_.reset();
  faceIndex_ = newIdx;
  currentFace_ = kFaces[faceIndex_].create();
  if (!currentFace_) {
    LOG_ERR("STANDBY", "OOM switching face");
    activityManager.goToApps();
    return;
  }
  currentFace_->onEnter();
  requestUpdate();
}

void StandbyActivity::startTimeSync() {
  if (standby_time::isSynced()) return;

  // Another activity (e.g. Settings → WiFi) may have already connected the
  // device. Skip our own WiFi.begin in that case and go straight to NTP.
  if (WiFi.status() == WL_CONNECTED) {
    LOG_DBG("STANDBY", "WiFi already connected, skipping silent attempt");
    beginNtpSync();
    return;
  }

  // Default path: try the saved "last connected" credentials silently.
  if (trySilentWifiConnect()) {
    return;  // pumpTimeSync() drives the WifiConnecting → NtpSyncing transition
  }

  // No credentials available to try. Push the WiFi selection UI (once per
  // session) so the user can connect manually.
  if (!g_promptedForWifiThisSession) {
    promptForWifi();
  }
}

bool StandbyActivity::trySilentWifiConnect() {
  // SD I/O shares SPI with the e-ink panel, so we hold a RenderLock for the
  // duration of the credential file access.
  std::string ssid;
  std::string pass;
  {
    RenderLock lock(*this);
    if (WIFI_STORE.getCredentials().empty()) WIFI_STORE.loadFromFile();
    const std::string& last = WIFI_STORE.getLastConnectedSsid();
    if (last.empty()) {
      LOG_DBG("STANDBY", "No lastConnectedSsid for silent connect");
      return false;
    }
    const WifiCredential* cred = WIFI_STORE.findCredential(last);
    if (!cred) {
      LOG_DBG("STANDBY", "lastConnectedSsid '%s' has no saved credential", last.c_str());
      return false;
    }
    ssid = cred->ssid;
    pass = cred->password;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // Fresh-slate disconnect (radio off + erase cached AP) so the next begin()
  // latches the exact creds we just loaded, not whatever the radio retained
  // from a previous session. The disconnect(false) form used by the teardown
  // paths is deliberately different — there we want to keep the AP cached
  // for the next silent-connect attempt.
  WiFi.disconnect(true, true);
  delay(100);
  if (pass.empty()) {
    WiFi.begin(ssid.c_str());
  } else {
    WiFi.begin(ssid.c_str(), pass.c_str());
  }
  syncState_ = SyncState::WifiConnecting;
  syncStartMs_ = millis();
  LOG_DBG("STANDBY", "Silent WiFi connect start: %s", ssid.c_str());
  return true;
}

void StandbyActivity::promptForWifi() {
  // Set the flag before pushing so a synchronous edge-case completion can't
  // re-prompt. autoConnect=false: the UI must not retry the same credential
  // we just failed on; let the user pick.
  g_promptedForWifiThisSession = true;
  LOG_DBG("STANDBY", "Prompting WiFi selection UI");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, /*autoConnect=*/false),
                         [this](const ActivityResult& result) { onWifiResult(result); });
}

void StandbyActivity::onWifiResult(const ActivityResult& result) {
  if (result.isCancelled) {
    LOG_DBG("STANDBY", "WiFi UI cancelled; staying in fallback time");
    // syncState_ is already Idle (cleared before the prompt). The face will
    // continue ticking on the pre-sync fallback clock.
    return;
  }
  // User connected via the UI; WiFi is up. Skip our own WiFi.begin and jump
  // straight to NTP. WifiSelectionActivity already persisted the credential
  // and updated lastConnectedSsid, so next boot the silent path will work.
  LOG_DBG("STANDBY", "WiFi UI returned connected; starting NTP");
  beginNtpSync();
  requestUpdate();  // refresh header back to "Syncing…"
}

void StandbyActivity::beginNtpSync() {
  if (esp_sntp_enabled()) esp_sntp_stop();
#ifdef ENABLE_CHINESE_VERSION
  // China-region servers: Aliyun is the most reliable; Tencent and the NTP
  // Pool CN node act as fallbacks. pool.ntp.org is often blocked or slow
  // inside the mainland.
  configTime(kTzOffsetSec, 0, "ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org");
#else
  configTime(kTzOffsetSec, 0, "pool.ntp.org");
#endif
  syncState_ = SyncState::NtpSyncing;
  syncStartMs_ = millis();
  LOG_DBG("STANDBY", "NTP started");
}

void StandbyActivity::pumpTimeSync() {
  if (syncState_ == SyncState::Idle) return;
  const uint32_t elapsed = millis() - syncStartMs_;

  if (syncState_ == SyncState::WifiConnecting) {
    const wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
      beginNtpSync();
      return;
    }
    if (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL || elapsed >= kWifiTimeoutMs) {
      LOG_DBG("STANDBY", "Silent WiFi sync failed (status=%d, t=%ums)", static_cast<int>(st),
              static_cast<unsigned>(elapsed));
      // Tear down our own WiFi state cleanly, then push the selection UI
      // (once per session). If we've already prompted, stay in fallback.
      if (esp_sntp_enabled()) esp_sntp_stop();
      WiFi.disconnect(false);
      delay(100);
      WiFi.mode(WIFI_OFF);
      syncState_ = SyncState::Idle;
      if (!g_promptedForWifiThisSession) {
        promptForWifi();
      } else {
        requestUpdate();  // header switches back to face title
      }
    }
    return;
  }

  if (syncState_ == SyncState::NtpSyncing) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      standby_time::setSynced(true);
      // Saved credentials worked this session — clear the "already prompted"
      // gate so that if they later fail (e.g. router password changed) the
      // next Standby entry can prompt the user again instead of staying
      // silently stuck on the fallback clock.
      g_promptedForWifiThisSession = false;
      LOG_DBG("STANDBY", "NTP synced");
      finishTimeSync();
      return;
    }
    if (elapsed >= kNtpTimeoutMs) {
      LOG_DBG("STANDBY", "NTP timeout");
      finishTimeSync();
    }
  }
}

void StandbyActivity::finishTimeSync() {
  if (esp_sntp_enabled()) esp_sntp_stop();
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  // Fully release the WiFi driver so HalPowerManager::setPowerSaving() can
  // actually drop the CPU to LOW_POWER_FREQ — it short-circuits while WiFi is
  // active (see HalPowerManager.cpp). Return value is ignored;
  // ESP_ERR_WIFI_NOT_INIT is fine if we never connected.
  esp_wifi_deinit();
  syncState_ = SyncState::Idle;
  requestUpdate();  // Header text and face content may now reflect synced time
}

void StandbyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // A face with a modal overlay (airpage mode menu) keeps Back for itself:
    // it closes the overlay instead of leaving Standby.
    if (currentFace_ && currentFace_->wantsExclusiveInput()) {
      lastInputMs_ = millis();
      if (currentFace_->handleBack()) requestUpdate();
      return;
    }
    activityManager.goHome();
    return;
  }

  // Up/Down: page navigation, dispatched to the current face.
  //   - SloppyClock: each press rerolls the style (treated as a shake).
  //   - ChineseCalendar: Up = previous day, Down = next day.
  // Pressing either button from Immersive wakes back to Normal first.
  const bool upPressed = mappedInput.wasReleased(MappedInputManager::Button::Up);
  const bool downPressed = mappedInput.wasReleased(MappedInputManager::Button::Down);
  if (upPressed || downPressed) {
    lastInputMs_ = millis();
    if (mode_ == DisplayMode::Immersive) {
      mode_ = DisplayMode::Normal;
      requestUpdate();
    } else if (currentFace_) {
      if (upPressed) {
        currentFace_->onPagePrev();
      } else {
        currentFace_->onPageNext();
      }
      requestUpdate();
    }
    return;
  }

  // Left/Right: cycle through faces. With only one face installed this is a
  // silent no-op (the keystroke is consumed but nothing visible changes).
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    lastInputMs_ = millis();
    // While a face's modal overlay is open, Left/Right do not switch faces.
    if (currentFace_ && currentFace_->wantsExclusiveInput()) return;
    if (mode_ == DisplayMode::Immersive) {
      mode_ = DisplayMode::Normal;
      requestUpdate();
      return;
    }
    const int8_t delta = mappedInput.wasReleased(MappedInputManager::Button::Right) ? +1 : -1;
    switchFace(delta);
    return;
  }

  // Power short-press: dedicated wake from Immersive back to Normal. Long-
  // press is handled by main.cpp's global shutdown path.
  if (mappedInput.wasReleased(MappedInputManager::Button::Power)) {
    lastInputMs_ = millis();
    if (mode_ == DisplayMode::Immersive) {
      mode_ = DisplayMode::Normal;
      requestUpdate();
    }
    return;
  }

  // Confirm: toggle inverse (black background / white content) regardless of
  // Normal vs. Immersive. invertScreen() flips the whole framebuffer right
  // before displayBuffer(), so title / battery / face dots / face content all
  // invert together — no per-face plumbing required.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lastInputMs_ = millis();
    // Interactive faces (airpage) own Confirm: it opens their mode menu (or
    // selects a menu row when their overlay is open) rather than the global
    // inverse mode.
    if (currentFace_ && currentFace_->isInteractive()) {
      if (currentFace_->handleConfirm()) requestUpdate();
      return;
    }
    inverseMode_ = !inverseMode_;
    requestUpdate();
    return;
  }

  pumpTimeSync();

  // After 5 s of idle, hide chrome and let the face fill the screen. From
  // there the framework's idle-3 s auto-downclock kicks in (HalPowerManager →
  // LOW_POWER_FREQ, 10 MHz). We keep preventAutoSleep() returning true so the
  // device will NOT auto-deep-sleep — standby is a clock and must keep showing
  // the time. The deep-sleep timer is short-circuited in main.cpp via
  // preventAutoSleep(); downclocking is unaffected because main.cpp only
  // resets lastActivityTime on real user input, not on preventAutoSleep().
  // Interactive faces (airpage) stay in Normal mode so they keep their chrome
  // (QR title/dots) and receive Up/Down/Confirm on the first press.
  const bool interactive = currentFace_ && currentFace_->isInteractive();
  const uint32_t idle = millis() - lastInputMs_;
  if (!interactive && mode_ == DisplayMode::Normal && idle >= 5000u) {
    mode_ = DisplayMode::Immersive;
    requestUpdate();
  }

  if (currentFace_ && currentFace_->tick()) {
    requestUpdate();
  }
}

void StandbyActivity::render(RenderLock&&) {
  if (!currentFace_) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();

  // Face renders into the full screen regardless of mode. Chrome (title, battery,
  // dot indicator) is drawn as overlay on top in Normal mode only, so the face
  // content doesn't re-flow when transitioning to Immersive.
  currentFace_->render(renderer, Rect{0, 0, sw, sh});

  // Interactive faces (airpage) that render full-screen (image view) paint
  // edge-to-edge with no chrome in a single BW pass — not gated on the 5s-idle
  // Immersive transition. Their QR / loading views fall through to the
  // Normal-mode chrome path below.
  if (currentFace_->isInteractive() && currentFace_->rendersFullScreen()) {
    renderer.displayBuffer();
    return;
  }

  if (mode_ != DisplayMode::Normal) {
    if (inverseMode_) renderer.invertScreen();
    renderer.displayBuffer();
    // Opt-in faces (老黄历) get a 4-level gray LUT enhancement layered on the BW
    // image. Only fires in Immersive — Normal-mode navigation needs the
    // ~300-500ms FAST_REFRESH and can't afford the ~2s gray LUT. inverseMode_
    // short-circuits because invertScreen is BW-only and mixes poorly with gray.
    if (currentFace_->wantsGrayscale() && !inverseMode_) applyGrayscalePass(sw, sh);
    return;
  }

  // Top-center face title (or sync state). Small font, no chrome container,
  // no separator line — Apple Standby-style minimal overlay.
  const char* title =
      (syncState_ != SyncState::Idle) ? tr(STR_STANDBY_SYNCING) : I18n::getInstance().get(currentFace_->titleId());
  renderer.drawCenteredText(SMALL_FONT_ID, metrics.topPadding, title, /*black=*/true);

  // Top-right battery icon (no percentage text). Reuses BaseTheme::drawBatteryRight.
  constexpr int kBatW = 16;
  constexpr int kBatH = 12;
  GUI.drawBatteryRight(renderer, Rect{sw - kBatW - metrics.contentSidePadding, metrics.topPadding, kBatW, kBatH},
                       /*showPercentage=*/false);

  const uint8_t availFaces = countAvailableFaces(sw, sh);
  const uint8_t curRank = rankOfAvailableFace(sw, sh, faceIndex_);
  drawFaceDots(renderer, sw, sh, availFaces, curRank);

  // Standby stays on FAST_REFRESH end-to-end — no full/half waveform flashes.
  // We accept some long-term ghosting in exchange for a calm, non-blinking face.
  if (inverseMode_) renderer.invertScreen();
  renderer.displayBuffer();
}

// Two extra renders (LSB then MSB) into scratch buffers, composited with the BW
// backup by the gray LUT waveform. Mirrors EpubReaderActivity.cpp:813-837. The
// caller gates invocation (passive Immersive vs interactive on-demand); this
// just runs the pass unconditionally.
void StandbyActivity::applyGrayscalePass(int sw, int sh) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("STANDBY", "Grayscale pass skipped: storeBwBuffer failed");
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  currentFace_->render(renderer, Rect{0, 0, sw, sh});
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  currentFace_->render(renderer, Rect{0, 0, sw, sh});
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.restoreBwBuffer();
}
