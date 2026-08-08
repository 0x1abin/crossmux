#pragma once

#include <vector>

class GfxRenderer {
 public:
  enum RenderMode {
    BW,
    GRAYSCALE_LSB,
    GRAYSCALE_MSB,
  };

  struct Line {
    int x0;
    int y0;
    int x1;
    int y1;
    bool state;

    bool operator==(const Line&) const = default;
  };

  GfxRenderer(int width, int height) : width_(width), height_(height) {}

  RenderMode getRenderMode() const { return BW; }
  int getScreenWidth() const { return width_; }
  int getScreenHeight() const { return height_; }
  void drawLine(int x0, int y0, int x1, int y1, bool state) const { lines.push_back({x0, y0, x1, y1, state}); }

  mutable std::vector<Line> lines;

 private:
  int width_;
  int height_;
};
