#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  const std::string targetPath = path ? path : "";
  if (targetPath.empty()) {
    LOG_ERR("PERSIST", "Missing JSON path for write");
    return false;
  }

  Storage.mkdir("/.crosspoint");
  const std::string tempPath = targetPath + ".tmp";
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
  if (written == 0) {
    Storage.remove(tempPath.c_str());
    LOG_ERR("PERSIST", "Failed to serialize %s", targetPath.c_str());
    return false;
  }

  if (Storage.exists(targetPath.c_str()) && !Storage.remove(targetPath.c_str())) {
    Storage.remove(tempPath.c_str());
    LOG_ERR("PERSIST", "Failed to replace %s", targetPath.c_str());
    return false;
  }
  if (!Storage.rename(tempPath.c_str(), targetPath.c_str())) {
    Storage.remove(tempPath.c_str());
    LOG_ERR("PERSIST", "Failed to rename temp file for %s", targetPath.c_str());
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc) {
  if (!Storage.exists(path)) {
    return false;  // Expected on first boot — not an error.
  }
  String json = Storage.readFile(path);
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
  bool ok = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &ok);
  if (!ok) {
    // Deobfuscation failed — fall back to legacy plaintext password.
    pass = doc["password"] | "";
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
