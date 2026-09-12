#pragma once

#include <WiFi.h>

class GfxRenderer;

namespace NetworkStartup {

inline void prepare(GfxRenderer&) {}
inline bool setMode(GfxRenderer&, const wifi_mode_t mode) { return WiFi.mode(mode); }

}  // namespace NetworkStartup
