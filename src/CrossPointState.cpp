#include "CrossPointState.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr uint8_t STATE_FILE_VERSION = 4;
constexpr char STATE_FILE_BIN[] = "/.crosspoint/state.bin";
constexpr char STATE_FILE_BAK[] = "/.crosspoint/state.bin.bak";

bool isRecentIndex(const uint16_t* recentImages, const uint8_t recentPos, const uint8_t recentFill, const uint16_t idx,
                   const uint8_t checkCount) {
  const uint8_t effectiveCount = std::min(checkCount, recentFill);
  for (uint8_t i = 0; i < effectiveCount; i++) {
    const uint8_t slot =
        (recentPos + CrossPointState::SLEEP_RECENT_COUNT - 1 - i) % CrossPointState::SLEEP_RECENT_COUNT;
    if (recentImages[slot] == idx) return true;
  }
  return false;
}

void pushRecentIndex(uint16_t* recentImages, uint8_t& recentPos, uint8_t& recentFill, const uint16_t idx) {
  recentImages[recentPos] = idx;
  recentPos = (recentPos + 1) % CrossPointState::SLEEP_RECENT_COUNT;
  if (recentFill < CrossPointState::SLEEP_RECENT_COUNT) recentFill++;
}
}  // namespace

bool CrossPointState::isRecentSleep(uint16_t idx, uint8_t checkCount) const {
  return isRecentIndex(recentSleepImages, recentSleepPos, recentSleepFill, idx, checkCount);
}

void CrossPointState::pushRecentSleep(uint16_t idx) {
  pushRecentIndex(recentSleepImages, recentSleepPos, recentSleepFill, idx);
}

bool CrossPointState::isRecentOverlaySleep(uint16_t idx, uint8_t checkCount) const {
  return isRecentIndex(recentOverlaySleepImages, recentOverlaySleepPos, recentOverlaySleepFill, idx, checkCount);
}

void CrossPointState::pushRecentOverlaySleep(uint16_t idx) {
  pushRecentIndex(recentOverlaySleepImages, recentOverlaySleepPos, recentOverlaySleepFill, idx);
}

void CrossPointState::toJson(JsonDocument& doc) const {
  doc["openEpubPath"] = openEpubPath;
  JsonArray recentArr = doc["recentSleepImages"].to<JsonArray>();
  for (int i = 0; i < SLEEP_RECENT_COUNT; i++) recentArr.add(recentSleepImages[i]);
  doc["recentSleepPos"] = recentSleepPos;
  doc["recentSleepFill"] = recentSleepFill;
  JsonArray recentOverlayArr = doc["recentOverlaySleepImages"].to<JsonArray>();
  for (int i = 0; i < SLEEP_RECENT_COUNT; i++) recentOverlayArr.add(recentOverlaySleepImages[i]);
  doc["recentOverlaySleepPos"] = recentOverlaySleepPos;
  doc["recentOverlaySleepFill"] = recentOverlaySleepFill;
  doc["readerActivityLoadCount"] = readerActivityLoadCount;
  doc["lastSleepFromReader"] = lastSleepFromReader;
  doc["showBootScreen"] = showBootScreen;
  doc["lastKnownValidTimestamp"] = lastKnownValidTimestamp;
}

