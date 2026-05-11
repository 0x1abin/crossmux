#include "Render.h"

namespace avatar {

namespace {

inline int toScreenX(float x, const Transform& t) { return t.offsetX + static_cast<int>(x * t.scale + 0.5f); }
inline int toScreenY(float y, const Transform& t) { return t.offsetY + static_cast<int>(y * t.scale + 0.5f); }

void drawPolylineXf(const GfxRenderer& renderer, const Polyline& line, const Transform& t) {
  if (line.count < 2) return;
  const int thick = line.thickness < 1 ? 1 : line.thickness;
  int prevX = toScreenX(line.points[0].x, t);
  int prevY = toScreenY(line.points[0].y, t);
  for (uint16_t i = 1; i < line.count; ++i) {
    const int curX = toScreenX(line.points[i].x, t);
    const int curY = toScreenY(line.points[i].y, t);
    renderer.drawLine(prevX, prevY, curX, curY, thick, true);
    prevX = curX;
    prevY = curY;
  }
  if (line.closed) {
    const int curX = toScreenX(line.points[0].x, t);
    const int curY = toScreenY(line.points[0].y, t);
    renderer.drawLine(prevX, prevY, curX, curY, thick, true);
  }
}

bool pointInPolygonF(float px, float py, const PointF* poly, uint16_t n) {
  bool inside = false;
  for (uint16_t i = 0, j = n - 1; i < n; j = i++) {
    const float yi = poly[i].y;
    const float yj = poly[j].y;
    if (((yi > py) != (yj > py)) && (px < (poly[j].x - poly[i].x) * (py - yi) / (yj - yi) + poly[i].x)) {
      inside = !inside;
    }
  }
  return inside;
}

void buildEyeContour(const Polyline& upper, const Polyline& lower, PointF* outBuf, uint16_t& outCount) {
  outCount = 0;
  const uint16_t startU = upper.count > 20 ? 10 : 0;
  const uint16_t endU = upper.count > 20 ? upper.count - 10 : upper.count;
  for (uint16_t i = startU; i < endU; ++i) outBuf[outCount++] = upper.points[i];
  const uint16_t startL = lower.count > 20 ? 10 : 0;
  const uint16_t endL = lower.count > 20 ? lower.count - 10 : lower.count;
  for (uint16_t i = endL; i > startL; --i) outBuf[outCount++] = lower.points[i - 1];
}

void drawPupil(const GfxRenderer& renderer, const PointF& center, float radius, const PointF* contour,
               uint16_t contourCount, const Transform& t) {
  const int sx = toScreenX(center.x, t);
  const int sy = toScreenY(center.y, t);
  const int sr = static_cast<int>(radius * t.scale + 0.5f);
  if (sr <= 0) return;
  for (int dy = -sr; dy <= sr; ++dy) {
    for (int dx = -sr; dx <= sr; ++dx) {
      if (dx * dx + dy * dy > sr * sr) continue;
      const float worldX = (sx + dx - t.offsetX) / t.scale;
      const float worldY = (sy + dy - t.offsetY) / t.scale;
      if (!pointInPolygonF(worldX, worldY, contour, contourCount)) continue;
      renderer.drawPixel(sx + dx, sy + dy, true);
    }
  }
}

void drawNoseDots(const GfxRenderer& renderer, const Polyline& nose, float dotRadius, const Transform& t) {
  if (nose.count < 1) return;
  const int sr = static_cast<int>(dotRadius * t.scale + 0.5f);
  for (uint16_t i = 0; i < nose.count; ++i) {
    const int sx = toScreenX(nose.points[i].x, t);
    const int sy = toScreenY(nose.points[i].y, t);
    for (int dy = -sr; dy <= sr; ++dy) {
      for (int dx = -sr; dx <= sr; ++dx) {
        if (dx * dx + dy * dy > sr * sr) continue;
        renderer.drawPixel(sx + dx, sy + dy, true);
      }
    }
  }
}

}  // namespace

Transform computeFitTransform(const AvatarData& data, const ScreenRect& viewport) {
  const float w = data.maxX - data.minX;
  const float h = data.maxY - data.minY;
  if (w <= 0.0f || h <= 0.0f) return Transform{1.0f, viewport.x + viewport.width / 2, viewport.y + viewport.height / 2};
  const float scaleX = static_cast<float>(viewport.width) / w;
  const float scaleY = static_cast<float>(viewport.height) / h;
  const float scale = (scaleX < scaleY ? scaleX : scaleY) * 0.85f;
  const float cx = (data.minX + data.maxX) * 0.5f;
  const float cy = (data.minY + data.maxY) * 0.5f;
  const int ox = viewport.x + viewport.width / 2 - static_cast<int>(cx * scale + 0.5f);
  const int oy = viewport.y + viewport.height / 2 - static_cast<int>(cy * scale + 0.5f);
  return Transform{scale, ox, oy};
}

void drawAvatar(const GfxRenderer& renderer, const AvatarData& data, const ScreenRect& viewport) {
  const Transform t = computeFitTransform(data, viewport);

  for (uint8_t i = 0; i < data.hairCount; ++i) {
    drawPolylineXf(renderer, data.hair[i], t);
  }
  drawPolylineXf(renderer, data.face, t);

  PointF leftContour[MAX_EYELID_POINTS * 2];
  PointF rightContour[MAX_EYELID_POINTS * 2];
  uint16_t leftContourN = 0;
  uint16_t rightContourN = 0;
  buildEyeContour(data.eyeLeftUpper, data.eyeLeftLower, leftContour, leftContourN);
  buildEyeContour(data.eyeRightUpper, data.eyeRightLower, rightContour, rightContourN);

  drawPolylineXf(renderer, data.eyeLeftUpper, t);
  drawPolylineXf(renderer, data.eyeLeftLower, t);
  drawPolylineXf(renderer, data.eyeRightUpper, t);
  drawPolylineXf(renderer, data.eyeRightLower, t);

  drawPupil(renderer, data.eyeLeftCenter, data.pupilRadius, leftContour, leftContourN, t);
  drawPupil(renderer, data.eyeRightCenter, data.pupilRadius, rightContour, rightContourN, t);

  if (data.noseStyle == 0) {
    drawNoseDots(renderer, data.nose, data.noseDotRadius, t);
  } else {
    drawPolylineXf(renderer, data.nose, t);
  }

  drawPolylineXf(renderer, data.mouth, t);
}

}  // namespace avatar
