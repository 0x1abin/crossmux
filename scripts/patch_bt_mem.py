"""
PlatformIO pre-build script: make the arduino-esp32 core define _btLibraryInUse
in a BLE-controller-only build.

The core header cores/esp32/esp32-hal-bt-mem.h emits a constructor
(_setBtLibraryInUse) that references `_btLibraryInUse` whenever BLE hardware is
present, but the core only DEFINES that symbol (in esp32-hal-bt.c) when an IDF
host stack -- CONFIG_BLUEDROID_ENABLED or CONFIG_NIMBLE_ENABLED -- is enabled.

CrossPoint runs the ESP-IDF BLE *controller* only, with NimBLE-Arduino supplying
the host (both IDF host stacks are disabled in custom_sdkconfig to avoid duplicate
npl_freertos_* symbols). In that configuration the core never defines the symbol,
so NimBLE's constructor link-fails with "undefined reference to `_btLibraryInUse'"
-- and it fails in TWO links that application source cannot reach:
  1. the final firmware link, and
  2. the arduino-lib-builder core-rebuild's dummy firmware (custom_sdkconfig),
     which links NimBLE but none of our src/.

Fix: turn the header's `extern bool _btLibraryInUse;` DECLARATION into a weak
DEFINITION. Every translation unit that includes the header (NimBLE) then provides
the symbol itself; the linker merges the duplicate weak defs, and the core's own
strong definition still wins whenever a host stack is actually enabled. This is why
it must live in the header rather than in our src -- only the header is seen by
NimBLE's TU in every link.

Idempotent text edit, re-applied every build (the framework package is a plain
extracted tarball, not a git checkout, so the git-apply pattern used for JPEGDEC
does not apply here; and PlatformIO may re-extract the package on reinstall).
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import sys

EXTERN_DECL = "extern bool _btLibraryInUse;"
MARKER = "patch_bt_mem.py"
WEAK_DEF = (
    "__attribute__((weak)) bool _btLibraryInUse = false;  /* CrossPoint " + MARKER + " */"
)


def patch_bt_mem(env):
    fw_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")  # noqa: F821
    if not fw_dir:
        return
    header = os.path.join(fw_dir, "cores", "esp32", "esp32-hal-bt-mem.h")
    if not os.path.isfile(header):
        return

    with open(header, "r") as f:
        text = f.read()

    if MARKER in text:
        return  # already patched this checkout of the package

    if EXTERN_DECL not in text:
        # Upstream header changed shape -- do not blindly rewrite it. Warn loudly:
        # if the controller-only link then fails on _btLibraryInUse, this script
        # needs updating for the new arduino-esp32 version.
        sys.stderr.write(
            "WARNING: patch_bt_mem.py could not find '%s' in %s; "
            "_btLibraryInUse may be left undefined for the controller-only build.\n"
            % (EXTERN_DECL, header)
        )
        return

    text = text.replace(EXTERN_DECL, WEAK_DEF, 1)
    with open(header, "w") as f:
        f.write(text)
    print("Patched esp32-hal-bt-mem.h: _btLibraryInUse now defined weakly")


patch_bt_mem(env)  # noqa: F821
