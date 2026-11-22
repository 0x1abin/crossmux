#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

// Board-corrected gravity -> existing reading orientation values. No allocation
// or hardware access; the HAL owns sampling and cancels across screen lifetimes.
class AutoRotation {
  enum class Pose : uint8_t { Portrait, Clockwise, Inverted, Counterclockwise, Unknown };
  Pose candidate = Pose::Unknown;
  uint32_t candidateSince = 0;
  uint32_t lastSample = 0;
  bool haveSample = false;
  uint32_t lastEmission = 0;
  uint8_t lastCurrent = 0;
  bool haveCurrent = false;
  bool emitted = false;

 public:
  void reset() {
    candidate = Pose::Unknown;
    haveCurrent = false;
    haveSample = false;
    emitted = false;
  }

  std::optional<uint8_t> update(float x, float y, float z, uint32_t now, uint8_t current) {
    // The Plus HAL polls every80ms: three missed intervals invalidate dwell.
    if (haveSample && now - lastSample > 240) candidate = Pose::Unknown;
    lastSample = now;
    haveSample = true;
    if (!haveCurrent || lastCurrent != current) {
      candidate = Pose::Unknown;
      lastCurrent = current;
      haveCurrent = true;
    }
    const float magnitudeSquared = x * x + y * y + z * z;
    const float ax = std::fabs(x), ay = std::fabs(y);
    if (current > 3 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        magnitudeSquared < 0.75f * 0.75f || magnitudeSquared > 1.25f * 1.25f || std::fabs(z) > 0.8f ||
        std::fabs(ax - ay) < 0.25f || (ax > ay ? ax : ay) < 0.7f) {
      candidate = Pose::Unknown;
      return std::nullopt;
    }
    const Pose pose =
        ax > ay ? (x > 0 ? Pose::Clockwise : Pose::Counterclockwise) : (y > 0 ? Pose::Inverted : Pose::Portrait);
    if (static_cast<uint8_t>(pose) == current) {
      candidate = Pose::Unknown;
      return std::nullopt;
    }
    if (candidate != pose) {
      candidate = pose;
      candidateSince = now;
      return std::nullopt;
    }
    if (now - candidateSince < 600 || (emitted && now - lastEmission < 1200)) return std::nullopt;
    lastEmission = now;
    emitted = true;
    candidate = Pose::Unknown;
    return static_cast<uint8_t>(pose);
  }
};
