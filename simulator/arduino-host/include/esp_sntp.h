#pragma once
// Host stub for ESP-IDF SNTP. The host clock is already valid, so these calls
// only need to let the shared firmware sources compile and link.

#include <cstdint>

typedef enum {
  SNTP_SYNC_STATUS_RESET = 0,
  SNTP_SYNC_STATUS_COMPLETED,
  SNTP_SYNC_STATUS_IN_PROGRESS,
} sntp_sync_status_t;

typedef enum {
  ESP_SNTP_OPMODE_POLL = 0,
  ESP_SNTP_OPMODE_LISTENONLY,
} esp_sntp_operatingmode_t;

inline bool esp_sntp_enabled() { return false; }
inline void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t) {}
inline void esp_sntp_setservername(uint8_t, const char*) {}
inline void esp_sntp_init() {}
inline void esp_sntp_stop() {}
inline sntp_sync_status_t sntp_get_sync_status() { return SNTP_SYNC_STATUS_RESET; }
inline void configTime(long, int, const char*, const char* = nullptr, const char* = nullptr) {}
