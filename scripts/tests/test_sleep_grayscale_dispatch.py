"""Pin which panels keep the original (0/85/170/255) sleep-image dither thresholds.

The dispatch lives in src/util/SleepGrayscale.h, shared by the book-cover and
image-viewer cover paths, so this extracts it and runs it against a stub
HalDisplay -- the same "production snippet plus seams" shape as
test_gfx_fill_clip.py. Every (controller, geometry) row below is a real panel or
a real registration hazard: the 800x480 and 480x800 rows are the X4 family's
glass in both scan orders, 792x528 is the X3 (a UC8279 that must not be caught)
and 768x552 is the EEGO A4's UC8279C.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
GRAYSCALE_SOURCE = ROOT / "src/util/SleepGrayscale.h"

# Mirrors BoardConfig::DisplayController in the SDK header. The helper switches
# on these names, so only the set of names has to stay in step.
CONTROLLERS = ["SSD1677", "UC8253", "ED2208", "LgfxEpd", "IT8951", "UC8279", "UC8179", "UC8279C"]

# Every (width, height) pair worth asking about: the real panels, plus the
# portrait registration of the same glass.
GEOMETRIES = [(800, 480), (480, 800), (792, 528), (528, 792), (768, 552), (960, 540), (416, 240),
              (1216, 684)]

STUBS = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace BoardConfig {
enum class DisplayController : uint8_t {
  SSD1677 = 0,
  UC8253 = 2,
  ED2208 = 3,
  LgfxEpd = 4,
  IT8951 = 5,
  UC8279 = 6,
  UC8179 = 7,
  UC8279C = 8,
};
}  // namespace BoardConfig

// Stand-in for lib/hal/HalDisplay.h: the helper only reads the controller and
// the panel's native scan geometry off it.
struct HalDisplay {
  using Controller = BoardConfig::DisplayController;
  Controller controller = Controller::SSD1677;
  uint16_t width = 800;
  uint16_t height = 480;
  Controller getController() const { return controller; }
  uint16_t getDisplayWidth() const { return width; }
  uint16_t getDisplayHeight() const { return height; }
};
HalDisplay display;
'''


def expected(controller, width, height):
    """The thresholds each panel family should get, spelled out independently of the C++."""
    if controller in ("SSD1677", "UC8179", "UC8279"):
        # The X4-family 800x480 glass, whichever way round its native scan is registered.
        return (width, height) in ((800, 480), (480, 800))
    return False  # UC8253 / UC8279C / ED2208 / IT8951 / LgfxEpd keep the legacy tuning.


def method(source, name):
    """Slice one out-of-line function definition, braces balanced."""
    start = source.index(name)
    brace = source.index("{", start)
    depth = 0
    end = brace
    while True:
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end + 1]
        end += 1


class SleepGrayscaleDispatchTest(unittest.TestCase):
    def test_controller_and_geometry_select_the_thresholds(self):
        source = GRAYSCALE_SOURCE.read_text()
        helper = method(source, "bool useOriginalGrayscaleThresholds()")
        # Fail loudly instead of testing a stale slice if the helper is renamed or reshaped.
        self.assertIn("display.getDisplayWidth()", helper)
        for name in ("SSD1677", "UC8279", "UC8179"):
            self.assertIn("HalDisplay::Controller::" + name, helper)

        cases = [(controller, width, height) for controller in CONTROLLERS
                 for width, height in GEOMETRIES]
        driver = "\nconst BoardConfig::DisplayController kControllers[] = {%s};\n" % "".join(
            "\n    BoardConfig::DisplayController::" + name + "," for name in CONTROLLERS) + r'''
int main(int argc, char** argv) {
  // One verdict per line, for argv triplets of controller index / width / height.
  for (int i = 1; i + 2 < argc; i += 3) {
    display.controller = kControllers[std::atoi(argv[i])];
    display.width = static_cast<uint16_t>(std::atoi(argv[i + 1]));
    display.height = static_cast<uint16_t>(std::atoi(argv[i + 2]));
    std::printf("%d\n", useOriginalGrayscaleThresholds() ? 1 : 0);
  }
}
'''
        with tempfile.TemporaryDirectory(prefix="sleep-grayscale-") as directory:
            cpp = Path(directory) / "check.cpp"
            exe = Path(directory) / ("check.exe" if os.name == "nt" else "check")
            cpp.write_text(STUBS + "\n" + helper + "\n" + driver)
            subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o",
                            str(exe)], check=True)
            args = [str(exe)]
            for controller, width, height in cases:
                args += [str(CONTROLLERS.index(controller)), str(width), str(height)]
            verdicts = subprocess.run(args, check=True, capture_output=True,
                                      text=True).stdout.split()

        self.assertEqual(len(verdicts), len(cases))
        for (controller, width, height), verdict in zip(cases, verdicts):
            with self.subTest(controller=controller, width=width, height=height):
                self.assertEqual(verdict, "1" if expected(controller, width, height) else "0")


if __name__ == "__main__":
    unittest.main()
