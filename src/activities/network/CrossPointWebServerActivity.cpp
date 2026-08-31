#include "CrossPointWebServerActivity.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstddef>

#include "MappedInputManager.h"
#include "NetworkModeSelectionActivity.h"
#include "NetworkStartup.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "activities/network/CalibreConnectActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"
#include "util/TaskWatchdog.h"

namespace {
// AP Mode configuration
constexpr const char* AP_SSID = "CrossPoint-Reader";
constexpr const char* AP_PASSWORD = nullptr;  // Open network for ease of use
constexpr const char* AP_HOSTNAME = "crosspoint";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CONNECTIONS = 4;
constexpr int QR_CODE_WIDTH = 198;
constexpr int QR_CODE_HEIGHT = 198;

// DNS server for captive portal (redirects all DNS queries to our IP)
DNSServer* dnsServer = nullptr;
constexpr uint16_t DNS_PORT = 53;

void stopDnsServer() {
  if (!dnsServer) return;

  dnsServer->stop();
  delete dnsServer;
  dnsServer = nullptr;
}

void restartMdns(const char* hostname, const char* tag) {
  MDNS.end();
  if (MDNS.begin(hostname)) {
    LOG_DBG(tag, "mDNS started: http://%s.local/", hostname);
  } else {
    LOG_DBG(tag, "WARNING: mDNS failed to start");
  }
}

// 0..4 bars from RSSI (dBm), with 3 dBm hysteresis on currentBars to suppress flicker.
int barsForRssi(int rssi, int currentBars) {
  static constexpr int RISE_DBM[] = {-85, -75, -65, -55};
  static constexpr int FALL_DBM[] = {-88, -78, -68, -58};
  int bars = std::clamp(currentBars, 0, 4);
  while (bars < 4 && rssi >= RISE_DBM[bars]) bars++;
  while (bars > 0 && rssi < FALL_DBM[bars - 1]) bars--;
  return bars;
}
}  // namespace

void CrossPointWebServerActivity::onEnter() {
  Activity::onEnter();

  LOG_DBG("WEBACT", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  // Heap-critical transition: WiFi (~45KB) plus the web server have to fit in
  // what's left of the ~380KB parts. SD-font caches retained for the CJK UI
  // fallback (mini glyph/kern arenas, kern class tables) are rebuildable on
  // demand — release them up front instead of aborting in startWebServer()
  // when the heap comes up short (observed on X3 with a Korean SD font).
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
    LOG_DBG("WEBACT", "Free heap after SD font cache release: %d bytes", ESP.getFreeHeap());
  }

  // Reset state
  state = WebServerActivityState::MODE_SELECTION;
  networkMode = NetworkMode::JOIN_NETWORK;
  isApMode = false;
  connectedIP.clear();
  connectedSSID.clear();
  lastHandleClientTime = 0;
  requestUpdate();

  LOG_DBG("WEBACT", "Launching NetworkModeSelectionActivity...");
  startActivityForResultWith<NetworkModeSelectionActivity>([this](const ActivityResult& result) {
    if (result.isCancelled) {
      onGoHome();
    } else {
      onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
    }
  });
}

