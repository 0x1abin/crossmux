#include "JsonSettingsIO.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <string>
#include <utility>

#include "AchievementsStore.h"
#include "ReadingStatsStore.h"
#include "util/BookIdentity.h"

namespace {

// Atomic JSON write: serialize (streamed) to "<path>.tmp", then rename over the
// target so a power loss mid-write never corrupts the existing file. Streaming
// keeps peak heap bounded for large analytics documents.
bool saveJsonDocumentToFile(const char* moduleName, const char* path, const JsonDocument& doc) {
  if (!path || *path == '\0') {
    LOG_ERR(moduleName, "Missing JSON path for write");
    return false;
  }
  if (doc.overflowed()) {
    LOG_ERR(moduleName, "Insufficient heap for JSON document");
    return false;
  }

  const size_t pathLength = strlen(path);
  const size_t tempPathSize = pathLength + sizeof(".tmp");
  // Export paths are variable-length, so use one fallible exact-size buffer instead of fixed stack storage.
  auto tempPath = makeUniqueNoThrow<char[]>(tempPathSize);
  if (!tempPath) {
    LOG_ERR(moduleName, "OOM allocating JSON temp path (%zu bytes)", tempPathSize);
    return false;
  }
  memcpy(tempPath.get(), path, pathLength);
  memcpy(tempPath.get() + pathLength, ".tmp", sizeof(".tmp"));

  if (Storage.exists(tempPath.get())) {
    Storage.remove(tempPath.get());
  }

  size_t written = 0;
  {
    HalFile file;
    if (!Storage.openFileForWrite(moduleName, tempPath.get(), file)) {
      LOG_ERR(moduleName, "Could not open JSON file for write: %s", tempPath.get());
      return false;
    }

    written = serializeJson(doc, file);
    file.flush();
  }
  if (written == 0) {
    Storage.remove(tempPath.get());
    LOG_ERR(moduleName, "serializeJson wrote 0 bytes for %s", path);
    return false;
  }

  if (Storage.exists(path) && !Storage.remove(path)) {
    Storage.remove(tempPath.get());
    LOG_ERR(moduleName, "Could not remove JSON file before replace: %s", path);
    return false;
  }

  if (!Storage.rename(tempPath.get(), path)) {
    Storage.remove(tempPath.get());
    LOG_ERR(moduleName, "Could not rename JSON temp file to final path: %s", path);
    return false;
  }

  return true;
}

}  // namespace

// ---- ReadingStatsStore ----
// reading_stats.json, format version 6. Written atomically (temp + rename) and
// parsed via a streamed HalFileStream so large histories don't double peak heap.

