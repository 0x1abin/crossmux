"""Compile the About content rectangle against already-reserved safe areas."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class AboutLayoutTest(unittest.TestCase):
    def test_content_uses_remaining_safe_height(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / "src/activities/settings/SettingsActivity.cpp").read_text()
        start = source.index("  Rect contentRect() const {")
        method = source[start:source.index("\n  }", start) + len("\n  }")]
        program = r"""
#include <cassert>
#include <initializer_list>
struct Rect { int x, y, width, height; };
struct Metrics { int topPadding, headerHeight, buttonHintsHeight; };
struct UITheme {
  Metrics metrics{0, 66, 40};
  Rect safe{0, 0, 480, 760};
  static UITheme& getInstance() { static UITheme theme; return theme; }
  const Metrics& getMetrics() const { return metrics; }
  Rect getScreenSafeArea(int, bool, bool) const { return safe; }
};
struct About {
  int renderer = 0;
METHOD
};
int main() {
  auto& theme = UITheme::getInstance();
  // Portrait, inverted portrait, both landscapes, and no button hints.
  for (const Rect safe : {Rect{0,0,480,760}, Rect{0,40,480,760},
                          Rect{40,0,760,480}, Rect{0,0,760,480},
                          Rect{0,0,480,800}}) {
    theme.safe = safe;
    const Rect content = About{}.contentRect();
    assert(content.x == safe.x && content.width == safe.width);
    assert(content.y == safe.y + 66);
    assert(content.y + content.height == safe.y + safe.height);
  }
  theme.safe = {0, 0, 480, 760};
  assert(About{}.contentRect().height / 66 == 10);
}
""".replace("METHOD", method)
        with tempfile.TemporaryDirectory(prefix="about-layout-") as directory:
            cpp = Path(directory) / "test.cpp"
            exe = Path(directory) / "test"
            cpp.write_text(program)
            subprocess.run(["c++", "-std=c++17", str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