void CrossPointWebServerActivity::onExit() {
  Activity::onExit();

  LOG_DBG("WEBACT", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());

  state = WebServerActivityState::SHUTTING_DOWN;
  stopDnsServer();
  MDNS.end();

  // Skip reboot if WiFi was never activated (e.g. user backed out of mode selection).
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    if (isApMode) {
      WiFi.softAPdisconnect(true);
    } else {
      WiFi.disconnect(false);
    }
    delay(30);
    silentRestart();
  }

  LOG_DBG("WEBACT", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServerActivity::onNetworkModeSelected(const NetworkMode mode) {
  const char* modeName = "Join Network";
  if (mode == NetworkMode::CONNECT_CALIBRE) {
    modeName = "Connect to Calibre";
  } else if (mode == NetworkMode::CREATE_HOTSPOT) {
    modeName = "Create Hotspot";
#if FREEINK_CAP_USB_MSC
  } else if (mode == NetworkMode::USB_DRIVE) {
    modeName = "USB Drive";
#endif
  }
  LOG_DBG("WEBACT", "Network mode selected: %s", modeName);

#if FREEINK_CAP_USB_MSC
  if (mode == NetworkMode::USB_DRIVE) {
    activityManager.goToUsbDrive();
    return;
  }
#endif

  networkMode = mode;
  isApMode = (mode == NetworkMode::CREATE_HOTSPOT);

  if (mode == NetworkMode::CONNECT_CALIBRE) {
    startActivityForResultWith<CalibreConnectActivity>([this](const ActivityResult& result) {
      state = WebServerActivityState::MODE_SELECTION;

      startActivityForResultWith<NetworkModeSelectionActivity>([this](const ActivityResult& result) {
        if (result.isCancelled) {
          onGoHome();
        } else {
          onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
        }
      });
    });
    return;
  }

  if (mode == NetworkMode::JOIN_NETWORK) {
    // STA mode - launch WiFi selection
    state = WebServerActivityState::WIFI_SELECTION;
    LOG_DBG("WEBACT", "Launching WifiSelectionActivity...");
    if (!startActivityForResultWith<WifiSelectionActivity>([this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto& wifi = std::get<WifiResult>(result.data);
            connectedIP = wifi.ip;
            connectedSSID = wifi.ssid;
          }
          onWifiSelectionComplete(!result.isCancelled);
        })) {
      onGoHome();
    }
  } else {
    // AP mode - start access point
    state = WebServerActivityState::AP_STARTING;
    requestUpdate();
    startAccessPoint();
  }
}

void CrossPointWebServerActivity::onWifiSelectionComplete(const bool connected) {
  LOG_DBG("WEBACT", "WifiSelectionActivity completed, connected=%d", connected);

  if (connected) {
    // Get connection info before exiting subactivity
    isApMode = false;

    // Start mDNS for hostname resolution
    restartMdns(AP_HOSTNAME, "WEBACT");

    // Start the web server
    startWebServer();
  } else {
    // User cancelled - go back to mode selection
    state = WebServerActivityState::MODE_SELECTION;

    startActivityForResultWith<NetworkModeSelectionActivity>([this](const ActivityResult& result) {
      if (result.isCancelled) {
        onGoHome();
      } else {
        onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
      }
    });
  }
}

void CrossPointWebServerActivity::startAccessPoint() {
  LOG_DBG("WEBACT", "Starting Access Point mode...");
  LOG_DBG("WEBACT", "Free heap before AP start: %d bytes", ESP.getFreeHeap());

  // Configure and start the AP
  NetworkStartup::setMode(renderer, WIFI_AP);
  delay(100);

  // Start soft AP
  bool apStarted;
  if (AP_PASSWORD && strlen(AP_PASSWORD) >= 8) {
    apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  } else {
    // Open network (no password)
    apStarted = WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
  }

  if (!apStarted) {
    LOG_ERR("WEBACT", "ERROR: Failed to start Access Point!");
    onGoHome();
    return;
  }

  delay(100);  // Wait for AP to fully initialize

  // Get AP IP address
  const IPAddress apIP = WiFi.softAPIP();
  char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
  connectedIP = ipStr;
  connectedSSID = AP_SSID;

  LOG_DBG("WEBACT", "Access Point started!");
  LOG_DBG("WEBACT", "SSID: %s", AP_SSID);
  LOG_DBG("WEBACT", "IP: %s", connectedIP.c_str());

  // Start mDNS for hostname resolution
  restartMdns(AP_HOSTNAME, "WEBACT");

  // Start DNS server for captive portal behavior
  // This redirects all DNS queries to our IP, making any domain typed resolve to us
  stopDnsServer();
  dnsServer = new (std::nothrow) DNSServer();
  if (!dnsServer) {
    LOG_ERR("WEBACT", "OOM: DNSServer (%u bytes)", static_cast<unsigned>(sizeof(DNSServer)));
    onGoHome();
    return;
  }
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", apIP);
  LOG_DBG("WEBACT", "DNS server started for captive portal");

  LOG_DBG("WEBACT", "Free heap after AP start: %d bytes", ESP.getFreeHeap());

  // Start the web server
  startWebServer();
}