bool CrossPointState::fromJson(JsonVariantConst doc) {
  openEpubPath = doc["openEpubPath"] | "";
  memset(recentSleepImages, 0, sizeof(recentSleepImages));
  JsonArrayConst recentArr = doc["recentSleepImages"];
  const int actualCount =
      recentArr.isNull() ? 0 : std::min(static_cast<int>(recentArr.size()), static_cast<int>(SLEEP_RECENT_COUNT));
  for (int i = 0; i < actualCount; i++) recentSleepImages[i] = recentArr[i] | static_cast<uint16_t>(0);
  recentSleepPos = doc["recentSleepPos"] | static_cast<uint8_t>(0);
  if (recentSleepPos >= SLEEP_RECENT_COUNT) recentSleepPos = actualCount > 0 ? recentSleepPos % SLEEP_RECENT_COUNT : 0;
  recentSleepFill = doc["recentSleepFill"] | static_cast<uint8_t>(0);
  recentSleepFill = static_cast<uint8_t>(std::min(static_cast<int>(recentSleepFill), actualCount));

  memset(recentOverlaySleepImages, 0, sizeof(recentOverlaySleepImages));
  JsonArrayConst recentOverlayArr = doc["recentOverlaySleepImages"];
  const int actualOverlayCount = recentOverlayArr.isNull() ? 0
                                                           : std::min(static_cast<int>(recentOverlayArr.size()),
                                                                      static_cast<int>(SLEEP_RECENT_COUNT));
  for (int i = 0; i < actualOverlayCount; i++) {
    recentOverlaySleepImages[i] = recentOverlayArr[i] | static_cast<uint16_t>(0);
  }
  recentOverlaySleepPos = doc["recentOverlaySleepPos"] | static_cast<uint8_t>(0);
  if (recentOverlaySleepPos >= SLEEP_RECENT_COUNT) {
    recentOverlaySleepPos = actualOverlayCount > 0 ? recentOverlaySleepPos % SLEEP_RECENT_COUNT : 0;
  }
  recentOverlaySleepFill = doc["recentOverlaySleepFill"] | static_cast<uint8_t>(0);
  recentOverlaySleepFill = static_cast<uint8_t>(std::min(static_cast<int>(recentOverlaySleepFill), actualOverlayCount));
  // Migrate legacy single-image field from old state.json (pre-recency-buffer).
  // Only seeds the buffer if the new buffer is empty (fresh migration, not a resave).
  if (recentSleepFill == 0 && !doc["lastSleepImage"].isNull()) {
    const uint8_t legacy = doc["lastSleepImage"] | static_cast<uint8_t>(UINT8_MAX);
    if (legacy != UINT8_MAX) pushRecentSleep(static_cast<uint16_t>(legacy));
  }
  readerActivityLoadCount = doc["readerActivityLoadCount"] | static_cast<uint8_t>(0);
  lastSleepFromReader = doc["lastSleepFromReader"] | false;
  showBootScreen = doc["showBootScreen"] | true;
  lastKnownValidTimestamp = doc["lastKnownValidTimestamp"] | static_cast<uint32_t>(0);
  return true;
}

bool CrossPointState::loadFromFile() {
  if (Storage.exists(getFilePath()) && PersistableStore<CrossPointState>::loadFromFile()) {
    return true;
  }

  if (!Storage.exists(STATE_FILE_BIN) || !loadFromBinaryFile()) {
    return false;
  }

  if (saveToFile()) {
    Storage.rename(STATE_FILE_BIN, STATE_FILE_BAK);
    LOG_DBG("CPS", "Migrated state.bin to state.json");
    return true;
  }
  LOG_ERR("CPS", "Failed to save state during migration");
  return false;
}

bool CrossPointState::loadFromBinaryFile() {
  HalFile inputFile;
  if (!Storage.openFileForRead("CPS", STATE_FILE_BIN, inputFile)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(storeMutex);

  uint8_t version = 0;
  if (!serialization::readPod(inputFile, version) || version > STATE_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    return false;
  }

  if (!serialization::readString(inputFile, openEpubPath, serialization::MAX_PATH_BYTES)) {
    LOG_ERR("CPS", "State path is truncated or oversized");
    return false;
  }
  if (version >= 2) {
    uint8_t legacyLastSleep = UINT8_MAX;
    if (!serialization::readPod(inputFile, legacyLastSleep)) return false;
    if (legacyLastSleep != UINT8_MAX) {
      pushRecentSleep(static_cast<uint16_t>(legacyLastSleep));
    }
  }
  if (version >= 3) {
    if (!serialization::readPod(inputFile, readerActivityLoadCount)) return false;
  }
  if (version >= 4) {
    if (!serialization::readPod(inputFile, lastSleepFromReader)) return false;
  } else {
    lastSleepFromReader = false;
  }

  return true;
}
