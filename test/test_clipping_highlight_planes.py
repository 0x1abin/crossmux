"""Compile the production highlight and render-pass callbacks against a host renderer."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text()
fill = "const auto fillHighlight =" + source.split("const auto fillHighlight =", 1)[1].split("\n  uint16_t pageWordIndex", 1)[0]
render_page = "const auto renderPage =" + source.split("const auto renderPage =", 1)[1].split("\n  };", 1)[0] + "\n  };"
with_guides = "const auto renderPageWithGuideLines =" + source.split("const auto renderPageWithGuideLines =", 1)[1].split("\n  };", 1)[0] + "\n  };"
render_gray = "auto renderGrayscalePass =" + source.split("auto renderGrayscalePass =", 1)[1].split("\n  };", 1)[0] + "\n  };"
harness = r"""
#include <cassert>
#include <memory>
enum class Color { LightGray };
struct GfxRenderer {
  bool grayscaleReplacesFrame() const { return FREEINK_DEVICE_EEGO_A4; }
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  struct SyntheticBoldScope { SyntheticBoldScope(GfxRenderer&, int) {} };
  RenderMode mode = BW;
  unsigned dots = 0, ditherRects = 0;
  RenderMode getRenderMode() const { return mode; }
  void drawPixel(int x, int y, bool state) {
    assert((x & 1) == 0 && (y & 1) == 0 && !state);
    ++dots;
  }
  void fillRectDither(int, int, int, int, Color) { ++ditherRects; }
};
struct Settings { int fakeBold = 0; } SETTINGS;
int main() {
  GfxRenderer renderer;
""" + fill + r"""
  for (auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.mode = mode;
    renderer.dots = renderer.ditherRects = 0;
    fillHighlight(1, 1, 4, 4);
    assert(renderer.dots == (FREEINK_DEVICE_EEGO_A4 && mode != GfxRenderer::BW ? 4u : 0u));
    assert(renderer.ditherRects == (FREEINK_DEVICE_EEGO_A4 && mode != GfxRenderer::BW ? 0u : 1u));
  }
  int order = 0;
  struct Page {
    int& order;
    void render(GfxRenderer&, int, int, int) { assert(++order == 2); }
    void renderImages(GfxRenderer&, int, int, int) { assert(++order == 2); }
  };
  auto page = std::make_unique<Page>(order);
  const int fontId = 0, orientedMarginTop = 0, orientedMarginLeft = 0;
  const bool absoluteImageGrayscale = false;
  bool needsTextGrayscale = true;
  auto drawClippingHighlights = [&](Page&, int, int, int) { assert(++order == 1); };
  auto drawGuideLines = [&] { assert(++order == 3); };
  auto renderStatusBar = [&] { ++order; };
""" + render_page + with_guides + render_gray + r"""
  renderGrayscalePass();
  assert(order == (FREEINK_DEVICE_EEGO_A4 ? 4 : 3));
  order = 0;
  needsTextGrayscale = false;
  renderGrayscalePass();
  assert(order == (FREEINK_DEVICE_EEGO_A4 ? 3 : 2));
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "highlight.cpp"
    executable = Path(directory) / "highlight"
    cpp.write_text(harness)
    for a4 in (0, 1):
        subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", f"-DFREEINK_DEVICE_EEGO_A4={a4}", str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print("Clipping highlight plane regression checks passed")