bool JsonSettingsIO::saveReadingStats(const ReadingStatsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 6;

  JsonArray days = doc["readingDays"].to<JsonArray>();
  for (const auto& day : store.getReadingDays()) {
    JsonObject dayObj = days.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  JsonArray legacyDays = doc["legacyReadingDays"].to<JsonArray>();
  for (const auto& day : store.legacyReadingDays) {
    JsonObject dayObj = legacyDays.add<JsonObject>();
    dayObj["dayOrdinal"] = day.dayOrdinal;
    dayObj["readingMs"] = day.readingMs;
  }

  JsonArray sessionLog = doc["sessionLog"].to<JsonArray>();
  for (const auto& session : store.getSessionLog()) {
    JsonObject sessionObj = sessionLog.add<JsonObject>();
    sessionObj["dayOrdinal"] = session.dayOrdinal;
    sessionObj["sessionMs"] = session.sessionMs;
  }

  JsonArray books = doc["books"].to<JsonArray>();
  for (const auto& book : store.getBooks()) {
    JsonObject obj = books.add<JsonObject>();
    obj["bookId"] = book.bookId;
    obj["path"] = book.path;
    JsonArray knownPaths = obj["knownPaths"].to<JsonArray>();
    for (const auto& knownPath : book.knownPaths) {
      knownPaths.add(knownPath);
    }
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
    obj["chapterTitle"] = book.chapterTitle;
    obj["totalReadingMs"] = book.totalReadingMs;
    obj["sessions"] = book.sessions;
    obj["lastSessionMs"] = book.lastSessionMs;
    obj["firstReadAt"] = book.firstReadAt;
    obj["lastReadAt"] = book.lastReadAt;
    obj["completedAt"] = book.completedAt;
    obj["lastProgressPercent"] = book.lastProgressPercent;
    obj["chapterProgressPercent"] = book.chapterProgressPercent;
    obj["completed"] = book.completed;

    JsonArray bookDays = obj["readingDays"].to<JsonArray>();
    for (const auto& day : book.readingDays) {
      JsonObject dayObj = bookDays.add<JsonObject>();
      dayObj["dayOrdinal"] = day.dayOrdinal;
      dayObj["readingMs"] = day.readingMs;
    }
  }

  return saveJsonDocumentToFile("RST", path, doc);
}

bool JsonSettingsIO::loadReadingStats(ReadingStatsStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("RST", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.books.clear();
  store.legacyReadingDays.clear();
  store.readingDays.clear();
  store.sessionLog.clear();
  store.dirty = false;

  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  auto appendReadingDays = [](std::vector<ReadingDayStats>& destination, JsonArray source) {
    for (JsonVariant value : source) {
      ReadingDayStats day;
      if (value.is<JsonObject>()) {
        JsonObject obj = value.as<JsonObject>();
        day.dayOrdinal = obj["dayOrdinal"] | static_cast<uint32_t>(0);
        day.readingMs = obj["readingMs"] | static_cast<uint64_t>(0);
      } else {
        day.dayOrdinal = value | static_cast<uint32_t>(0);
        day.readingMs = 0;
      }
      if (day.dayOrdinal != 0) {
        destination.push_back(day);
      }
    }
  };

  appendReadingDays(store.readingDays, doc["readingDays"].as<JsonArray>());
  if (formatVersion >= 2) {
    appendReadingDays(store.legacyReadingDays, doc["legacyReadingDays"].as<JsonArray>());
    if (formatVersion < 6 && store.legacyReadingDays.empty()) {
      store.legacyReadingDays = store.readingDays;
    }
  } else {
    store.legacyReadingDays = store.readingDays;
  }

  if (formatVersion >= 4) {
    for (JsonObject sessionObj : doc["sessionLog"].as<JsonArray>()) {
      ReadingSessionLogEntry session;
      session.dayOrdinal = sessionObj["dayOrdinal"] | static_cast<uint32_t>(0);
      session.sessionMs = sessionObj["sessionMs"] | static_cast<uint32_t>(0);
      if (session.dayOrdinal != 0 && session.sessionMs != 0) {
        store.sessionLog.push_back(session);
      }
    }
  } else {
    store.dirty = true;
  }

  JsonArray books = doc["books"].as<JsonArray>();
  for (JsonObject obj : books) {
    ReadingBookStats book;
    book.bookId = obj["bookId"] | std::string("");
    book.path = obj["path"] | std::string("");
    if (book.path.empty()) {
      continue;
    }
    for (JsonVariant value : obj["knownPaths"].as<JsonArray>()) {
      const std::string knownPath = value | std::string("");
      if (!knownPath.empty()) {
        book.knownPaths.push_back(knownPath);
      }
    }
    book.title = obj["title"] | std::string("");
    book.author = obj["author"] | std::string("");
    book.coverBmpPath = obj["coverBmpPath"] | std::string("");
    book.chapterTitle = obj["chapterTitle"] | std::string("");
    book.totalReadingMs = obj["totalReadingMs"] | static_cast<uint64_t>(0);
    book.sessions = obj["sessions"] | static_cast<uint32_t>(0);
    book.lastSessionMs = obj["lastSessionMs"] | static_cast<uint32_t>(0);
    book.firstReadAt = obj["firstReadAt"] | static_cast<uint32_t>(0);
    book.lastReadAt = obj["lastReadAt"] | static_cast<uint32_t>(0);
    book.completedAt = obj["completedAt"] | static_cast<uint32_t>(0);
    book.lastProgressPercent = obj["lastProgressPercent"] | static_cast<uint8_t>(0);
    book.chapterProgressPercent = obj["chapterProgressPercent"] | static_cast<uint8_t>(0);
    book.completed = obj["completed"] | false;
    if (formatVersion >= 2) {
      appendReadingDays(book.readingDays, obj["readingDays"].as<JsonArray>());
    }
    if (formatVersion < 3 || book.bookId.empty()) {
      store.dirty = true;
    }
    store.books.push_back(std::move(book));
  }

  if (formatVersion < 6) {
    store.convertLegacyReadingDaysToUnassigned();
    store.dirty = true;
  }
  store.rebuildAggregatedReadingDays();
  LOG_DBG("RST", "Reading stats loaded from file (%d books)", static_cast<int>(store.books.size()));
  return true;
}

bool JsonSettingsIO::loadReadingStatsFromFile(ReadingStatsStore& store, const char* path) {
  if (!Storage.exists(path)) {
    return false;
  }
  const String json = Storage.readFile(path);
  if (json.isEmpty()) {
    return false;
  }
  return loadReadingStats(store, json.c_str());
}

// ---- AchievementsStore ----
// achievements.json, format version 2. Written atomically (temp + rename).

bool JsonSettingsIO::saveAchievements(const AchievementsStore& store, const char* path) {
  JsonDocument doc;
  doc["formatVersion"] = 2;
  doc["accumulatedReadingMs"] = store.accumulatedReadingMs;
  doc["countedSessions"] = store.countedSessions;
  doc["totalBookmarksAdded"] = store.totalBookmarksAdded;
  doc["longestSessionMs"] = store.longestSessionMs;
  doc["goalDaysCount"] = store.goalDaysCount;
  doc["currentGoalStreak"] = store.currentGoalStreak;
  doc["maxGoalStreak"] = store.maxGoalStreak;
  doc["lastGoalDayOrdinal"] = store.lastGoalDayOrdinal;
  doc["resetDayOrdinal"] = store.resetDayOrdinal;
  doc["resetDayBaselineMs"] = store.resetDayBaselineMs;

  JsonArray states = doc["states"].to<JsonArray>();
  for (const auto& state : store.states) {
    JsonObject obj = states.add<JsonObject>();
    obj["unlocked"] = state.unlocked;
    obj["unlockedAt"] = state.unlockedAt;
  }

  JsonArray startedBooks = doc["startedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.startedBooks) {
    startedBooks.add(pathValue);
  }

  JsonArray finishedBooks = doc["finishedBooks"].to<JsonArray>();
  for (const auto& pathValue : store.finishedBooks) {
    finishedBooks.add(pathValue);
  }

  return saveJsonDocumentToFile("ACH", path, doc);
}

bool JsonSettingsIO::loadAchievements(AchievementsStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("ACH", "JSON parse error: %s", error.c_str());
    return false;
  }

  store.states = {};
  store.startedBooks.clear();
  store.finishedBooks.clear();
  store.pendingUnlocks.clear();
  store.dirty = false;
  const uint32_t formatVersion = doc["formatVersion"] | static_cast<uint32_t>(1);

  store.accumulatedReadingMs = doc["accumulatedReadingMs"] | static_cast<uint64_t>(0);
  store.countedSessions = doc["countedSessions"] | static_cast<uint32_t>(0);
  store.totalBookmarksAdded = doc["totalBookmarksAdded"] | static_cast<uint32_t>(0);
  store.longestSessionMs = doc["longestSessionMs"] | static_cast<uint32_t>(0);
  store.goalDaysCount = doc["goalDaysCount"] | static_cast<uint32_t>(0);
  store.currentGoalStreak = doc["currentGoalStreak"] | static_cast<uint32_t>(0);
  store.maxGoalStreak = doc["maxGoalStreak"] | static_cast<uint32_t>(0);
  store.lastGoalDayOrdinal = doc["lastGoalDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayOrdinal = doc["resetDayOrdinal"] | static_cast<uint32_t>(0);
  store.resetDayBaselineMs = doc["resetDayBaselineMs"] | static_cast<uint64_t>(0);
  // Session serials are runtime-only; persisted values collide after ReadingStatsStore resets on reboot.
  store.lastProcessedSessionSerial = 0;

  JsonArray states = doc["states"].as<JsonArray>();
  size_t stateIndex = 0;
  for (JsonObject obj : states) {
    if (stateIndex >= store.states.size()) {
      break;
    }
    store.states[stateIndex].unlocked = obj["unlocked"] | false;
    store.states[stateIndex].unlockedAt = obj["unlockedAt"] | static_cast<uint32_t>(0);
    ++stateIndex;
  }

  for (JsonVariant value : doc["startedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.startedBooks.push_back(bookKey);
    }
  }

  for (JsonVariant value : doc["finishedBooks"].as<JsonArray>()) {
    std::string bookKey = value | std::string("");
    if (formatVersion < 2 && !bookKey.empty()) {
      if (const auto* statsBook = READING_STATS.findMatchingBookForPath(bookKey)) {
        bookKey = statsBook->bookId;
      } else {
        bookKey = BookIdentity::resolveStableBookId(bookKey);
      }
      store.dirty = true;
    }
    if (!bookKey.empty()) {
      store.finishedBooks.push_back(bookKey);
    }
  }

  return true;
}

bool JsonSettingsIO::loadAchievementsFromFile(AchievementsStore& store, const char* path) {
  if (!Storage.exists(path)) {
    return false;
  }
  const String json = Storage.readFile(path);
  if (json.isEmpty()) {
    return false;
  }
  return loadAchievements(store, json.c_str());
}
