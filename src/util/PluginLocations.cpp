#include "PluginLocations.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

namespace PluginLocations {

std::vector<Entry> scanPlugins() {
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
      if (findPluginDir(name) != e.dir) continue;
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

std::string findPluginDir(const char* name) {
  for (size_t i = 0; i < ROOT_COUNT; i++) {
    std::string dir = std::string(ROOTS[i]) + "/" + name;
    if (Storage.exists(dir.c_str())) return dir;
  }
  return {};
}

}  // namespace PluginLocations
