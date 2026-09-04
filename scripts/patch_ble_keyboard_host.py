"""Remove a callback absent from the pinned NimBLE-Arduino 2.3.8 API."""

import atexit
import os

Import("env")  # noqa: F821

METHOD = """  uint32_t onPassKeyDisplay(NimBLEConnInfo&) override {
    const uint32_t passkey = NimBLEDevice::getSecurityPasskey();
    self().onPairingPasskey(passkey);
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] pairing passkey: %06lu\\n", static_cast<unsigned long>(passkey));
#endif
    return passkey;
  }
"""

source = os.path.join(
    env.subst("$PROJECT_DIR"),
    "freeink-sdk",
    "libs",
    "network",
    "BleKeyboardHost",
    "src",
    "BleKeyboardHost.cpp",
)

if os.path.isfile(source):
    with open(source, encoding="utf-8") as source_file:
        original = source_file.read()
    if METHOD in original:
        with open(source, "w", encoding="utf-8") as source_file:
            source_file.write(original.replace(METHOD, "", 1))

        def restore():
            with open(source, "w", encoding="utf-8") as source_file:
                source_file.write(original)

        atexit.register(restore)
        print("patch_ble_keyboard_host: removed obsolete passkey callback")
