import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "metalio_plus_idf.py"
spec = importlib.util.spec_from_file_location("metalio_plus_idf", SCRIPT)
idf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(idf)


class ProjectCmakeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="metalio-plus-cmake-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / "config").mkdir()
        (self.root / "config/metalio_plus_project.cmake").write_text("project(metalio_plus)\n")

    def test_install_is_idempotent_and_recovers_after_legacy_core_cleanup(self):
        target = self.root / "CMakeLists.txt"
        idf.prepare_project(self.root)
        first = target.read_text()
        stamp = target.stat().st_mtime_ns
        idf.prepare_project(self.root)
        self.assertEqual(target.stat().st_mtime_ns, stamp)
        target.unlink()  # Older C3 HybridCompile removes the root CMake file.
        idf.prepare_project(self.root)
        self.assertEqual(target.read_text(), first)

    def test_platform_generated_boilerplate_can_be_replaced(self):
        target = self.root / "CMakeLists.txt"
        target.write_text('cmake_minimum_required(VERSION 3.16.0)\n'
                          'include($ENV{IDF_PATH}/tools/cmake/project.cmake)\n'
                          f'project({self.root.name})\n')
        idf.prepare_project(self.root)
        self.assertIn("project(metalio_plus)", target.read_text())

    def test_unknown_user_cmake_is_preserved(self):
        target = self.root / "CMakeLists.txt"
        original = "# User project\nproject(custom)\n"
        target.write_text(original)
        with self.assertRaises(RuntimeError):
            idf.prepare_project(self.root)
        self.assertEqual(target.read_text(), original)

    def test_generated_s31_root_preserves_later_legacy_default_selection(self):
        template = SCRIPT.parent.parent / "config/metalio_plus_project.cmake"
        (self.root / "config/metalio_plus_project.cmake").write_text(template.read_text())
        idf.prepare_project(self.root)
        # Execute the real generated CMake selection; stub only the SDK import
        # and project() so this regression needs no C3/S3 toolchain/downloads.
        cmake = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        self.assertTrue(cmake, "Run via CTest or set CMAKE_COMMAND to the installed CMake")
        generated = (self.root / "CMakeLists.txt").read_text()
        for chip in ("esp32s31", "esp32c3", "esp32s3"):
            expected = str(self.root / "config/metalio_plus_sdkconfig.defaults") if chip == "esp32s31" else "sdkconfig.defaults"
            script = self.root / f"{chip}.cmake"
            script.write_text(f'''set(IDF_TARGET "{chip}")
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
function(include)
endfunction()
function(project)
 if(NOT SDKCONFIG_DEFAULTS STREQUAL "{expected}")
  message(FATAL_ERROR "Unexpected defaults for {chip}: ${{SDKCONFIG_DEFAULTS}}")
 endif()
endfunction()
''' + generated)
            result = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_reader_wraps_are_removed_before_idf_clones_bootloader_flags(self):
        original = ['-DNAME=\\"Reader\\"', idf.READER_WRAPS, '-Wl,--gc-sections']
        self.assertEqual(idf.without_reader_wraps(original), ['-DNAME=\\"Reader\\"', '-Wl,--gc-sections'])
        self.assertEqual(original[1], idf.READER_WRAPS)
        text = '-Os ' + idf.READER_WRAPS + ' -Wl,--gc-sections'
        self.assertNotIn('--wrap=', idf.without_reader_wraps(text))
        self.assertIn('-Wl,--gc-sections', idf.without_reader_wraps(text))


if __name__ == "__main__":
    unittest.main()
