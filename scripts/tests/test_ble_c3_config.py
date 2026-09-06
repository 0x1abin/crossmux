"""Check resolved profile isolation and C3 overrides against conflicting core defaults."""

import configparser
import os
from pathlib import Path
import re
import runpy
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class BleC3ConfigTest(unittest.TestCase):
    def test_power_saving_protects_c3_host_lifetime_and_recovers(self):
        source = (ROOT / "lib/hal/HalPowerManager.cpp").read_text()
        method = source.split("void HalPowerManager::setPowerSaving(bool enabled) {", 1)[1]
        method = "void HalPowerManager::setPowerSaving(bool enabled) {" + method.split(
            "void HalPowerManager::startDeepSleep", 1)[0]
        harness = r'''
#include <cassert>
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
constexpr int WIFI_MODE_NULL = 0;
struct { int mode = 0; int getMode() { return mode; } } WiFi;
struct { bool running = false; bool isRunning() { return running; } } BleHid;
unsigned frequency = 160;
unsigned getCpuFrequencyMhz() { return frequency; }
bool setCpuFrequencyMhz(unsigned value) { frequency = value; return true; }
struct HalPowerManager {
    enum LockMode { None, NormalSpeed };
    int normalFreq = 160;
    bool isLowPower = false;
    LockMode currentLockMode = None;
    static constexpr int LOW_POWER_FREQ = 10;
    void setPowerSaving(bool enabled);
};
'''
        checks = r'''
int main() {
    HalPowerManager power;
    power.setPowerSaving(true);
    assert(frequency == 10);
    BleHid.running = true;
    power.setPowerSaving(true);
#if CONFIG_IDF_TARGET_ESP32C3 && FREEINK_CAP_BLE_HID_HOST
    assert(frequency == 160);
    power.setPowerSaving(true); // Idle while scanning/reconnecting stays protected.
    assert(frequency == 160);
#else
    assert(frequency == 10);
#endif
    BleHid.running = false;
    power.setPowerSaving(true);
    assert(frequency == 10);
    WiFi.mode = 1;
    power.setPowerSaving(true);
    assert(frequency == 160);
    WiFi.mode = 0;
    power.currentLockMode = HalPowerManager::NormalSpeed;
    power.setPowerSaving(true);
    assert(frequency == 160);
    power.currentLockMode = HalPowerManager::None;
    power.setPowerSaving(true);
    assert(frequency == 10);
    power.setPowerSaving(false);
    assert(frequency == 160);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "power.cpp"
            cpp.write_text(harness + method + checks)
            exe = Path(directory) / "power"
            for c3, ble in ((1, 1), (1, 0), (0, 1)):
                subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                    "-std=c++17", f"-DCONFIG_IDF_TARGET_ESP32C3={c3}",
                    f"-DFREEINK_CAP_BLE_HID_HOST={ble}", str(cpp), "-o", str(exe),
                ], check=True, capture_output=True)
                subprocess.run([str(exe)], check=True)

    def test_heap_diagnostic_batches_unordered_heaps_and_logs_outside_heap_lock(self):
        source = (ROOT / "src/BleInput.cpp").read_text()
        method = source.split("void logInternalHeapBlocks() {", 1)[1].split("\n#endif", 1)[0]
        method = "void logInternalHeapBlocks() {" + method
        harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>
struct walker_heap_into_t { intptr_t start, end; };
struct walker_block_info_t { void* ptr; size_t size; bool used; };
constexpr int MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2;
bool walking=false;
std::vector<uintptr_t> addresses;
void testLog(const char*, const char*, void* heap, void* ptr, unsigned size, unsigned used) {
  assert(!walking);
  const auto address=reinterpret_cast<uintptr_t>(ptr);
  assert(address > reinterpret_cast<uintptr_t>(heap));
  assert(size==32 && used==(address/64)%2);
  addresses.push_back(address);
}
#define LOG_INF(...) testLog(__VA_ARGS__)
void heap_caps_walk(int caps, bool (*callback)(walker_heap_into_t, walker_block_info_t, void*), void* ctx) {
  assert(caps==(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
  walking=true;
  // Registration order is not address order. Include more than one batch per heap.
  for(intptr_t base : {0x3000, 0x1000, 0x2000}) {
    for(int i=1;i<=19;++i) {
      uintptr_t address=base+64*i;
      if(!callback({base, base+0x1000}, {reinterpret_cast<void*>(address),32,bool((address/64)%2)},ctx)) break;
    }
  }
  walking=false;
}
'''
        checks = r'''
int main() {
  logInternalHeapBlocks();
  assert(addresses.size()==57);
  unsigned index=0;
  for(uintptr_t base : {0x1000, 0x2000, 0x3000})
    for(int i=1;i<=19;++i) assert(addresses[index++]==base+64*i);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / "heap.cpp"
            cpp.write_text(harness + method + checks)
            exe = Path(directory) / "heap"
            result = subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", str(cpp), "-o", str(exe)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(exe)], check=True)

    def test_c3_candidate_is_not_enabled_in_default_or_releases(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini")

        def resolve(section, key):
            if section == "sysenv":
                return os.environ.get(key, "")
            value = config[section].get(key)
            if value is None:
                for parent in config[section].get("extends", "").split(","):
                    if parent.strip():
                        inherited = resolve(parent.strip(), key)
                        if inherited:
                            return inherited
                return ""
            return re.sub(r"\$\{([^{}]+)\.([^{}.]+)\}",
                          lambda match: resolve(*match.groups()), value)

        flags = resolve("c3_ble", "build_flags")
        self.assertIn("-DFREEINK_CAP_BLE_HID_HOST=1", flags)
        self.assertNotIn("CROSSPOINT_BLE_HOST_PSRAM", flags)
        self.assertNotIn("xTaskCreatePinnedToCore", flags)
        self.assertIn("h2zero/NimBLE-Arduino @ 2.3.8", resolve("c3_ble", "lib_deps"))
        self.assertIn("patch_ble_keyboard_host.py", resolve("c3_ble", "extra_scripts"))
        self.assertIn("configure_nimble_psram.py", resolve("c3_ble", "extra_scripts"))
        self.assertIn("post:scripts/configure_c3_ble_controller.py", resolve("c3_ble", "extra_scripts"))
        self.assertEqual(resolve("c3_ble", "custom_nimble_config"), "src/platform/NimbleC3Config.h")
        self.assertIn("CONFIG_BT_CONTROLLER_ONLY=y", resolve("c3_ble", "custom_sdkconfig"))
        self.assertIn("CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y", resolve("c3_ble", "custom_sdkconfig"))
        self.assertIn("CONFIG_BT_NIMBLE_ENABLED=n", resolve("c3_ble", "custom_sdkconfig"))
        for section in ("env:default", "env:gh_release", "env:gh_release_rc", "env:slim", "env:simulator"):
            with self.subTest(section=section):
                self.assertNotIn("FREEINK_CAP_BLE_HID_HOST", resolve(section, "build_flags"))
                self.assertNotIn("NimBLE-Arduino", resolve(section, "lib_deps"))
                self.assertFalse(resolve(section, "custom_nimble_config"))
                self.assertNotIn("configure_c3_ble_controller.py", resolve(section, "extra_scripts"))
        self.assertIn("uint8_t bluetoothEnabled = 0;", (ROOT / "src/CrossPointSettings.h").read_text())

    def test_flash_controller_link_uses_matching_archive_without_changing_packages(self):
        class BuildEnv(dict):
            def BoardConfig(self):
                return {"build.mcu": self["mcu"]}

            def GetProjectOption(self, name, default):
                return self.get(name, default)

            def PioPlatform(self):
                return self

            def get_package_dir(self, name):
                self.package = name
                return self["package_dir"]

            def File(self, path):
                return Path(path)

            def Replace(self, **values):
                self.update(values)

        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "components/bt/controller/lib_esp32c3_family/esp32c3/libbtdm_app_flash.a"
            archive.parent.mkdir(parents=True)
            archive.write_bytes(b"controller")
            env = BuildEnv(mcu="esp32c3", package_dir=directory,
                           custom_sdkconfig="CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y",
                           LIBS=["-lbt", "-lbtdm_app", "-lc"],
                           LINKFLAGS=["-Wl,--gc-sections", "-T", "esp32c3.rom.ld",
                                      "-T", "esp32c3.rom.bt_funcs.ld",
                                      "-T", "/sdk/esp32c3.rom.eco3_bt_funcs.ld",
                                      "-T", "esp32c3.rom.eco7_bt_funcs.ld",
                                      "-T", "esp32c3.rom.eco3.ld"])
            script = str(ROOT / "scripts/configure_c3_ble_controller.py")
            runpy.run_path(script, init_globals={"env": env, "Import": lambda _: None})
            self.assertEqual(env["LIBS"], ["-lbt", archive, "-lc"])
            self.assertEqual(env["LINKFLAGS"], ["-Wl,--gc-sections", "-T", "esp32c3.rom.ld",
                                                 "-T", "esp32c3.rom.eco3.ld"])
            self.assertEqual(env.package, "framework-espidf")
            self.assertEqual(archive.read_bytes(), b"controller")
            env["mcu"] = "esp32s3"
            with self.assertRaisesRegex(RuntimeError, "C3-only"):
                runpy.run_path(script, init_globals={"env": env, "Import": lambda _: None})
            env["mcu"] = "esp32c3"
            env["custom_sdkconfig"] = ""
            with self.assertRaisesRegex(RuntimeError, "matching custom core"):
                runpy.run_path(script, init_globals={"env": env, "Import": lambda _: None})

    def test_header_overrides_core_defaults_only_on_c3(self):
        expected = {
            "MEM_ALLOC_MODE_INTERNAL": 1, "ROLE_CENTRAL": 1, "ROLE_OBSERVER": 1,
            "ROLE_PERIPHERAL": 0, "ROLE_BROADCASTER": 0, "MAX_CONNECTIONS": 1,
            "MAX_BONDS": 4, "MAX_CCCDS": 2, "ATT_PREFERRED_MTU": 23,
            "MSYS1_BLOCK_COUNT": 6, "MSYS_1_BLOCK_COUNT": 6, "MSYS_2_BLOCK_COUNT": 6,
            "MSYS_1_BLOCK_SIZE": 256, "MSYS_2_BLOCK_SIZE": 320,
            "TRANSPORT_ACL_FROM_LL_COUNT": 6,
            "TRANSPORT_EVT_COUNT": 12,
        }
        with tempfile.TemporaryDirectory() as directory:
            stub = Path(directory) / "sdkconfig.h"
            (Path(directory) / "nimconfig.h").write_text(
                "#pragma once\n#undef CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT\n"
                "#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT 30\n")
            stub.write_text("#pragma once\n" + "\n".join(
                f"#define CONFIG_BT_NIMBLE_{key} 99" for key in expected
            ) + "\n#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1\n")
            for c3, enabled in ((1, 1), (0, 1), (1, 0)):
                command = shlex.split(os.environ.get("CXX", "c++")) + [
                    "-E", "-dM", "-x", "c++", "-I", directory,
                    f"-DCONFIG_IDF_TARGET_ESP32C3={c3}", f"-DFREEINK_CAP_BLE_HID_HOST={enabled}",
                    "-include", str(ROOT / "src/platform/NimbleC3Config.h"), "-",
                ]
                result = subprocess.run(command, input="", text=True, capture_output=True, check=True)
                macros = dict(re.findall(r"^#define (\w+) (.+)$", result.stdout, re.MULTILINE))
                for key, value in expected.items():
                    self.assertEqual(macros[f"CONFIG_BT_NIMBLE_{key}"], str(value if c3 and enabled else 99))
                self.assertEqual("CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL" in macros, not (c3 and enabled))


if __name__ == "__main__":
    unittest.main()
