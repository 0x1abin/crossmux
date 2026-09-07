import configparser
import fnmatch
from pathlib import Path
import runpy
import unittest


class NimblePsramMiddlewareTest(unittest.TestCase):
    def test_all_s3_ble_targets_share_psram_without_changing_sticky_core(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(Path(__file__).resolve().parents[2] / "platformio.ini")
        devices = ("sticky", "x4pro", "x4c", "papermono", "eego_a4", "murphy_m4", "waveshare_epaper_397")
        for device in devices:
            hardware = f"{device}_hardware"
            for option in ("lib_deps", "extra_scripts", "build_flags"):
                self.assertIn(f"${{s3_ble_psram.{option}}}", config[hardware][option])
            for name in config.sections():
                if name.startswith("env:") and config[name].get("extends") == hardware:
                    with self.subTest(target=name):
                        self.assertIn(f"${{{hardware}.build_flags}}", config[name]["build_flags"])
                        self.assertNotIn("lib_deps", config[name])
                        self.assertNotIn("extra_scripts", config[name])
        self.assertEqual(
            config["sticky_hardware"]["custom_sdkconfig"].split(),
            ["${firmware_tuned.custom_sdkconfig}", "${s3_ble_controller.custom_sdkconfig}"],
        )
        self.assertNotIn("BOARD_HAS_PSRAM", config["sticky_hardware"]["build_flags"])
        self.assertIn("CONFIG_ARDUINO_LOOP_STACK_SIZE=16384", config["eego_a4_hardware"]["custom_sdkconfig"])
        self.assertIn("CONFIG_BT_CONTROLLER_ONLY=y", config["s3_ble_controller"]["custom_sdkconfig"])
        self.assertIn("CONFIG_BT_NIMBLE_ENABLED=n", config["s3_ble_controller"]["custom_sdkconfig"])

    def test_generic_base_and_simulators_do_not_inherit_radio_configuration(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(Path(__file__).resolve().parents[2] / "platformio.ini")
        for name in ("base", "firmware_tuned", "env:simulator", "env:simulator_x3",
                     "env:simulator_eego_a4", "env:simulator_murphy_m4"):
            with self.subTest(profile=name):
                for value in config[name].values():
                    self.assertNotIn("s3_ble", value)
                    self.assertNotIn("FREEINK_CAP_BLE_HID_HOST", value)
                    self.assertNotIn("CROSSPOINT_BLE_HOST_PSRAM", value)
                    self.assertNotIn("NimBLE-Arduino", value)

    def test_only_nimble_sources_are_overridden_and_next_middleware_gets_a_node(self):
        root = Path(__file__).resolve().parents[2]

        class BuildEnv(dict):
            def GetProjectOption(self, name, default):
                return self.get(name, default)

            def subst(self, value):
                return str(root) if value == "$PROJECT_DIR" else value

            def AddBuildMiddleware(self, callback, pattern):
                self.callback = callback
                self.pattern = pattern

            def Object(self, node, **kwargs):
                self.object_flags = kwargs["CCFLAGS"]
                return [node]

            def Depends(self, node, config):
                self.dependency = (node, config)

        env = BuildEnv(CCFLAGS=["-Os"])
        runpy.run_path(
            str(root / "scripts/configure_nimble_psram.py"),
            init_globals={"env": env, "Import": lambda _: None},
        )
        self.assertTrue(fnmatch.fnmatch(".pio/libdeps/eego/NimBLE-Arduino/src/nimble/mem.c", env.pattern))
        self.assertFalse(fnmatch.fnmatch("src/BleInput.cpp", env.pattern))
        self.assertFalse(fnmatch.fnmatch("framework/cores/esp32/esp32-hal-bt.c", env.pattern))
        node = object()
        self.assertIs(env.callback(env, node), node)
        self.assertEqual(env.dependency, (node, str(root / "src/platform/NimblePsramConfig.h")))
        self.assertEqual(env["CCFLAGS"], ["-Os"])
        self.assertEqual(
            env.object_flags, ["-Os", "-include", str(root / "src/platform/NimblePsramConfig.h")]
        )
        env["custom_nimble_config"] = "src/platform/NimbleC3Config.h"
        runpy.run_path(
            str(root / "scripts/configure_nimble_psram.py"),
            init_globals={"env": env, "Import": lambda _: None},
        )
        self.assertIs(env.callback(env, node), node)
        self.assertEqual(env.dependency, (node, str(root / "src/platform/NimbleC3Config.h")))


if __name__ == "__main__":
    unittest.main()
