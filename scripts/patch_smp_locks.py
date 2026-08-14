"""
Build-time patch for SMP (ESP32-S3) safety.

The Arduino core's Wire/SPI/HWCDC/USBCDC/HardwareSerial guards use
priority-inheriting FreeRTOS mutexes (xSemaphoreCreateMutex). On the dual-core
S3 the Arduino loop task may migrate between cores between take() and give();
FreeRTOS SMP then trips the xTaskPriorityDisinherit assert
(pxTCB == pxCurrentTCBs[xPortGetCoreID()]) and reboots the device. The render
task is pinned to core 1 while loop() runs on whichever core it is scheduled
to, so any core-library lock shared between the two tasks can hit this —
observed on the EEGO A4 (ESP32-S3) when entering WeRead (heavy logging +
network + rendering on both tasks).

Replacing the mutexes with counting semaphores (max=1, initial=1) keeps the
mutual exclusion with NO priority inheritance, which is assert-free across
cores — the same approach ActivityManager/HalStorage/HalPowerManager use in
firmware code. The core-library call sites only use xSemaphoreTake/Give (no
GetMutexHolder / recursive variants), so the swap is API-compatible.

The patch is applied to the unpacked Arduino core package before compilation.
"""

import os

Import("env")  # noqa: F821  -- provided by PlatformIO at script load

# (relative path in the core package, source, replacement)
REPLACEMENTS = [
    ("libraries/Wire/src/Wire.cpp", "xSemaphoreCreateMutex()", "xSemaphoreCreateCounting(1, 1)"),
    ("libraries/SPI/src/SPI.cpp", "xSemaphoreCreateMutex()", "xSemaphoreCreateCounting(1, 1)"),
    ("cores/esp32/HWCDC.cpp", "xSemaphoreCreateMutex()", "xSemaphoreCreateCounting(1, 1)"),
    ("cores/esp32/USBCDC.cpp", "xSemaphoreCreateMutex()", "xSemaphoreCreateCounting(1, 1)"),
    ("cores/esp32/HardwareSerial.cpp", "xSemaphoreCreateMutex()", "xSemaphoreCreateCounting(1, 1)"),
    # esp32-hal-cpu.c's apb_change_lock is NOT gated by CONFIG_DISABLE_HAL_LOCKS
    # and is taken by both cores: the render task takes HalPowerManager::Lock
    # (setPowerSaving(false) -> setCpuFrequencyMhz) while the loop task
    # downclocks on idle — cross-core take/give tripped the assert.
    ("cores/esp32/esp32-hal-cpu.c", "xSemaphoreCreateMutex()", "xSemaphoreCreateCounting(1, 1)"),
]

# wolfSSL's port mutex (wc_port.c) — TLS runs on the loop task, but patch it
# too so any task that ever touches wolfCrypt cannot trip the assert.
WOLFSSL_REPLACEMENTS = [
    ("xSemaphoreCreateMutex()", "xSemaphoreCreateCounting(1, 1)"),
]


def patch_file(path, replacements):
    if not os.path.exists(path):
        print(f"[patch_smp_locks] skip (not found): {path}")
        return False
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()
    original = content
    for old, new in replacements:
        content = content.replace(old, new)
    if content != original:
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"[patch_smp_locks] patched {path}")
        return True
    print(f"[patch_smp_locks] already patched / no change: {path}")
    return False


try:
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
except Exception as exc:  # noqa: BLE001
    print(f"[patch_smp_locks] cannot resolve framework dir: {exc}")
    framework_dir = None

if framework_dir:
    for rel, old, new in REPLACEMENTS:
        patch_file(os.path.join(framework_dir, rel), [(old, new)])
else:
    print("[patch_smp_locks] WARNING: framework dir unresolved; nothing patched")

# wolfSSL lives in libdeps, not the framework package; locate it via the env.
try:
    libdeps_dir = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps", env["PIOENV"])
    wc_port = os.path.join(libdeps_dir, "Arduino-wolfSSL", "src", "wolfcrypt", "src", "wc_port.c")
    patch_file(wc_port, WOLFSSL_REPLACEMENTS)
except Exception as exc:  # noqa: BLE001
    print(f"[patch_smp_locks] wolfSSL patch skipped: {exc}")
