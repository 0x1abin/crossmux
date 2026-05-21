#pragma once

#include <string>

namespace airpage {

// Returns this device's 16-character random id, unrelated to the hardware MAC.
// On first use a random id is generated (alphabet A-Za-z0-9, 62 chars) and
// persisted to the SD card at /.crosspoint/airpage_device_id. Subsequent calls
// return that stored id, so it is stable across reboots. Deleting the file (or
// swapping/formatting the SD card) clears it and a new id is generated next time.
// Computed once and cached in a function-local static.
const std::string& deviceId();

// AirPage "live push" mode flag, persisted to the SD card at
// /.crosspoint/airpage/mode ("1"/missing = live MQTT push, "0" = manual).
// Default is ON: a fresh device (no file yet) boots into live push. Kept out of
// CrossPointSettings on purpose: it is AirPage-internal state (like the device
// id), toggled only from the AirPage face menu, and must not surface in the
// global Settings UI. Read once on face entry; written immediately on every
// toggle so it survives reboot.
bool loadRealtimeMode();
void saveRealtimeMode(bool enabled);

}  // namespace airpage
