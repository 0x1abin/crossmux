#pragma once
// Simulator shim for esp_mac.h. The real firmware reads the per-device eFuse
// MAC; the host has none, so we return a fixed deterministic MAC. This keeps
// AirPageDeviceId::deviceId() stable across simulator runs (matching the WiFi
// shim's macAddress() of 02:00:00:00:00:01).
#include <cstdint>
#include <cstring>

inline int esp_efuse_mac_get_default(uint8_t* mac) {
  static const uint8_t kFakeMac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
  if (mac) memcpy(mac, kFakeMac, sizeof(kFakeMac));
  return 0;
}
