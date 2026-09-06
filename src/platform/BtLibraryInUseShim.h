#pragma once

#include <BoardConfig.h>

#if FREEINK_CAP_BLE_HID_HOST
#include "BleIpcStack.h"

extern "C" {
__attribute__((weak)) bool _btLibraryInUse = true;
}
#endif
