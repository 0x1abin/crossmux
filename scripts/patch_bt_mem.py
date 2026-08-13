"""Keep NimBLE-Arduino out of arduino-esp32's BT usage shim.

Controller-only builds do not define ``_btLibraryInUse`` while rebuilding the
Arduino core. NimBLE-Arduino 2.3.8 includes the core header that references it,
so that temporary core build cannot link. The firmware supplies the flag in
``BtLibraryInUseShim.cpp``; omit the header only while PlatformIO builds.
"""

import atexit
import os

Import("env")  # noqa: F821 (SCons-injected global)

SOURCE = os.path.join(
    env.subst("$PROJECT_LIBDEPS_DIR"),  # noqa: F821
    env.subst("$PIOENV"),  # noqa: F821
    "NimBLE-Arduino",
    "src",
    "NimBLEDevice.cpp",
)
INCLUDE = '#   include "esp32-hal-bt-mem.h"'
PATCHED = '// CrossMux controller-only build: BT usage flag supplied by application'


if not os.path.isfile(SOURCE):
    print(f"patch_bt_mem: {SOURCE} not found; skipping")
else:
    with open(SOURCE, encoding="utf-8") as source_file:
        original = source_file.read()

    if INCLUDE in original:
        restored = original
        with open(SOURCE, "w", encoding="utf-8") as source_file:
            source_file.write(original.replace(INCLUDE, PATCHED, 1))
    elif PATCHED in original:
        restored = original.replace(PATCHED, INCLUDE, 1)
    else:
        print("patch_bt_mem: NimBLE include not found; upstream source changed")

    if INCLUDE in original or PATCHED in original:

        def restore():
            with open(SOURCE, "w", encoding="utf-8") as source_file:
                source_file.write(restored)

        atexit.register(restore)
        print("patch_bt_mem: arduino-esp32 BT usage header omitted from NimBLE")
