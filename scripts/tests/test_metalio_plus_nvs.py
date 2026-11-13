"""Compile the real Plus NVS policy; fake only IDF init and logging."""

import importlib.util
import os
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NvsPolicyTest(unittest.TestCase):
    def test_real_policy_preserves_results_and_blocks_arduino_erase_triggers(self):
        with tempfile.TemporaryDirectory(prefix="metalio-plus-nvs-") as tmp:
            work = Path(tmp)
            (work / "esp_err.h").write_text("""#pragma once
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_NO_MEM=0x101, ESP_ERR_INVALID_STATE=0x103;
""")
            (work / "nvs_flash.h").write_text("""#pragma once
#include <esp_err.h>
constexpr int ESP_ERR_NVS_NOT_FOUND=0x1102;
constexpr int ESP_ERR_NVS_NO_FREE_PAGES=0x110d;
constexpr int ESP_ERR_NVS_NEW_VERSION_FOUND=0x1110;
""")
            (work / "esp_log.h").write_text("""#pragma once
void recordError(const char*, const char*, ...);
#define ESP_LOGE(...) recordError(__VA_ARGS__)
""")
            harness = work / "harness.cpp"
            harness.write_text("""#include <cassert>
#include <nvs_flash.h>
static int result, calls, errors;
extern "C" int __real_nvs_flash_init() { ++calls; return result; }
extern "C" int __wrap_nvs_flash_init();
void recordError(const char*, const char*, ...) { ++errors; }
int main() {
  const int input[] = {0, 0x110d, 0x1110, 0x101, 0x1102, 0x103};
  const int expected[] = {0, 0x103, 0x103, 0x101, 0x1102, 0x103};
  for (unsigned i=0; i<6; ++i) {
    result=input[i]; calls=0; errors=0;
    assert(__wrap_nvs_flash_init()==expected[i]);
    assert(calls==1);
    assert(errors==((i==1 || i==2)?1:0));
  }
}
""")
            source = ROOT / "lib/hal/HalNvsPolicy.cpp"
            common = [os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror"]
            subprocess.run(common + ["-DFREEINK_DEVICE_METALIO_EINK4_PLUS=1", "-I" + str(work),
                                     str(source), str(harness), "-o", str(work / "test")], check=True)
            subprocess.run([str(work / "test")], check=True)
            obj = work / "legacy.o"
            subprocess.run(common + ["-DFREEINK_DEVICE_METALIO_EINK4_PLUS=0", "-c", str(source),
                                     "-o", str(obj)], check=True)
            symbols = subprocess.run(["nm", str(obj)], capture_output=True, text=True, check=True).stdout
            self.assertNotIn("__wrap_nvs_flash_init", symbols)

    def test_nvs_wrap_is_added_by_application_post_hook(self):
        script = ROOT / "scripts/metalio_plus_idf.py"
        spec = importlib.util.spec_from_file_location("metalio_plus_idf", script)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        class Environment(dict):
            def Append(self, **kwargs):
                self.setdefault("LINKFLAGS", []).extend(kwargs["LINKFLAGS"])

        env = Environment(METALIO_PLUS_READER_WRAPS=module.READER_WRAPS)
        runpy.run_path(str(ROOT / "scripts/metalio_plus_link.py"),
                       init_globals={"Import": lambda _: None, "env": env})
        self.assertEqual(env["LINKFLAGS"], [module.READER_WRAPS, "-Wl,--wrap=nvs_flash_init"])


if __name__ == "__main__":
    unittest.main()
