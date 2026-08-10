#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace pixel_switch {

inline constexpr char MQTT_TOPIC[] = "crossmux/pixel-switch/v1/canvas";
inline constexpr uint32_t PUBLISH_DEBOUNCE_MS = 2000u;
inline constexpr uint32_t RECONNECT_WINDOW_MS = 120000u;

enum class Shade : uint8_t {
  White = 0,
  LightGray = 1,
  DarkGray = 2,
  Black = 3,
};

class PixelSwitchState {
 public:
  static constexpr int WIDTH = 100;
  static constexpr int HEIGHT = 60;
  static constexpr size_t PIXEL_COUNT = static_cast<size_t>(WIDTH) * HEIGHT;
  static constexpr size_t BYTE_COUNT = PIXEL_COUNT / 4;
  using Bytes = std::array<uint8_t, BYTE_COUNT>;

  void clear();
  Shade shadeAt(int x, int y) const;
  bool setShade(int x, int y, Shade shade);
  bool assignSnapshot(const uint8_t* payload, size_t length);
  bool isBlank() const;

  const Bytes& bytes() const { return bytes_; }

 private:
  Bytes bytes_{};
};

struct CanvasPoint {
  int x;
  int y;
};

struct DisplaySize {
  int width;
  int height;
};

inline constexpr int DISPLAY_WIDTH = PixelSwitchState::HEIGHT;
inline constexpr int DISPLAY_HEIGHT = PixelSwitchState::WIDTH;

constexpr CanvasPoint displayToCanvas(const int x, const int y) {
  return CanvasPoint{y, PixelSwitchState::HEIGHT - 1 - x};
}

constexpr DisplaySize fitDisplaySize(const int availableWidth, const int availableHeight) {
  if (availableWidth <= 0 || availableHeight <= 0) return DisplaySize{0, 0};
  if (availableWidth * DISPLAY_HEIGHT <= availableHeight * DISPLAY_WIDTH) {
    return DisplaySize{availableWidth, availableWidth * DISPLAY_HEIGHT / DISPLAY_WIDTH};
  }
  return DisplaySize{availableHeight * DISPLAY_WIDTH / DISPLAY_HEIGHT, availableHeight};
}

constexpr int scaledDisplayEdge(const int index, const int extent, const int logicalExtent) {
  return logicalExtent > 0 ? index * extent / logicalExtent : 0;
}

constexpr bool hasElapsed(const uint32_t start, const uint32_t now, const uint32_t duration) {
  return static_cast<uint32_t>(now - start) >= duration;
}

constexpr bool reconnectWindowExpired(const uint32_t start, const uint32_t now) {
  return hasElapsed(start, now, RECONNECT_WINDOW_MS);
}

bool isValidCanvasMessage(const char* topic, size_t payloadLength);
bool importCanvasMessage(PixelSwitchState& state, const char* topic, const uint8_t* payload, size_t payloadLength);

class PixelSwitchRateLimiter {
 public:
  static constexpr uint8_t LIMIT = 10;
  static constexpr uint32_t WINDOW_MS = 10000u;

  bool record(uint32_t now);
  uint8_t remaining(uint32_t now);
  uint32_t retryAfterMs(uint32_t now, uint8_t reserved = 0);

 private:
  void prune(uint32_t now);

  std::array<uint32_t, LIMIT> timestamps_{};
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};

enum class PlacementResult : uint8_t {
  Changed,
  Unchanged,
  RateLimited,
};

enum class FlushResult : uint8_t {
  NothingPending,
  BlankRejected,
  Published,
  PublishFailed,
};

class PixelSwitchPendingBatch {
 public:
  PlacementResult place(PixelSwitchState& state, PixelSwitchRateLimiter& limiter, int x, int y, Shade shade,
                        uint32_t now);
  void rebase(PixelSwitchState& state);
  void rollback(PixelSwitchState& state);
  void clear() { count_ = 0; }

  bool empty() const { return count_ == 0; }
  uint8_t size() const { return count_; }

  template <typename Publisher>
  FlushResult flush(PixelSwitchState& state, PixelSwitchRateLimiter& limiter, const uint32_t now, Publisher&& publish) {
    if (empty()) return FlushResult::NothingPending;
    if (state.isBlank()) {
      rollback(state);
      return FlushResult::BlankRejected;
    }
    if (!publish(state.bytes())) return FlushResult::PublishFailed;

    const uint8_t publishedCount = count_;
    clear();
    for (uint8_t i = 0; i < publishedCount; ++i) limiter.record(now);
    return FlushResult::Published;
  }

 private:
  struct Change {
    uint8_t x;
    uint8_t y;
    Shade baseShade;
    Shade shade;
  };

  int find(int x, int y) const;
  void remove(uint8_t index);

  std::array<Change, PixelSwitchRateLimiter::LIMIT> changes_{};
  uint8_t count_ = 0;
};

}  // namespace pixel_switch