void CrossPointWebServerActivity::startWebServer() {
  LOG_DBG("WEBACT", "Starting web server...");

  // Repeat the release right before the allocation: the WiFi selection screen
  // rendered since onEnter(), and a CJK SSID repopulates the SD-font caches.
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("WEBACT", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("WEBACT", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
  }

  // Create the web server instance
  webServer = makeUniqueNoThrow<CrossPointWebServer>();
  if (!webServer) {
    LOG_ERR("WEBACT", "OOM: CrossPointWebServer (%u bytes)", static_cast<unsigned>(sizeof(CrossPointWebServer)));
    onGoHome();
    return;
  }
  webServer->begin();

  if (webServer->isRunning()) {
    state = WebServerActivityState::SERVER_RUNNING;
    LOG_DBG("WEBACT", "Web server started successfully");
    lastWifiBars = isApMode ? 0 : barsForRssi(WiFi.RSSI(), 0);

    // Force an immediate render since we're transitioning from a subactivity
    // that had its own rendering task. We need to make sure our display is shown.
    requestUpdate();
  } else {
    LOG_ERR("WEBACT", "ERROR: Failed to start web server!");
    webServer.reset();
    // Go back on error
    onGoHome();
  }
}

void CrossPointWebServerActivity::loop() {
  // Handle different states
  if (state == WebServerActivityState::SERVER_RUNNING) {
    // Handle DNS requests for captive portal (AP mode only)
    if (isApMode && dnsServer) {
      dnsServer->processNextRequest();
    }

    // STA mode: Monitor WiFi connection health
    if (!isApMode && webServer && webServer->isRunning()) {
      static unsigned long lastWifiCheck = 0;
      if (millis() - lastWifiCheck > 2000) {  // Check every 2 seconds
        lastWifiCheck = millis();
        const wl_status_t wifiStatus = WiFi.status();
        // Driver auto-reconnect handles retries; abandon (via onGoHome) only
        // after WIFI_ABANDON_MS, otherwise the activity freezes on a blip.
        bool repaint = false;
        if (wifiStatus != WL_CONNECTED) {
          if (consecutiveDisconnects == 0) {
            firstDisconnectAt = millis();
            repaint = true;
          }
          consecutiveDisconnects++;
          LOG_DBG("WEBACT", "WiFi not connected (status=%d, consecutive=%d, total=%lu ms)", wifiStatus,
                  consecutiveDisconnects, millis() - firstDisconnectAt);
          if (millis() - firstDisconnectAt > WIFI_ABANDON_MS) {
            LOG_DBG("WEBACT", "WiFi unavailable for >%lu s; returning to network selection", WIFI_ABANDON_MS / 1000UL);
            state = WebServerActivityState::SHUTTING_DOWN;
            onGoHome();
            return;
          }
        } else {
          if (consecutiveDisconnects > 0) {
            LOG_DBG("WEBACT", "WiFi recovered after %d failed checks (%lu ms)", consecutiveDisconnects,
                    millis() - firstDisconnectAt);
            repaint = true;
          }
          consecutiveDisconnects = 0;
          firstDisconnectAt = 0;
          const int rssi = WiFi.RSSI();
          if (rssi < -75) {
            LOG_DBG("WEBACT", "Warning: Weak WiFi signal: %d dBm", rssi);
          }
          const int bars = barsForRssi(rssi, lastWifiBars);
          if (bars != lastWifiBars) {
            lastWifiBars = bars;
            repaint = true;
          }
        }
        if (repaint) requestUpdate();
      }
    }

    // Handle web server requests - maximize throughput with watchdog safety
    if (webServer && webServer->isRunning()) {
      const unsigned long timeSinceLastHandleClient = millis() - lastHandleClientTime;

      // Log if there's a significant gap between handleClient calls (>100ms)
      if (lastHandleClientTime > 0 && timeSinceLastHandleClient > 100) {
        LOG_DBG("WEBACT", "WARNING: %lu ms gap since last handleClient", timeSinceLastHandleClient);
      }

      // Reset watchdog BEFORE processing - HTTP header parsing can be slow
      resetTaskWatchdogIfSubscribed();

      // Process HTTP requests in tight loop for maximum throughput
      // More iterations = more data processed per main loop cycle
      constexpr int MAX_ITERATIONS = 500;
      for (int i = 0; i < MAX_ITERATIONS && webServer->isRunning(); i++) {
        webServer->handleClient();
        // Reset watchdog every 32 iterations
        if ((i & 0x1F) == 0x1F) {
          resetTaskWatchdogIfSubscribed();
        }
        // Yield and check for exit button every 64 iterations
        if ((i & 0x3F) == 0x3F) {
          yield();
          // Pump input inside this blocking loop so exit events remain responsive.
          mappedInput.update();
          // This update consumes the one-shot Home event before ActivityManager
          // can see it, so handle Home here alongside Back.
          if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
            onGoHome();
            return;
          }
        }
      }
      lastHandleClientTime = millis();
    }

    // Also check outside the request-processing loop.
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
      onGoHome();
      return;
    }
  }
}

