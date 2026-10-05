"""Compare production rectangle fills with a clipped per-pixel reference."""
from pathlib import Path
import unittest

from test_reading_ui_regressions import method, run_cpp

ROOT = Path(__file__).resolve().parents[2]


class GfxFillClipTest(unittest.TestCase):
    def test_solid_and_dither_fills_respect_clip_and_strip_in_all_orientations(self):
        source = (ROOT / "lib/GfxRenderer/GfxRenderer.cpp").read_text()
        rotate = method(source, "static inline void rotateCoordinates(")
        fill = method(source, "void GfxRenderer::fillRectImpl(")
        run_cpp(r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
enum class Color { Clear, Black, White, LightGray, DarkGray };
struct FontCacheManager { bool isScanning() const { return false; } };
struct GfxRenderer {
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  Orientation orientation = Portrait;
  uint16_t panelWidth = 16, panelHeight = 24, panelWidthBytes = 2;
  std::array<uint8_t, 48> bytes;
  FontCacheManager* fontCacheManager_ = nullptr;
  bool strip = false;
  int clipLeft_=0, clipTop_=0, clipRight_=0, clipBottom_=0;
  int getScreenWidth() const { return orientation == Portrait || orientation == PortraitInverted ? panelHeight : panelWidth; }
  int getScreenHeight() const { return orientation == Portrait || orientation == PortraitInverted ? panelWidth : panelHeight; }
  uint8_t* getWriteTarget() const { return const_cast<uint8_t*>(bytes.data()); }
  int getWriteOriginY() const { return strip ? 7 : 0; }
  int getWriteRows() const { return strip ? 8 : panelHeight; }
  // Present so the extracted fillRectImpl compiles. On the 16-level text path the
  // production function mirrors 1-bit fills into that buffer -- its fast path writes
  // framebuffer bytes directly and would otherwise miss it -- and the mirror walk
  // needs drawPixel. Both stay at their defaults here, so that branch is never taken
  // and the expectations below still describe the byte-level fill.
  const uint8_t* grayscale16Buffer = nullptr;
  void drawPixel(int, int, bool) const {}
  template<Color C> void fillRectImpl(int, int, int, int) const;
};
''' + rotate + "\ntemplate<Color C>\n" + fill + r'''
struct Rect { int x, y, w, h; };
template<Color C> void check() {
  for (auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise,
                           GfxRenderer::PortraitInverted, GfxRenderer::LandscapeCounterClockwise})
    for (bool clipped : {false, true}) for (bool strip : {false, true})
      for (Rect clip : {Rect{3,4,7,8}, Rect{0,0,0,5}, Rect{-2,-3,10,8},
                        Rect{50,50,2,2}, Rect{0,0,100,100}})
        for (Rect rect : {Rect{-4,-3,40,40}, Rect{4,5,9,11}, Rect{8,7,1,1},
                          Rect{1,2,7,2}, Rect{30,30,4,4}, Rect{1,1,0,5}, Rect{1,1,5,-1}}) {
          GfxRenderer r;
          r.orientation = orientation;
          r.strip = strip;
          r.clipLeft_ = clipped ? std::max(0,clip.x) : 0;
          r.clipTop_ = clipped ? std::max(0,clip.y) : 0;
          r.clipRight_ = clipped ? std::min(r.getScreenWidth(),clip.x+clip.w) : r.getScreenWidth();
          r.clipBottom_ = clipped ? std::min(r.getScreenHeight(),clip.y+clip.h) : r.getScreenHeight();
          r.bytes.fill(0xa5);
          auto expected = r.bytes;
          for (int y = 0; y < r.getScreenHeight(); ++y)
            for (int x = 0; x < r.getScreenWidth(); ++x) {
              if (C == Color::Clear || rect.w <= 0 || rect.h <= 0 ||
                  x < rect.x || x >= rect.x + rect.w || y < rect.y || y >= rect.y + rect.h)
                continue;
              if (clipped && (x < clip.x || x >= clip.x + clip.w || y < clip.y || y >= clip.y + clip.h))
                continue;
              int px, py;
              rotateCoordinates(orientation, x, y, &px, &py, r.panelWidth, r.panelHeight);
              if (py < r.getWriteOriginY() || py >= r.getWriteOriginY() + r.getWriteRows()) continue;
              const bool black = C == Color::Black ||
                  (C == Color::LightGray && x % 2 == 0 && y % 2 == 0) ||
                  (C == Color::DarkGray && (x + y) % 2 == 0);
              auto& byte = expected[(py - r.getWriteOriginY()) * r.panelWidthBytes + px / 8];
              const uint8_t mask = uint8_t(1u << (7 - px % 8));
              if (black) byte &= uint8_t(~mask); else byte |= mask;
            }
          r.fillRectImpl<C>(rect.x, rect.y, rect.w, rect.h);
          assert(r.bytes == expected);
        }
}
int main() {
  check<Color::Clear>(); check<Color::Black>(); check<Color::White>();
  check<Color::LightGray>(); check<Color::DarkGray>();
}
''')


if __name__ == "__main__":
    unittest.main()
