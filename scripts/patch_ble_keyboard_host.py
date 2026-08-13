"""Patch the pinned FreeInk BLE host for NimBLE-Arduino 2.3.8.

NimBLE 2.3.8 has no client-side onPassKeyDisplay callback, but the pinned SDK
still overrides it. BLE page-turners use Just Works pairing, so remove only that
dead override from PlatformIO's private library copy.
"""

Import("env")  # noqa: F821 (SCons-injected global)

import os
import atexit


METHOD = """  uint32_t onPassKeyDisplay(NimBLEConnInfo&) override {
    const uint32_t passkey = NimBLEDevice::getSecurityPasskey();
    self().onPairingPasskey(passkey);
#if FREEINK_BLE_HID_SCAN_DEBUG
    Serial.printf("[BleHid] pairing passkey: %06lu\\n", static_cast<unsigned long>(passkey));
#endif
    return passkey;
  }
"""


def patch_host():
    source = os.path.join(
        env.subst("$PROJECT_DIR"), "freeink-sdk", "libs", "network", "BleKeyboardHost", "src", "BleKeyboardHost.cpp"
    )
    if not os.path.isfile(source):
        print("patch_ble_keyboard_host: source not found; skipping")
        return

    with open(source, "r") as handle:
        text = handle.read()
    if METHOD not in text:
        print("patch_ble_keyboard_host: obsolete callback already absent")
        return

    with open(source, "w") as handle:
        handle.write(text.replace(METHOD, "", 1))
    atexit.register(_restore, source, text)
    print("patch_ble_keyboard_host: removed obsolete NimBLE client passkey callback")


def _restore(source, text):
    with open(source, "w") as handle:
        handle.write(text)


patch_host()
