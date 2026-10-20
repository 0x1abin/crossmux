#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ObfuscationUtils.h>

#include <cstring>
#include <limits>

bool PersistableStoreBase::recoverTempFile(const char* path) {
  if (!path || !*path) {
    LOG_ERR("PERSIST", "Missing JSON path for recovery");
    return false;
  }
  HalStorage::StorageLock lock;
  if (Storage.exists(path)) return true;

  const size_t pathLength = strlen(path);
  // Bookmark/export paths vary; allocate exactly the recovery path without a large stack buffer.
  auto tempPath = makeUniqueNoThrow<char[]>(pathLength + sizeof(".tmp"));
  if (!tempPath) {
    LOG_ERR("PERSIST", "OOM allocating %zu bytes for JSON recovery path", pathLength + sizeof(".tmp"));
    return false;
  }
  memcpy(tempPath.get(), path, pathLength);
  memcpy(tempPath.get() + pathLength, ".tmp", sizeof(".tmp"));
  if (!Storage.exists(tempPath.get())) return false;
  if (!Storage.rename(tempPath.get(), path)) {
    LOG_ERR("PERSIST", "Cannot recover staged JSON for %s", path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  if (doc.overflowed()) {
    LOG_ERR("PERSIST", "Insufficient heap for JSON document");
    return false;
  }

  const std::string targetPath = path ? path : "";
  if (targetPath.empty()) {
    LOG_ERR("PERSIST", "Missing JSON path for write");
    return false;
  }

  HalStorage::StorageLock lock;
  Storage.mkdir("/.crosspoint");
  const std::string tempPath = targetPath + ".tmp";
  if (!Storage.exists(targetPath.c_str()) && Storage.exists(tempPath.c_str()) &&
      !Storage.rename(tempPath.c_str(), targetPath.c_str())) {
    LOG_ERR("PERSIST", "Cannot recover staged JSON for %s", targetPath.c_str());
    return false;
  }
  if (Storage.exists(tempPath.c_str())) {
    Storage.remove(tempPath.c_str());
  }

  size_t written = 0;
  {
    HalFile file;
    if (!Storage.openFileForWrite("PERSIST", tempPath.c_str(), file)) {
      LOG_ERR("PERSIST", "Failed to open %s", tempPath.c_str());
      return false;
    }
    written = serializeJson(doc, file);
    file.flush();
  }
  if (written == 0 || written != measureJson(doc)) {
    Storage.remove(tempPath.c_str());
    LOG_ERR("PERSIST", "Failed to serialize %s", targetPath.c_str());
    return false;
  }

  if (Storage.exists(targetPath.c_str()) && !Storage.remove(targetPath.c_str())) {
    LOG_ERR("PERSIST", "Failed to replace %s", targetPath.c_str());
    return false;
  }
  if (!Storage.rename(tempPath.c_str(), targetPath.c_str())) {
    LOG_ERR("PERSIST", "Failed to rename temp file for %s", targetPath.c_str());
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  String json;
  {
    HalStorage::StorageLock lock;
    if (!recoverTempFile(path)) {
      return false;  // Expected on first boot — not an error.
    }
    json = Storage.readFile(path);
  }
  if (json.isEmpty()) {
    LOG_ERR("PERSIST", "Failed to read %s (empty)", path);
    return false;
  }
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", path, error.c_str());
    return false;
  }
  return true;
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool valid = false;
  return extractPassword(doc, needsResave, std::numeric_limits<size_t>::max(), valid);
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave, const size_t maxLength,
                                                  bool& valid) {
  valid = true;
  bool ok = false;
  bool tooLong = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", maxLength, &ok, &tooLong);
  if (tooLong) {
    valid = false;
    return "";
  }
  if (!ok) {
    // Deobfuscation failed — fall back to legacy plaintext password.
    const char* legacyPassword = doc["password"] | "";
    const size_t legacyLength = strlen(legacyPassword);
    if (legacyLength > maxLength) {
      valid = false;
      return "";
    }
    pass.assign(legacyPassword, legacyLength);
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
