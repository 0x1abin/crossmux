#pragma once

#include <string>

namespace simulator {

// Host-side simulator preferences persisted across runs (separate from the
// firmware's CrossPointSettings — those live on the simulated SD card and
// describe device state, not the simulator window/host UI).
struct HostSettings {
  bool showDeviceShell = false;  // Draw black bezel/buttons around eink area.
};

// Read settings from JSON file at hostSettingsPath(). Returns false if the file
// is missing or unparseable — caller should treat the in-out struct as already
// holding the desired defaults in that case (this function only overwrites
// fields it finds in the file).
bool loadHostSettings(HostSettings& out);

// Atomically write settings to hostSettingsPath() (write to .tmp then rename).
bool saveHostSettings(const HostSettings& s);

// Returns the absolute path used for persistence. Prefers $HOME, falls back to
// the current working directory when HOME is unset.
const std::string& hostSettingsPath();

}  // namespace simulator
