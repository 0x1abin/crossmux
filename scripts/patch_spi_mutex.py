from pathlib import Path

Import("env")


# ---------------------------------------------------------------------------
# Cross-core SMP priority-inheritance workaround.
#
# The Arduino esp32 core guards each shared peripheral (SPI bus, UART, I2C/Wire)
# with a priority-inheriting mutex (xSemaphoreCreateMutex). The render task
# (core 1) and the main task (core 0) both drive these peripherals (SD-card SPI,
# e-ink SPI, debug UART, touch I2C), so the same mutex is taken on one core and
# given on the other.
#
# ESP-IDF SMP FreeRTOS then trips, on the give path:
#   assert failed: xTaskPriorityDisinherit tasks.c (pxTCB == pxCurrentTCBs[core])
# which aborts the render task and leaves the panel frozen/black. This is the
# root cause of both the WeRead entry crash and the reader black-screen.
#
# Priority inheritance is only a latency optimization, not required for
# correctness. A counting semaphore (max=1, initial=1) keeps mutual exclusion
# but has no priority-inheritance give path, so cross-core take/give is safe.
# SD access is additionally serialized by HalStorage::storageMutex, so losing
# the redundant SPI priority inheritance is safe.
# ---------------------------------------------------------------------------

MUTEX_CREATE = "xSemaphoreCreateMutex()"
SEMAPHORE_CREATE = "xSemaphoreCreateCounting(1, 1)"

# Files (relative to the framework package dir) whose mutex creation we replace.
TARGETS = [
    "cores/esp32/esp32-hal-spi.c",
    "libraries/SPI/src/SPI.cpp",
    "cores/esp32/esp32-hal-uart.c",
    "cores/esp32/HardwareSerial.cpp",
    "libraries/Wire/src/Wire.cpp",
]


def patch_file(path: Path) -> bool:
    text = path.read_text()
    if MUTEX_CREATE not in text:
        return False
    visited = text.count(MUTEX_CREATE)
    new_text = text.replace(MUTEX_CREATE, SEMAPHORE_CREATE)
    path.write_text(new_text)
    print(f"  patched {path.name}: {visited} x xSemaphoreCreateMutex() -> counting semaphore")
    return True


# Locate the framework package reliably. `env.PioPlatform()` resolves the exact
# installed package dir regardless of OS home / platformio home differences.
root = None
try:
    pkg_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    if pkg_dir and Path(pkg_dir).is_dir():
        root = Path(pkg_dir)
except Exception as e:  # noqa: BLE001
    print(f"WARNING: get_package_dir failed ({e}); falling back to path probing")

if root is None:
    import os
    for var in ("PLATFORMIO_CORE_DIR", "HOME", "USERPROFILE"):
        v = env.subst("$%s" % var) or os.environ.get(var)
        if not v:
            continue
        cand = Path(v) / "packages" / "framework-arduinoespressif32"
        if cand.is_dir():
            root = cand
            break

if root is None:
    print("WARNING: could not locate framework-arduinoespressif32; SPI mutex patch skipped.")
    exit(0)

print(f"Patching framework-arduinoespressif32 at: {root}")
patched_any = False
for rel in TARGETS:
    p = root / rel
    if not p.is_file():
        print(f"  skip (missing): {rel}")
        continue
    if patch_file(p):
        patched_any = True
    else:
        print(f"  skip (no mutex create / already patched): {rel}")

if not patched_any:
    print("WARNING: no peripheral mutex file was patched; check the framework path.")