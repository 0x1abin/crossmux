#pragma once
// Host stub for ESP-IDF's WiFi C API. The simulator has no real WiFi; this
// header exists only for linkage of code (e.g. StandbyActivity) that calls
// esp_wifi_deinit() defensively before light sleep on real hardware.

inline int esp_wifi_deinit() { return 0; }
