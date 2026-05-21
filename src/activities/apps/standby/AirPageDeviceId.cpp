#include "AirPageDeviceId.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_random.h>

#include <cstdint>
#include <string>
#include <utility>

namespace airpage {

namespace {

// nanoid alphabet: uppercase + lowercase letters + digits (62 chars). No '_'/'-'
// so the id is purely alphanumeric.
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr int kAlphabetLen = 62;
constexpr int kIdLen = 16;

// Persisted on the SD card, independent of any user setting. Survives reboots and
// cache clears (ClearCacheActivity only removes epub_*/xtc_* dirs). Deleting this
// file (or swapping/formatting the SD card) forces a fresh id on next boot.
constexpr char kIdDir[] = "/.crosspoint";
constexpr char kIdPath[] = "/.crosspoint/airpage_device_id";

// Live-push mode flag. Shares the AirPage cache dir (created lazily on first
// image fetch); we ensure it here too so the toggle works before any fetch.
constexpr char kModeDir[] = "/.crosspoint/airpage";
constexpr char kModePath[] = "/.crosspoint/airpage/mode";

// Generate a random id with esp_random(). Mask-and-reject (nanoid style): take the
// low 6 bits (0..63) and discard 62/63, so every accepted value is uniform over
// 0..61 with no modulo bias.
std::string generateId() {
  std::string id;
  id.reserve(kIdLen);
  while (static_cast<int>(id.size()) < kIdLen) {
    const uint8_t v = esp_random() & 0x3F;  // 0..63
    if (v < kAlphabetLen) id.push_back(kAlphabet[v]);
  }
  return id;
}

// Guard against reading a truncated/corrupt file: exact length and alphanumeric-only.
bool isValidId(const std::string& s) {
  if (static_cast<int>(s.size()) != kIdLen) return false;
  for (const char c : s) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (!ok) return false;
  }
  return true;
}

}  // namespace

const std::string& deviceId() {
  // Single lazy init — no thread-safety concern on single-core ESP32-C3.
  static std::string cached;
  if (!cached.empty()) return cached;

  // 1) Reuse the persisted id if present and well-formed.
  std::string stored(Storage.readFile(kIdPath).c_str());  // empty on missing/failure
  if (isValidId(stored)) {
    cached = std::move(stored);
    LOG_DBG("AIRP", "deviceId loaded: %s", cached.c_str());
    return cached;
  }

  // 2) First use (or missing/corrupt file) — generate a fresh random id.
  cached = generateId();

  // 3) Persist it. Write failure (e.g. SD not ready) is non-fatal: we still return
  // the in-memory id this session and retry generation+persist on next boot.
  Storage.ensureDirectoryExists(kIdDir);
  if (Storage.writeFile(kIdPath, String(cached.c_str()))) {
    LOG_INF("AIRP", "deviceId generated+persisted: %s", cached.c_str());
  } else {
    LOG_ERR("AIRP", "deviceId persist failed (SD?), will retry next boot: %s", cached.c_str());
  }
  return cached;
}

bool loadRealtimeMode() {
  // Default ON: a missing/unreadable file means live push is enabled (fresh
  // devices boot into live mode). Only the explicit byte '0' selects manual.
  const std::string v(Storage.readFile(kModePath).c_str());
  return v.empty() || v[0] != '0';
}

void saveRealtimeMode(bool enabled) {
  Storage.ensureDirectoryExists(kModeDir);
  if (!Storage.writeFile(kModePath, String(enabled ? "1" : "0"))) {
    LOG_ERR("AIRP", "realtime mode persist failed (SD?)");
  }
}

}  // namespace airpage
