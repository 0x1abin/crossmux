"""Keep NimBLE-Arduino out of arduino-esp32's controller-only BT shim."""

import atexit
import os

Import("env")  # noqa: F821

source = os.path.join(
    env.subst("$PROJECT_LIBDEPS_DIR"),
    env.subst("$PIOENV"),
    "NimBLE-Arduino",
    "src",
    "NimBLEDevice.cpp",
)
include = '#   include "esp32-hal-bt-mem.h"'
patched = "// CrossMux controller-only build: BT usage flag supplied by application"

if os.path.isfile(source):
    with open(source, encoding="utf-8") as source_file:
        original = source_file.read()
    restored = original.replace(patched, include, 1)
    if include in original or patched in original:
        with open(source, "w", encoding="utf-8") as source_file:
            source_file.write(restored.replace(include, patched, 1))

        def restore():
            with open(source, "w", encoding="utf-8") as source_file:
                source_file.write(restored)

        atexit.register(restore)
        print("patch_bt_mem: omitted arduino BT usage header from NimBLE")
