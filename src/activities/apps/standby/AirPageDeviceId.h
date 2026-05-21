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

}  // namespace airpage
