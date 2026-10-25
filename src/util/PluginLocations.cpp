#include "PluginLocations.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"

namespace PluginLocations {

std::vector<Entry> scanPluginsOnDisk() {
  std::vector<Entry> plugins;
  // ponytail: 32 installed entries fit the C3 picker budget; page discovery if that ceiling is needed.
  constexpr size_t MAX_PLUGINS = 32;
  if (!memory::hasAllocationHeadroom(ESP.getFreeHeap(), ESP.getMaxAllocHeap(), MAX_PLUGINS * sizeof(Entry),
                                     MAX_PLUGINS * sizeof(Entry), 32 * 1024, 4096)) {
    LOG_ERR("PLUG", "OOM: installed plugin picker");
    return plugins;
  }
  plugins.reserve(MAX_PLUGINS);
  for (size_t r = 0; r < ROOT_COUNT; r++) {
    HalFile root = Storage.open(ROOTS[r]);
    if (!root || !root.isDirectory()) continue;
    for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      if (!entry.isDirectory()) continue;
      char name[128];
      if (entry.getName(name, sizeof(name)) == 0 || name[0] == '.') continue;
      Entry e;
      e.name = name;
      e.dir = std::string(ROOTS[r]) + "/" + name;
      // Reuse the authoritative root precedence; no unbounded seen-name table.
      if (findPluginDirOnDisk(name) != e.dir) continue;
      e.hasPluginJs = Storage.exists((e.dir + "/plugin.js").c_str());
      e.hasDevice = Storage.exists((e.dir + "/device.json").c_str());
      e.hasManifest = Storage.exists((e.dir + "/manifest.json").c_str());
      if (e.hasPluginJs || e.hasDevice || e.hasManifest) {
        if (plugins.size() == MAX_PLUGINS) {
          LOG_ERR("PLUG", "Installed plugin picker limit (%u)", unsigned(MAX_PLUGINS));
          return plugins;
        }
        plugins.push_back(std::move(e));
      }
    }
  }
  return plugins;
}

std::vector<Entry> scanPlugins() {
  if (!SETTINGS.pluginsEnabled) return {};
  auto plugins = scanPluginsOnDisk();
  plugins.erase(std::remove_if(plugins.begin(), plugins.end(),
                               [](const Entry& entry) {
                                 return !isDiscoverableEntry(isPluginQuarantined(entry.dir), entry.hasPluginJs,
                                                             entry.hasDevice, entry.hasManifest);
                               }),
                plugins.end());
  return plugins;
}

bool anyPluginInstalledOnDisk() {
  for (size_t r = 0; r < ROOT_COUNT; r++) {
    HalFile root = Storage.open(ROOTS[r]);
    if (!root || !root.isDirectory()) continue;
    for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      if (!entry.isDirectory()) continue;
      char name[128];
      if (entry.getName(name, sizeof(name)) == 0 || name[0] == '.') continue;
      const std::string dir = std::string(ROOTS[r]) + "/" + name;
      if (Storage.exists((dir + "/plugin.js").c_str()) || Storage.exists((dir + "/device.json").c_str()) ||
          Storage.exists((dir + "/manifest.json").c_str())) {
        return true;
      }
    }
  }
  return false;
}

std::string findPluginDirOnDisk(const char* name) {
  for (size_t i = 0; i < ROOT_COUNT; i++) {
    std::string dir = std::string(ROOTS[i]) + "/" + name;
    if (Storage.exists(dir.c_str())) return dir;
  }
  return {};
}

bool isPluginQuarantined(const std::string& dir) { return Storage.exists((dir + "/" + INSTALL_MARKER).c_str()); }

std::string findPluginDir(const char* name) {
  if (!SETTINGS.pluginsEnabled) return {};
  const std::string dir = findPluginDirOnDisk(name);
  if (dir.empty() || isPluginQuarantined(dir)) return {};
  return dir;
}

}  // namespace PluginLocations
