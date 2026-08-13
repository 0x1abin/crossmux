// Definition of `_btLibraryInUse` for the BLE-controller-only build.
//
// The arduino-esp32 core header cores/esp32/esp32-hal-bt-mem.h emits a global
// constructor (`_setBtLibraryInUse`) that references `_btLibraryInUse` whenever BLE
// hardware is present, but the core only DEFINES that symbol (in esp32-hal-bt.c)
// when an IDF host stack — CONFIG_BLUEDROID_ENABLED or CONFIG_NIMBLE_ENABLED — is
// enabled. CrossPoint runs the ESP-IDF BLE *controller* only, with NimBLE-Arduino
// supplying the host (both IDF host stacks are disabled in custom_sdkconfig), so the
// core never defines it and both the core lib and NimBLEDevice.cpp.o fail the final
// firmware link with "undefined reference to `_btLibraryInUse'".
//
// scripts/patch_bt_mem.py omits the core header from NimBLE-Arduino while PlatformIO
// builds. This application-owned definition replaces the missing core symbol and
// remains true because the BLE firmware always links the NimBLE host.
#ifdef ENABLE_BLUETOOTH
extern "C" {
bool _btLibraryInUse = true;
}
#endif
