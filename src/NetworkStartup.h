#pragma once

#include <esp_wifi_types.h>

class GfxRenderer;

namespace NetworkStartup {

// Release rebuildable render memory before the Wi-Fi driver starts allocating.
void prepare(GfxRenderer& renderer);

// The only entry point for enabling STA/AP mode in application code.
bool setMode(GfxRenderer& renderer, wifi_mode_t mode);

}  // namespace NetworkStartup
