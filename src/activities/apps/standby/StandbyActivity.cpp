#include "StandbyActivity.h"

#include <Arduino.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <algorithm>
#include <string>

#include "../../../util/PaginationDots.h"
#include "../../../util/TimeUtils.h"
#include "../../ActivityResult.h"
#include "../../network/WifiSelectionActivity.h"
#include "CrossPointSettings.h"
#include "NetworkStartup.h"
#ifdef ENABLE_CHINESE_VERSION
#include "ChineseCalendarFace.h"
#endif
#include "SloppyClockFace.h"
#include "StandbyTime.h"
#include "WifiCredentialStore.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr uint32_t kWifiTimeoutMs = 15000u;  // Same as WifiSelectionActivity
constexpr uint32_t kNtpTimeoutMs = 12000u;
constexpr uint32_t kSyncDelayMs = 1500u;

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
// get online for clock sync. After it has been shown (regardless of whether the
// user connected or cancelled), subsequent Standby entries within the same
// session will not re-prompt — the explicit way to retry is Settings → WiFi.
// This module-scope flag persists across activity destroy/recreate.
bool g_promptedForWifiThisSession = false;

// Bottom-center page indicator dots. One dot per face, filled for the current
// face. Drawn only in Normal mode — hidden once we go Immersive. Shares the dot
// style with the Apps menu via drawPaginationDots().
constexpr int kDotBottomInset = 24;  // distance from bottom edge to top of dots

void drawFaceDots(const GfxRenderer& renderer, int sw, int sh, uint8_t total, uint8_t current) {
  drawPaginationDots(renderer, sw, sh - kDotBottomInset, total, current);
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
  if (!TimeUtils::isClockValid() && SETTINGS.clockAutoSync) {
    syncState_ = SyncState::Delayed;
    syncStartMs_ = millis();
  }
  requestUpdate();
}

void StandbyActivity::onExit() {
  switch (syncState_) {
    case SyncState::Idle:
    case SyncState::Delayed:
      break;
    case SyncState::WifiConnecting:
    case SyncState::ClockSyncing:
      WiFi.disconnect(false);
      delay(100);
      WiFi.mode(WIFI_OFF);
      break;
  }
  syncState_ = SyncState::Idle;
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
  if (TimeUtils::isClockValid() || !SETTINGS.clockAutoSync) return;

  // Another activity (e.g. Settings → WiFi) may have already connected the
  // device. Skip our own WiFi.begin in that case and request a clock sync.
  if (WiFi.status() == WL_CONNECTED) {
    NetworkStartup::prepare(renderer);
    LOG_DBG("STANDBY", "WiFi already connected, skipping silent attempt");
    beginClockSync();
    return;
  }

  // Default path: try the saved "last connected" credentials silently.
  if (trySilentWifiConnect()) {
    return;
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
    if (WIFI_STORE.getCredentialCount() == 0) WIFI_STORE.loadFromFile();
    const std::string last = WIFI_STORE.getLastConnectedSsid();
    if (last.empty()) {
      LOG_DBG("STANDBY", "No lastConnectedSsid for silent connect");
      return false;
    }
    const auto cred = WIFI_STORE.findCredential(last);
    if (!cred) {
      LOG_DBG("STANDBY", "lastConnectedSsid '%s' has no saved credential", last.c_str());
      return false;
    }
    ssid = cred->ssid;
    pass = cred->password;
  }

  WiFi.persistent(false);
  NetworkStartup::setMode(renderer, WIFI_STA);
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
  // ActivityManager owns the picker across frames; stack lifetime is insufficient.
  auto activity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, /*autoConnect=*/false);
  if (!activity) {
    LOG_ERR("STANDBY", "OOM: WifiSelectionActivity (%u bytes)", static_cast<unsigned>(sizeof(WifiSelectionActivity)));
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult& result) { onWifiResult(result); });
}

void StandbyActivity::onWifiResult(const ActivityResult& result) {
  if (result.isCancelled) {
    LOG_DBG("STANDBY", "WiFi UI cancelled; clock remains unavailable");
    return;
  }
  if (TimeUtils::isClockValid()) {
    LOG_DBG("STANDBY", "WiFi UI returned with a trustworthy clock");
    finishTimeSync();
    return;
  }

  LOG_DBG("STANDBY", "WiFi UI returned connected; requesting clock sync");
  beginClockSync();
  requestUpdate();  // refresh header back to "Syncing…"
}

void StandbyActivity::beginClockSync() {
  if (!halClock.requestSync()) {
    LOG_ERR("STANDBY", "Clock sync could not start");
    finishTimeSync();
    return;
  }
  syncState_ = SyncState::ClockSyncing;
  syncStartMs_ = millis();
  LOG_DBG("STANDBY", "Clock sync started");
}

