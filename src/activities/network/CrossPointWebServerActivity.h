#pragma once

#include <functional>
#include <memory>
#include <string>

#include "NetworkModeSelectionActivity.h"
#include "activities/Activity.h"
#include "network/CrossPointWebServer.h"

// Web server activity states
enum class WebServerActivityState {
  MODE_SELECTION,  // Showing NetworkModeSelectionActivity (Join / Calibre / Hotspot)
  WIFI_SELECTION,  // WifiSelectionActivity is active (for Join Network / WeRead paths)
  AP_STARTING,     // Starting Access Point mode
  SERVER_RUNNING,  // Web server is running and handling requests
  SHUTTING_DOWN    // Shutting down server and WiFi
};

// Entry intent — drives which onboarding path the activity takes:
//
//   FileTransfer    Default. Opens NetworkModeSelectionActivity so the user
//                   picks Join Network / Calibre / Create Hotspot, then a
//                   general-purpose web server backs file management.
//
//   WeReadKeySetup  Single-purpose path for binding a WeRead API key via the
//                   user's phone. Skips the mode chooser, only runs STA on a
//                   saved network, and the device screen / QR point at the
//                   /weread paste form instead of the file-transfer root.
enum class WebServerEntryPurpose {
  FileTransfer,
  WeReadKeySetup,
};

/**
 * CrossPointWebServerActivity owns the device-side flow for bringing up the
 * built-in web server. Two entry purposes share this activity (see
 * WebServerEntryPurpose) — the file-transfer path runs the full network mode
 * chooser, while the WeRead-key path skips straight to STA + the /weread page.
 *
 * Shared lifecycle:
 *  - Connect WiFi (STA or AP), with mDNS for crosspoint.local resolution.
 *  - Construct CrossPointWebServer and run handleClient() from loop().
 *  - On Back, shut down WiFi and reboot via silentRestart() to release the
 *    networking stack cleanly.
 */
class CrossPointWebServerActivity final : public Activity {
  WebServerActivityState state = WebServerActivityState::MODE_SELECTION;

  // Entry intent (see WebServerEntryPurpose). Set in the constructor.
  WebServerEntryPurpose purpose = WebServerEntryPurpose::FileTransfer;

  // Network mode
  NetworkMode networkMode = NetworkMode::JOIN_NETWORK;
  bool isApMode = false;

  // Web server - owned by this activity
  std::unique_ptr<CrossPointWebServer> webServer;

  // Server status
  std::string connectedIP;
  std::string connectedSSID;  // For STA mode: network name, For AP mode: AP name

  // Performance monitoring
  unsigned long lastHandleClientTime = 0;

  // Sustained WiFi-loss tracking; abandon only after WIFI_ABANDON_MS.
  int consecutiveDisconnects = 0;
  unsigned long firstDisconnectAt = 0;
  static constexpr unsigned long WIFI_ABANDON_MS = 5UL * 60UL * 1000UL;

  // Cached signal-strength bracket (0..4) for the WiFi indicator.
  int lastWifiBars = 0;

  void renderServerRunning() const;
  void renderWifiIndicator(int subHeaderTop) const;
  const char* headerTitle() const;

  void onNetworkModeSelected(NetworkMode mode);
  void onWifiSelectionComplete(bool connected);
  void startAccessPoint();
  void startWebServer();

 public:
  explicit CrossPointWebServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       WebServerEntryPurpose purpose = WebServerEntryPurpose::FileTransfer)
      : Activity("CrossPointWebServer", renderer, mappedInput), purpose(purpose) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};
