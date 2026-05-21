#pragma once
// Header-only stub for WiFi-aware code on host. The simulator reports a permanently
// "connected" state so HTTPClient (libcurl-backed) can pass preflight checks like
// `WiFi.status() == WL_CONNECTED`. The host's real network stack does the actual I/O.

#include <Print.h>
#include <WString.h>

#include <cstdint>

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t, uint8_t, uint8_t, uint8_t) {}
  operator uint32_t() const { return 0; }
  String toString() const { return String("127.0.0.1"); }
};

enum WiFiMode { WIFI_OFF = 0, WIFI_STA, WIFI_AP, WIFI_AP_STA };
// ESP-IDF wifi_mode_t values, as returned by WiFi.getMode(). WIFI_MODE_NULL means the
// radio is off; the simulator has no radio, so getMode() always reports it (the deep-sleep
// WiFi-teardown path in main.cpp then short-circuits — nothing to tear down on host).
enum wifi_mode_t { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA };
enum WiFiStatus { WL_NO_SHIELD = 0, WL_IDLE_STATUS, WL_NO_SSID_AVAIL, WL_SCAN_COMPLETED, WL_CONNECTED,
                  WL_CONNECT_FAILED, WL_CONNECTION_LOST, WL_DISCONNECTED };

class WiFiClass {
 public:
  void mode(int) {}
  wifi_mode_t getMode() { return WIFI_MODE_NULL; }
  int begin(const char* = nullptr, const char* = nullptr) { return WL_CONNECTED; }
  int status() { return WL_CONNECTED; }
  void disconnect(bool = false) {}
  void disconnect(bool, bool) {}
  void persistent(bool) {}
  IPAddress localIP() { return IPAddress(127, 0, 0, 1); }
  String macAddress() { return String("02:00:00:00:00:01"); }
  String SSID() { return String("SimulatedWiFi"); }
  int RSSI() { return -50; }
  int scanNetworks() { return 1; }
  String SSID(int) { return String("SimulatedWiFi"); }
  int encryptionType(int) { return 0; }
  int RSSI(int) { return -50; }
};

extern WiFiClass WiFi;

using wl_status_t = int;
