import hashlib
from pathlib import Path
import runpy
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "patch_pioarduino_cache.py"
ORIGINAL_CHECK = (
    "flag_any_custom_sdkconfig = (FRAMEWORK_LIB_DIR is not None and \n"
    '                            exists(str(Path(FRAMEWORK_LIB_DIR) / "sdkconfig")))'
)


class PioarduinoCacheTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.builder = self.root / "builder/frameworks/arduino.py"
        self.builder.parent.mkdir(parents=True)
        self.builder.write_text(ORIGINAL_CHECK)
        self.chip = self.root / "esp32s3"
        self.chip.mkdir()
        self.config = self.chip / "sdkconfig"
        self.original = self.chip / "sdkconfig.orig"
        self.request = self.chip / "sdkconfig.crosspoint"
        self.config.write_text("original template")

    def run_script(self, requested):
        root = self.root

        class BuildEnv:
            def PioPlatform(self):
                return self

            def get_dir(self):
                return str(root)

            def get_package_dir(self, name):
                return str(root)

            def GetProjectOption(self, name, default):
                return requested

            def BoardConfig(self):
                return {"build.mcu": "esp32s3"}

            def subst(self, value):
                return str(root)

        runpy.run_path(str(SCRIPT), init_globals={"env": BuildEnv(), "Import": lambda _: None})

    def seed_custom(self):
        self.config.write_text("custom binary configuration")
        self.original.write_text("original template")
        self.request.write_text("CONFIG_A=y\n")

    def test_first_build_keeps_original_template(self):
        for requested in ("", "CONFIG_A=y"):
            with self.subTest(requested=requested):
                self.run_script(requested)
                self.assertEqual(self.config.read_text(), "original template")
                self.assertFalse(self.original.exists())
                self.assertEqual(self.request.read_text(), requested + "\n")

    def test_matching_custom_reuses_libraries_and_restores_marker(self):
        self.seed_custom()
        defaults = self.root / "sdkconfig.defaults"
        defaults.write_text("# old marker\nCONFIG_KEEP=y\n")
        self.run_script(" CONFIG_A=y\n")
        marker = hashlib.md5(b"CONFIG_A=yesp32s3").hexdigest()[:16]
        self.assertEqual(defaults.read_text(), f"# TASMOTA__{marker}\nCONFIG_KEEP=y\n")
        self.assertEqual(self.config.read_text(), "custom binary configuration")
        self.assertTrue(self.original.exists())

    def test_changed_custom_restores_template_for_rebuild(self):
        self.seed_custom()
        self.run_script("CONFIG_B=y")
        self.assertEqual(self.config.read_text(), "original template")
        self.assertFalse(self.original.exists())
        self.assertEqual(self.request.read_text(), "CONFIG_B=y\n")

    def test_custom_to_prebuilt_keeps_reinstall_marker(self):
        self.seed_custom()
        for requested in ("", " \n"):
            with self.subTest(requested=requested):
                self.run_script(requested)
                self.assertEqual(self.original.read_text(), "original template")
                self.assertEqual(self.config.read_text(), "custom binary configuration")
                self.assertEqual(self.request.read_text(), "\n")

    def test_repeated_prebuilt_build_does_not_create_custom_marker(self):
        self.run_script("")
        self.run_script("")
        self.assertFalse(self.original.exists())
        self.assertEqual(self.config.read_text(), "original template")


if __name__ == "__main__":
    unittest.main()
