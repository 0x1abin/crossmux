#pragma once

// Forced into NimBLE-Arduino translation units only, after the core's defaults.
#include <sdkconfig.h>

#if CROSSPOINT_BLE_HOST_PSRAM
#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL
#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1
#endif
