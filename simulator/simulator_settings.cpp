#include "simulator_settings.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace simulator {

const std::string& hostSettingsPath() {
  static std::string cached = []() {
    const char* home = std::getenv("HOME");
    if (home && *home) {
      return std::string(home) + "/.crosspoint_simulator.json";
    }
    return std::string("./simulator_settings.json");
  }();
  return cached;
}

bool loadHostSettings(HostSettings& out) {
  std::ifstream in(hostSettingsPath(), std::ios::binary);
  if (!in.good()) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string body = ss.str();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    std::fprintf(stderr, "simulator: failed to parse %s: %s\n",
                 hostSettingsPath().c_str(), err.c_str());
    return false;
  }
  if (doc["showDeviceShell"].is<bool>()) {
    out.showDeviceShell = doc["showDeviceShell"].as<bool>();
  }
  return true;
}

bool saveHostSettings(const HostSettings& s) {
  JsonDocument doc;
  doc["showDeviceShell"] = s.showDeviceShell;

  const std::string& path = hostSettingsPath();
  const std::string tmpPath = path + ".tmp";
  {
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
      std::fprintf(stderr, "simulator: cannot open %s for write\n", tmpPath.c_str());
      return false;
    }
    std::string serialized;
    serializeJson(doc, serialized);
    out << serialized;
    if (!out.good()) {
      std::fprintf(stderr, "simulator: write to %s failed\n", tmpPath.c_str());
      return false;
    }
  }
  // rename() is atomic on POSIX when source/target are on the same FS.
  if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
    std::fprintf(stderr, "simulator: rename %s -> %s failed\n",
                 tmpPath.c_str(), path.c_str());
    return false;
  }
  return true;
}

}  // namespace simulator
