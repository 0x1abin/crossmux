#pragma once

inline int espWifiDeinitCalls = 0;

inline int esp_wifi_deinit() {
  ++espWifiDeinitCalls;
  return 0;
}
