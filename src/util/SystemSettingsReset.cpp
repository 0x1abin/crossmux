#include "SystemSettingsReset.h"

#include <HalStorage.h>
#include <Logging.h>

namespace {

constexpr const char* RESET_PATHS[] = {
    "/.crosspoint/settings.json",    "/.crosspoint/settings.json.tmp", "/.crosspoint/settings.bin",
    "/.crosspoint/settings.bin.bak", "/.crosspoint/language.bin",      "/.crosspoint/language.bin.bak",
    "/.crosspoint/wifi.json",        "/.crosspoint/wifi.json.tmp",     "/.crosspoint/opds.json",
    "/.crosspoint/opds.json.tmp",    "/.crosspoint/koreader.json",     "/.crosspoint/koreader.json.tmp",
};

}  // namespace

bool systemSettingsReset::clearPersistedSettings() {
  bool success = true;
  for (const char* path : RESET_PATHS) {
    if (Storage.exists(path) && !Storage.remove(path)) {
      LOG_ERR("RESET", "Failed to remove %s", path);
      success = false;
    }
  }
  return success;
}
