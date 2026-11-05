#ifdef ENABLE_CHINESE_VERSION
#include "WeReadAutoSync.h"

#include <Arduino.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstdio>
#include <cstring>

#include "NetworkStartup.h"
#include "WifiCredentialStore.h"
#include "activities/RenderLock.h"
#include "util/TimeUtils.h"

namespace weread_auto {
namespace {
char queuedBook[64]{};
WeReadClient::ProgressSyncInput queuedInput;
uint32_t startedAt = 0;
bool expired(void*) { return millis() - startedAt >= 45000u; }
}  // namespace

void queue(const char* bookId, const WeReadClient::ProgressSyncInput input) {
  if (!bookId || !bookId[0] || strlen(bookId) >= sizeof(queuedBook)) return;
  const std::string directory = WeReadStore::bookDirectory(bookId);
  if (!Storage.ensureDirectoryExists(directory.c_str())) return;
  char record[128];
  snprintf(record, sizeof(record), "v1 %.9g %u %u %u", static_cast<double>(input.localFraction), input.localTocIndex,
           input.localOffset, static_cast<unsigned>(input.localOffsetBasis));
  const std::string path = directory + "/auto_sync_pending";
  const std::string lastUploaded = directory + "/auto_sync_local";
  if (!Storage.exists(path.c_str()) && Storage.readFile(lastUploaded.c_str()) == record) return;
  // Durable intent precedes networking. Failures and conflicts leave it intact.
  if (!Storage.writeFile(path.c_str(), String(record))) {
    LOG_ERR("WRAuto", "Failed to persist pending progress");
    return;
  }
  snprintf(queuedBook, sizeof(queuedBook), "%s", bookId);
  queuedInput = input;
}

void drain(GfxRenderer& renderer) {
  if (!queuedBook[0]) return;
  char bookId[64];
  memcpy(bookId, queuedBook, sizeof(bookId));
  queuedBook[0] = '\0';
  const auto input = queuedInput;
  const std::string path = WeReadStore::bookDirectory(bookId) + "/auto_sync_pending";
  WeReadStore::Session session;
  const bool loggedIn = WeReadStore::loadSession(session) && session.valid();
  session.clear();
  if (!loggedIn) return;
  // The operation owns HTTP scratch across steps (~8 KiB); a fallible heap
  // allocation avoids overflowing the main task stack and follows reader teardown.
  auto operation = makeUniqueNoThrow<WeReadClient::Operation>();
  if (!operation) {
    LOG_ERR("WRAuto", "OOM: progress workspace");
    return;
  }
  powerManager.setPowerSaving(false);
  const bool ownWifi = WiFi.getMode() == WIFI_MODE_NULL;
  bool connected = WiFi.status() == WL_CONNECTED;
  startedAt = millis();
  if (!connected && ownWifi) {
    if (!WIFI_STORE.getCredentialCount()) WIFI_STORE.loadFromFile();
    const auto credential = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
    if (!credential) return;
    if (!NetworkStartup::setMode(renderer, WIFI_STA)) {
      WiFi.mode(WIFI_OFF);
      esp_wifi_deinit();
      return;
    }
    WiFi.persistent(false);
    WiFi.begin(credential->ssid.c_str(), credential->password.empty() ? nullptr : credential->password.c_str());
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 12000u) delay(50);
    connected = WiFi.status() == WL_CONNECTED;
  }
  bool success = false;
  if (connected && (TimeUtils::isClockValid() || halClock.syncNow()) && !expired(nullptr) &&
      operation->beginProgressSync(bookId, input, WeReadClient::ProgressSyncMode::AutoUpload)) {
    while (!expired(nullptr)) {
      WeReadClient::Operation::Event event;
      {
        RenderLock lock;
        event = operation->step(nullptr, nullptr, expired, nullptr);
      }
      if (event == WeReadClient::Operation::Event::Complete) {
        const auto outcome = operation->progressSyncResult().outcome;
        success = outcome == WeReadClient::ProgressSyncOutcome::LocalUploaded ||
                  outcome == WeReadClient::ProgressSyncOutcome::AlreadySynced;
        LOG_INF("WRAuto", "completed outcome=%u", static_cast<unsigned>(outcome));
        break;
      }
      if (event == WeReadClient::Operation::Event::Failed || event == WeReadClient::Operation::Event::Cancelled) break;
      delay(10);
    }
  }
  operation.reset();
  if (success) {
    const String record = Storage.readFile(path.c_str());
    const std::string localPath = WeReadStore::bookDirectory(bookId) + "/auto_sync_local";
    if (record.length() && Storage.writeFile(localPath.c_str(), record)) Storage.remove(path.c_str());
  } else
    LOG_INF("WRAuto", "Progress retained for retry or manual conflict resolution");
  if (ownWifi) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    esp_wifi_deinit();
  }
}
}  // namespace weread_auto
#endif