void StandbyActivity::pumpTimeSync() {
  if (syncState_ == SyncState::Idle) return;
  const uint32_t elapsed = millis() - syncStartMs_;

  switch (syncState_) {
    case SyncState::Idle:
      return;
    case SyncState::Delayed:
      if (elapsed < kSyncDelayMs) return;
      syncState_ = SyncState::Idle;
      startTimeSync();
      return;
    case SyncState::WifiConnecting: {
      const wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED) {
        beginClockSync();
        return;
      }
      if (status != WL_CONNECT_FAILED && status != WL_NO_SSID_AVAIL && elapsed < kWifiTimeoutMs) return;

      LOG_DBG("STANDBY", "Silent WiFi sync failed (status=%d, t=%ums)", static_cast<int>(status),
              static_cast<unsigned>(elapsed));
      WiFi.disconnect(false);
      delay(100);
      WiFi.mode(WIFI_OFF);
      syncState_ = SyncState::Idle;
      if (!g_promptedForWifiThisSession)
        promptForWifi();
      else
        requestUpdate();
      return;
    }
    case SyncState::ClockSyncing:
      switch (halClock.syncState()) {
        case ClockSyncState::Succeeded:
          g_promptedForWifiThisSession = false;
          LOG_DBG("STANDBY", "Clock synchronized");
          finishTimeSync();
          return;
        case ClockSyncState::Failed:
          LOG_DBG("STANDBY", "Clock sync failed");
          finishTimeSync();
          return;
        case ClockSyncState::Idle:
          if (elapsed < kNtpTimeoutMs) return;
          LOG_DBG("STANDBY", "Clock sync stopped");
          finishTimeSync();
          return;
        case ClockSyncState::Syncing:
          if (elapsed < kNtpTimeoutMs) return;
          LOG_DBG("STANDBY", "Clock sync timed out");
          finishTimeSync();
          return;
      }
      break;
  }
}

void StandbyActivity::finishTimeSync() {
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  // Fully release the WiFi driver so HalPowerManager::setPowerSaving() can
  // actually drop the CPU to LOW_POWER_FREQ — it short-circuits while WiFi is
  // active (see HalPowerManager.cpp). Return value is ignored;
  // ESP_ERR_WIFI_NOT_INIT is fine if we never connected.
  esp_wifi_deinit();
  syncState_ = SyncState::Idle;
  requestUpdate();
}

void StandbyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goHome();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::None) {
    lastInputMs_ = millis();
    if (mode_ == DisplayMode::Immersive) {
      mode_ = DisplayMode::Normal;
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Left) {
      switchFace(1);
    } else if (swipe == MappedInputManager::SwipeDir::Right) {
      switchFace(-1);
    } else if (currentFace_) {
      if (swipe == MappedInputManager::SwipeDir::Up) {
        currentFace_->onPageNext();
      } else {
        currentFace_->onPagePrev();
      }
      requestUpdate();
    }
    return;
  }

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    lastInputMs_ = millis();
    if (mode_ == DisplayMode::Immersive) {
      mode_ = DisplayMode::Normal;
    } else {
      inverseMode_ = !inverseMode_;
    }
    requestUpdate();
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

  // Confirm toggles inverse (black background / white content) regardless of
  // Normal vs. Immersive.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    lastInputMs_ = millis();
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
  const uint32_t idle = millis() - lastInputMs_;
  if (mode_ == DisplayMode::Normal && idle >= 5000u) {
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
  int viewTop = 0;
  int viewRight = 0;
  int viewBottom = 0;
  int viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  const Rect faceViewport{viewLeft, viewTop, sw - viewLeft - viewRight, sh - viewTop - viewBottom};
  const int overlayTop = std::max(metrics.topPadding, viewTop);

  renderer.clearScreen();

  // Face renders inside the physically viewable screen regardless of mode. Normal
  // chrome is overlaid without reflow; the unsynced warning remains in Immersive.
  {
    const GfxRenderer::ClipScope clip(renderer, faceViewport.x, faceViewport.y, faceViewport.width,
                                      faceViewport.height);
    currentFace_->render(renderer, faceViewport);
  }

  const bool clockValid = TimeUtils::isClockValid();
  if (!clockValid) {
    UITheme::drawCenteredText(renderer, faceViewport, SMALL_FONT_ID, overlayTop, tr(STR_STANDBY_SYNCING));
  }

  if (mode_ != DisplayMode::Normal) {
    if (inverseMode_) renderer.invertScreen();
    renderer.displayBuffer();
    // Opt-in faces (老黄历) get a 4-level gray LUT enhancement layered on the BW
    // image. Only fires in Immersive — Normal-mode navigation needs the
    // ~300-500ms FAST_REFRESH and can't afford the ~2s gray LUT. inverseMode_
    // short-circuits because invertScreen is BW-only and mixes poorly with gray.
    if (currentFace_->wantsGrayscale() && !inverseMode_) applyGrayscalePass(faceViewport);
    return;
  }

  // Top-center face title (or sync state). Small font, no chrome container,
  // no separator line — Apple Standby-style minimal overlay.
  if (clockValid) {
    UITheme::drawCenteredText(renderer, faceViewport, SMALL_FONT_ID, overlayTop,
                              I18n::getInstance().get(currentFace_->titleId()));
  }

  // Top-right battery icon (no percentage text). Reuses BaseTheme::drawBatteryRight.
  constexpr int kBatW = 16;
  constexpr int kBatH = 12;
  GUI.drawBatteryRight(renderer, Rect{sw - viewRight - kBatW - metrics.contentSidePadding, overlayTop, kBatW, kBatH},
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
void StandbyActivity::applyGrayscalePass(const Rect& viewport) {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("STANDBY", "Grayscale pass skipped: storeBwBuffer failed");
    return;
  }

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  {
    const GfxRenderer::ClipScope clip(renderer, viewport.x, viewport.y, viewport.width, viewport.height);
    currentFace_->render(renderer, viewport);
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  {
    const GfxRenderer::ClipScope clip(renderer, viewport.x, viewport.y, viewport.width, viewport.height);
    currentFace_->render(renderer, viewport);
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.restoreBwBuffer();
}
