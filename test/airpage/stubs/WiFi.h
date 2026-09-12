#pragma once

#include <string>

enum wl_status_t {
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_DISCONNECTED = 6,
};

using wifi_mode_t = int;
constexpr wifi_mode_t WIFI_OFF = 0;
constexpr wifi_mode_t WIFI_STA = 1;

class WiFiClient {};

class WiFiClass {
 public:
  wl_status_t status() const { return statusValue; }
  bool persistent(bool) { return true; }
  bool disconnect(bool = false, bool = false) {
    ++disconnectCalls;
    statusValue = WL_DISCONNECTED;
    return true;
  }
  wl_status_t begin(const char* ssid) { return begin(ssid, ""); }
  wl_status_t begin(const char* ssid, const char* password) {
    ++beginCalls;
    lastSsid = ssid;
    lastPassword = password;
    statusValue = WL_IDLE_STATUS;
    return statusValue;
  }
  bool mode(const wifi_mode_t mode) {
    modeValue = mode;
    return true;
  }
  void reset() {
    statusValue = WL_DISCONNECTED;
    modeValue = WIFI_OFF;
    beginCalls = 0;
    disconnectCalls = 0;
    lastSsid.clear();
    lastPassword.clear();
  }

  wl_status_t statusValue = WL_DISCONNECTED;
  wifi_mode_t modeValue = WIFI_OFF;
  int beginCalls = 0;
  int disconnectCalls = 0;
  std::string lastSsid;
  std::string lastPassword;
};

inline WiFiClass WiFi;
