#include "PixelSwitchState.h"

#include <algorithm>
#include <cstring>

namespace pixel_switch {

namespace {

bool inBounds(const int x, const int y) {
  return x >= 0 && x < PixelSwitchState::WIDTH && y >= 0 && y < PixelSwitchState::HEIGHT;
}

size_t pixelIndex(const int x, const int y) {
  return static_cast<size_t>(y) * PixelSwitchState::WIDTH + static_cast<size_t>(x);
}

uint8_t pixelShift(const size_t index) { return static_cast<uint8_t>(6u - (index & 3u) * 2u); }

}  // namespace

void PixelSwitchState::clear() { bytes_.fill(0); }

Shade PixelSwitchState::shadeAt(const int x, const int y) const {
  if (!inBounds(x, y)) return Shade::White;
  const size_t index = pixelIndex(x, y);
  return static_cast<Shade>((bytes_[index / 4] >> pixelShift(index)) & 0x03u);
}

bool PixelSwitchState::setShade(const int x, const int y, const Shade shade) {
  if (!inBounds(x, y)) return false;
  const uint8_t shadeValue = static_cast<uint8_t>(shade);
  if (shadeValue > static_cast<uint8_t>(Shade::Black)) return false;
  const size_t index = pixelIndex(x, y);
  const uint8_t shift = pixelShift(index);
  const uint8_t mask = static_cast<uint8_t>(0x03u << shift);
  const uint8_t updated = static_cast<uint8_t>((bytes_[index / 4] & ~mask) | (shadeValue << shift));
  if (updated == bytes_[index / 4]) return false;
  bytes_[index / 4] = updated;
  return true;
}

bool PixelSwitchState::assignSnapshot(const uint8_t* payload, const size_t length) {
  if (!payload || length != BYTE_COUNT) return false;
  memcpy(bytes_.data(), payload, BYTE_COUNT);
  return true;
}

bool PixelSwitchState::isBlank() const {
  return std::all_of(bytes_.begin(), bytes_.end(), [](const uint8_t value) { return value == 0; });
}

bool isValidCanvasMessage(const char* topic, const size_t payloadLength) {
  return topic && payloadLength == PixelSwitchState::BYTE_COUNT && strcmp(topic, MQTT_TOPIC) == 0;
}

bool importCanvasMessage(PixelSwitchState& state, const char* topic, const uint8_t* payload,
                         const size_t payloadLength) {
  if (!isValidCanvasMessage(topic, payloadLength)) return false;
  return state.assignSnapshot(payload, payloadLength);
}

void PixelSwitchRateLimiter::prune(const uint32_t now) {
  while (count_ > 0 && static_cast<uint32_t>(now - timestamps_[head_]) >= WINDOW_MS) {
    head_ = static_cast<uint8_t>((head_ + 1) % LIMIT);
    --count_;
  }
}

bool PixelSwitchRateLimiter::record(const uint32_t now) {
  prune(now);
  if (count_ >= LIMIT) return false;
  const uint8_t tail = static_cast<uint8_t>((head_ + count_) % LIMIT);
  timestamps_[tail] = now;
  ++count_;
  return true;
}

uint8_t PixelSwitchRateLimiter::remaining(const uint32_t now) {
  prune(now);
  return static_cast<uint8_t>(LIMIT - count_);
}

uint32_t PixelSwitchRateLimiter::retryAfterMs(const uint32_t now, const uint8_t reserved) {
  prune(now);
  if (static_cast<uint16_t>(count_) + reserved < LIMIT) return 0;
  if (count_ == 0) return WINDOW_MS;
  return WINDOW_MS - static_cast<uint32_t>(now - timestamps_[head_]);
}

int PixelSwitchPendingBatch::find(const int x, const int y) const {
  for (uint8_t i = 0; i < count_; ++i) {
    if (changes_[i].x == x && changes_[i].y == y) return i;
  }
  return -1;
}

void PixelSwitchPendingBatch::remove(const uint8_t index) {
  for (uint8_t i = index; i + 1 < count_; ++i) changes_[i] = changes_[i + 1];
  --count_;
}

PlacementResult PixelSwitchPendingBatch::place(PixelSwitchState& state, PixelSwitchRateLimiter& limiter, const int x,
                                               const int y, const Shade shade, const uint32_t now) {
  if (!inBounds(x, y)) return PlacementResult::Unchanged;
  const Shade current = state.shadeAt(x, y);
  if (shade == current) return PlacementResult::Unchanged;

  const int existing = find(x, y);
  if (existing < 0) {
    if (limiter.remaining(now) <= count_) return PlacementResult::RateLimited;
    state.setShade(x, y, shade);
    changes_[count_++] = Change{static_cast<uint8_t>(x), static_cast<uint8_t>(y), current, shade};
    return PlacementResult::Changed;
  }

  state.setShade(x, y, shade);
  auto& change = changes_[existing];
  if (shade == change.baseShade) {
    remove(static_cast<uint8_t>(existing));
  } else {
    change.shade = shade;
  }
  return PlacementResult::Changed;
}

void PixelSwitchPendingBatch::rebase(PixelSwitchState& state) {
  uint8_t index = 0;
  while (index < count_) {
    auto& change = changes_[index];
    change.baseShade = state.shadeAt(change.x, change.y);
    if (change.shade == change.baseShade) {
      remove(index);
      continue;
    }
    state.setShade(change.x, change.y, change.shade);
    ++index;
  }
}

void PixelSwitchPendingBatch::rollback(PixelSwitchState& state) {
  for (uint8_t i = 0; i < count_; ++i) state.setShade(changes_[i].x, changes_[i].y, changes_[i].baseShade);
  clear();
}

}  // namespace pixel_switch
