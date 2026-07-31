// BT controller memory-retention shim for the controller-only NimBLE-Arduino setup.
//
// arduino-esp32 (3.3.x) decides at boot whether to release the ~36 KB BT
// controller memory via btInUse()/_btLibraryInUse: declared in the core's
// esp32-hal-bt-mem.h, defined in esp32-hal-bt.c. The core compiles that
// definition ONLY when CONFIG_BLUEDROID_ENABLED or CONFIG_NIMBLE_ENABLED is set.
//
// This firmware runs the ESP-IDF BLE *controller* only: both IDF host stacks are
// disabled in custom_sdkconfig (CONFIG_BT_BLUEDROID_ENABLED=n /
// CONFIG_BT_NIMBLE_ENABLED=n) so they don't collide with NimBLE-Arduino, which
// supplies the host. In that configuration the core omits the definition, yet
// NimBLE-Arduino's esp32-hal-bt-mem.h still emits a constructor referencing
// _btLibraryInUse (its guard is the broader SOC_BLE_SUPPORTED). A clean core
// rebuild therefore link-fails with "undefined reference to `_btLibraryInUse'".
//
// Provide the symbol here. We do use the BT controller, so it stays true (the
// core's constructor sets it true regardless). Weak, so any configuration where
// the core *does* define it wins with no duplicate-symbol error.
#include <soc/soc_caps.h>

#if defined(SOC_BLE_SUPPORTED) && defined(FREEINK_CAP_BLE_HID_HOST)
extern "C" {
__attribute__((weak)) bool _btLibraryInUse = true;
}
#endif
