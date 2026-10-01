#pragma once

#include <BoardConfig.h>

#if FREEINK_CAP_BLE_HID_HOST
#include "BleIpcStack.h"

extern "C" {
__attribute__((weak)) bool _btLibraryInUse = true;
// Arduino 3.3.11 split the flag by transport. Controller-only custom cores
// omit the definition, while NimBLE's constructor still references it.
__attribute__((weak)) bool _bleLibraryInUse = true;
}
#endif
