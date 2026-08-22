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

PointF eyeContourPoint(const Polyline& upper, const Polyline& lower, const uint16_t index) {
  const uint16_t startU = upper.count > 20 ? 10 : 0;
  const uint16_t endU = upper.count > 20 ? upper.count - 10 : upper.count;
  const uint16_t upperCount = endU - startU;
  if (index < upperCount) return upper.points[startU + index];

  const uint16_t startL = lower.count > 20 ? 10 : 0;
  const uint16_t endL = lower.count > 20 ? lower.count - 10 : lower.count;
  return lower.points[endL - 1 - (index - upperCount)];
}

bool pointInEye(float px, float py, const Polyline& upper, const Polyline& lower) {
  const uint16_t upperCount = upper.count > 20 ? upper.count - 20 : upper.count;
  const uint16_t lowerCount = lower.count > 20 ? lower.count - 20 : lower.count;
  const uint16_t count = upperCount + lowerCount;
  if (count < 3) return false;

  bool inside = false;
  for (uint16_t i = 0, j = count - 1; i < count; j = i++) {
    const PointF current = eyeContourPoint(upper, lower, i);
    const PointF previous = eyeContourPoint(upper, lower, j);
    if (((current.y > py) != (previous.y > py)) &&
        (px < (previous.x - current.x) * (py - current.y) / (previous.y - current.y) + current.x)) {
      inside = !inside;
    }
  }
  return inside;
}

void drawPupil(const GfxRenderer& renderer, const PointF& center, float radius, const Polyline& upper,
               const Polyline& lower, const Transform& t) {
  const int sx = toScreenX(center.x, t);
  const int sy = toScreenY(center.y, t);
  const int sr = static_cast<int>(radius * t.scale + 0.5f);
  if (sr <= 0) return;
  for (int dy = -sr; dy <= sr; ++dy) {
    for (int dx = -sr; dx <= sr; ++dx) {
      if (dx * dx + dy * dy > sr * sr) continue;
      const float worldX = (sx + dx - t.offsetX) / t.scale;
      const float worldY = (sy + dy - t.offsetY) / t.scale;
      if (!pointInEye(worldX, worldY, upper, lower)) continue;
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

  drawPolylineXf(renderer, data.eyeLeftUpper, t);
  drawPolylineXf(renderer, data.eyeLeftLower, t);
  drawPolylineXf(renderer, data.eyeRightUpper, t);
  drawPolylineXf(renderer, data.eyeRightLower, t);

  drawPupil(renderer, data.eyeLeftCenter, data.pupilRadius, data.eyeLeftUpper, data.eyeLeftLower, t);
  drawPupil(renderer, data.eyeRightCenter, data.pupilRadius, data.eyeRightUpper, data.eyeRightLower, t);

  if (data.noseStyle == 0) {
    drawNoseDots(renderer, data.nose, data.noseDotRadius, t);
  } else {
    drawPolylineXf(renderer, data.nose, t);
  }

  drawPolylineXf(renderer, data.mouth, t);
}

}  // namespace avatar