const char* CrossPointWebServerActivity::headerTitle() const {
  if (isApMode) return tr(STR_HOTSPOT_MODE);
  return tr(STR_FILE_TRANSFER);
}

void CrossPointWebServerActivity::render(RenderLock&&) {
  // Only render our own UI when server is running
  // Subactivities handle their own rendering
  if (state == WebServerActivityState::SERVER_RUNNING || state == WebServerActivityState::AP_STARTING) {
    renderer.clearScreen();

    if (state == WebServerActivityState::SERVER_RUNNING) {
      renderServerRunning();
    } else {
      const auto& metrics = UITheme::getInstance().getMetrics();
      const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
      GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                     headerTitle(), nullptr);
      const Rect content = SubpageLayout::contentRect(safeArea, metrics);
      const Rect text = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
      const int height = renderer.getLineHeight(UI_12_FONT_ID);
      UITheme::drawCenteredText(renderer, text, UI_12_FONT_ID, SubpageLayout::centeredTop(content, height),
                                tr(STR_STARTING_HOTSPOT));
    }
    renderer.displayBuffer();
  }
}

void CrossPointWebServerActivity::renderServerRunning() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int pageWidth = safeArea.width;

  constexpr const char* urlSuffix = "/";
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 headerTitle(), nullptr);
  GUI.drawSubHeader(
      renderer,
      Rect{safeArea.x, safeArea.y + metrics.topPadding + metrics.headerHeight, safeArea.width, metrics.tabBarHeight},
      connectedSSID.c_str());

  if (!isApMode) {
    renderWifiIndicator(safeArea.y + metrics.topPadding + metrics.headerHeight);
  }

  const Rect body = SubpageLayout::contentRect(safeArea, metrics, true);
  const int relatedGap = SubpageLayout::relatedGap(metrics);
  const int sectionGap = SubpageLayout::sectionGap(metrics);
  int startY = body.y + sectionGap;
  const int height10 = renderer.getLineHeight(UI_10_FONT_ID);
  if (isApMode) {
    // AP mode display
    renderer.drawText(UI_10_FONT_ID, body.x + metrics.contentSidePadding, startY, tr(STR_CONNECT_WIFI_HINT), true,
                      EpdFontFamily::BOLD);
    startY += height10 + sectionGap;

    // Show QR code for Wifi
    // follows spec at https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11
    const std::string wifiConfig = std::string("WIFI:T:nopass;S:") + connectedSSID + ";;";
    const Rect qrBoundsWifi(body.x + metrics.contentSidePadding, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBoundsWifi, wifiConfig);

    // Show network name
    const int sideTextX = body.x + metrics.contentSidePadding + QR_CODE_WIDTH + sectionGap;
    const int sideTextWidth = std::max(1, body.x + body.width - metrics.contentSidePadding - sideTextX);
    const std::string shownSsid = renderer.truncatedText(UI_10_FONT_ID, connectedSSID.c_str(), sideTextWidth);
    renderer.drawText(UI_10_FONT_ID, sideTextX, startY + 80, shownSsid.c_str());

    startY += QR_CODE_HEIGHT + sectionGap;

    // Show primary URL (hostname)
    renderer.drawText(UI_10_FONT_ID, body.x + metrics.contentSidePadding, startY, tr(STR_OPEN_URL_HINT), true,
                      EpdFontFamily::BOLD);
    startY += height10 + sectionGap;

    std::string hostnameUrl = std::string("http://") + AP_HOSTNAME + ".local" + urlSuffix;
    std::string ipUrl = tr(STR_OR_HTTP_PREFIX) + connectedIP + urlSuffix;

    // Show QR code for URL
    const Rect qrBoundsUrl(body.x + metrics.contentSidePadding, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBoundsUrl, hostnameUrl);

    // Show IP address as fallback
    const std::string shownHost = renderer.truncatedText(UI_10_FONT_ID, hostnameUrl.c_str(), sideTextWidth);
    const std::string shownIp = renderer.truncatedText(SMALL_FONT_ID, ipUrl.c_str(), sideTextWidth);
    renderer.drawText(UI_10_FONT_ID, sideTextX, startY + 80, shownHost.c_str());
    renderer.drawText(SMALL_FONT_ID, sideTextX, startY + 80 + height10 + relatedGap, shownIp.c_str());
  } else {
    // STA mode display (original behavior)
    // std::string ipInfo = "IP Address: " + connectedIP;
    renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_OPEN_URL_HINT), true, EpdFontFamily::BOLD);
    startY += height10;
    renderer.drawCenteredText(UI_10_FONT_ID, startY, tr(STR_SCAN_QR_HINT), true, EpdFontFamily::BOLD);
    startY += height10 + sectionGap;

    // Show QR code for URL
    std::string webInfo = std::string("http://") + connectedIP + urlSuffix;
    const Rect qrBounds(safeArea.x + (pageWidth - QR_CODE_WIDTH) / 2, startY, QR_CODE_WIDTH, QR_CODE_HEIGHT);
    QrUtils::drawQrCode(renderer, qrBounds, webInfo);
    startY += QR_CODE_HEIGHT + sectionGap;

    // Show web server URL prominently
    renderer.drawCenteredText(UI_10_FONT_ID, startY, webInfo.c_str(), true);
    startY += height10 + relatedGap;

    // Also show hostname URL
    std::string hostnameUrl = std::string(tr(STR_OR_HTTP_PREFIX)) + AP_HOSTNAME + ".local" + urlSuffix;
    renderer.drawCenteredText(SMALL_FONT_ID, startY, hostnameUrl.c_str(), true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CrossPointWebServerActivity::renderWifiIndicator(int subHeaderTop) const {
  constexpr int BAR_COUNT = 4;
  constexpr int BAR_WIDTH = 4;
  constexpr int BAR_GAP = 2;
  constexpr int ICON_HEIGHT = 14;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int iconWidth = BAR_COUNT * BAR_WIDTH + (BAR_COUNT - 1) * BAR_GAP;
  const int iconRight = safeArea.x + safeArea.width - metrics.contentSidePadding;
  const int iconLeft = iconRight - iconWidth;
  const int iconBottom = subHeaderTop + metrics.tabBarHeight - SubpageLayout::relatedGap(metrics);

  const bool wifiUp = (WiFi.status() == WL_CONNECTED) && (consecutiveDisconnects == 0);
  if (wifiUp) {
    for (int i = 0; i < BAR_COUNT; i++) {
      const int barHeight = (i + 1) * ICON_HEIGHT / BAR_COUNT;
      const int x = iconLeft + i * (BAR_WIDTH + BAR_GAP);
      const int y = iconBottom - barHeight;
      if (i < lastWifiBars) {
        renderer.fillRect(x, y, BAR_WIDTH, barHeight, true);
      } else {
        renderer.drawRect(x, y, BAR_WIDTH, barHeight, true);
      }
    }
  } else {
    const int xSize = ICON_HEIGHT;
    const int x0 = iconRight - xSize;
    const int y0 = iconBottom - xSize;
    renderer.drawLine(x0, y0, x0 + xSize, y0 + xSize, 2, true);
    renderer.drawLine(x0, y0 + xSize, x0 + xSize, y0, 2, true);
  }
}
