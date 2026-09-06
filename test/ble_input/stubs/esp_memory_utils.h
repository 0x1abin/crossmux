#pragma once
inline bool probeExternal = true;
inline bool esp_ptr_external_ram(const void*) { return probeExternal; }
