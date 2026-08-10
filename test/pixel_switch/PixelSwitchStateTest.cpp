#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>

#include "PixelSwitchState.h"

using pixel_switch::PixelSwitchPendingBatch;
using pixel_switch::PixelSwitchRateLimiter;
using pixel_switch::PixelSwitchState;
using pixel_switch::Shade;

TEST(PixelSwitchGeometry, RotatesDisplayCoordinatesClockwise) {
  EXPECT_EQ(pixel_switch::DISPLAY_WIDTH, 60);
  EXPECT_EQ(pixel_switch::DISPLAY_HEIGHT, 100);

  const auto topLeft = pixel_switch::displayToCanvas(0, 0);
  EXPECT_EQ(topLeft.x, 0);
  EXPECT_EQ(topLeft.y, 59);

  const auto topRight = pixel_switch::displayToCanvas(59, 0);
  EXPECT_EQ(topRight.x, 0);
  EXPECT_EQ(topRight.y, 0);

  const auto bottomLeft = pixel_switch::displayToCanvas(0, 99);
  EXPECT_EQ(bottomLeft.x, 99);
  EXPECT_EQ(bottomLeft.y, 59);

  const auto bottomRight = pixel_switch::displayToCanvas(59, 99);
  EXPECT_EQ(bottomRight.x, 99);
  EXPECT_EQ(bottomRight.y, 0);

  const auto center = pixel_switch::displayToCanvas(30, 50);
  EXPECT_EQ(center.x, 50);
  EXPECT_EQ(center.y, 29);
}

TEST(PixelSwitchGeometry, MapsEveryDisplayPixelExactlyOnce) {
  std::array<bool, PixelSwitchState::PIXEL_COUNT> visited{};

  for (int y = 0; y < pixel_switch::DISPLAY_HEIGHT; ++y) {
    for (int x = 0; x < pixel_switch::DISPLAY_WIDTH; ++x) {
      const auto point = pixel_switch::displayToCanvas(x, y);
      ASSERT_GE(point.x, 0);
      ASSERT_LT(point.x, PixelSwitchState::WIDTH);
      ASSERT_GE(point.y, 0);
      ASSERT_LT(point.y, PixelSwitchState::HEIGHT);

      const size_t index = static_cast<size_t>(point.y) * PixelSwitchState::WIDTH + point.x;
      ASSERT_FALSE(visited[index]);
      visited[index] = true;
    }
  }

  for (const bool wasVisited : visited) EXPECT_TRUE(wasVisited);
}

TEST(PixelSwitchGeometry, FitsCompleteCanvasToX3AndX4Viewports) {
  const auto x4 = pixel_switch::fitDisplaySize(474, 788);
  EXPECT_EQ(x4.width, 472);
  EXPECT_EQ(x4.height, 788);

  const auto x3 = pixel_switch::fitDisplaySize(522, 780);
  EXPECT_EQ(x3.width, 468);
  EXPECT_EQ(x3.height, 780);
}

TEST(PixelSwitchGeometry, ScalesEveryDisplayPixelWithoutGaps) {
  constexpr std::array<pixel_switch::DisplaySize, 2> sizes{{{472, 788}, {468, 780}}};

  for (const auto size : sizes) {
    EXPECT_EQ(pixel_switch::scaledDisplayEdge(0, size.width, pixel_switch::DISPLAY_WIDTH), 0);
    EXPECT_EQ(pixel_switch::scaledDisplayEdge(pixel_switch::DISPLAY_WIDTH, size.width, pixel_switch::DISPLAY_WIDTH),
              size.width);
    EXPECT_EQ(pixel_switch::scaledDisplayEdge(0, size.height, pixel_switch::DISPLAY_HEIGHT), 0);
    EXPECT_EQ(pixel_switch::scaledDisplayEdge(pixel_switch::DISPLAY_HEIGHT, size.height, pixel_switch::DISPLAY_HEIGHT),
              size.height);

    for (int y = 0; y < pixel_switch::DISPLAY_HEIGHT; ++y) {
      const int top = pixel_switch::scaledDisplayEdge(y, size.height, pixel_switch::DISPLAY_HEIGHT);
      const int bottom = pixel_switch::scaledDisplayEdge(y + 1, size.height, pixel_switch::DISPLAY_HEIGHT);
      ASSERT_LT(top, bottom);
      for (int x = 0; x < pixel_switch::DISPLAY_WIDTH; ++x) {
        const int left = pixel_switch::scaledDisplayEdge(x, size.width, pixel_switch::DISPLAY_WIDTH);
        const int right = pixel_switch::scaledDisplayEdge(x + 1, size.width, pixel_switch::DISPLAY_WIDTH);
        ASSERT_LT(left, right);
      }
    }
  }
}

TEST(PixelSwitchGeometry, ElapsedTimeHandlesMillisWraparound) {
  constexpr uint32_t start = std::numeric_limits<uint32_t>::max() - 1000u;
  EXPECT_FALSE(pixel_switch::hasElapsed(start, start + 1999u, pixel_switch::PUBLISH_DEBOUNCE_MS));
  EXPECT_TRUE(pixel_switch::hasElapsed(start, start + 2000u, pixel_switch::PUBLISH_DEBOUNCE_MS));

  constexpr uint32_t laterPlacement = start + 1500u;
  EXPECT_FALSE(pixel_switch::hasElapsed(laterPlacement, laterPlacement + 1999u, pixel_switch::PUBLISH_DEBOUNCE_MS));
  EXPECT_TRUE(pixel_switch::hasElapsed(laterPlacement, laterPlacement + 2000u, pixel_switch::PUBLISH_DEBOUNCE_MS));
}

TEST(PixelSwitchGeometry, ReconnectWindowExpiresAtTwoMinutesAcrossMillisWraparound) {
  constexpr uint32_t start = UINT32_MAX - 1000u;
  EXPECT_FALSE(pixel_switch::reconnectWindowExpired(start, start + pixel_switch::RECONNECT_WINDOW_MS - 1u));
  EXPECT_TRUE(pixel_switch::reconnectWindowExpired(start, start + pixel_switch::RECONNECT_WINDOW_MS));
}

TEST(PixelSwitchGeometry, RetainedSnapshotDisplaysClockwise) {
  PixelSwitchState source;
  const auto topLeft = pixel_switch::displayToCanvas(0, 0);
  const auto topRight = pixel_switch::displayToCanvas(pixel_switch::DISPLAY_WIDTH - 1, 0);
  const auto bottomLeft = pixel_switch::displayToCanvas(0, pixel_switch::DISPLAY_HEIGHT - 1);

  EXPECT_TRUE(source.setShade(topLeft.x, topLeft.y, Shade::Black));
  EXPECT_TRUE(source.setShade(topRight.x, topRight.y, Shade::DarkGray));
  EXPECT_TRUE(source.setShade(bottomLeft.x, bottomLeft.y, Shade::LightGray));

  const auto snapshot = source.bytes();
  PixelSwitchState imported;
  EXPECT_TRUE(pixel_switch::importCanvasMessage(imported, pixel_switch::MQTT_TOPIC, snapshot.data(), snapshot.size()));
  EXPECT_EQ(imported.bytes(), snapshot);
  EXPECT_EQ(imported.shadeAt(topLeft.x, topLeft.y), Shade::Black);
  EXPECT_EQ(imported.shadeAt(topRight.x, topRight.y), Shade::DarkGray);
  EXPECT_EQ(imported.shadeAt(bottomLeft.x, bottomLeft.y), Shade::LightGray);

  const auto center = pixel_switch::displayToCanvas(30, 50);
  EXPECT_TRUE(imported.setShade(center.x, center.y, Shade::Black));
  EXPECT_EQ(imported.shadeAt(50, 29), Shade::Black);
}

TEST(PixelSwitchState, PacksAdjacentAndBoundaryPixels) {
  PixelSwitchState state;

  EXPECT_TRUE(state.setShade(0, 0, Shade::Black));
  EXPECT_TRUE(state.setShade(1, 0, Shade::DarkGray));
  EXPECT_TRUE(state.setShade(2, 0, Shade::LightGray));
  EXPECT_EQ(state.bytes()[0], 0xE4u);

  EXPECT_TRUE(state.setShade(3, 0, Shade::Black));
  EXPECT_TRUE(state.setShade(4, 0, Shade::LightGray));
  EXPECT_EQ(state.shadeAt(3, 0), Shade::Black);
  EXPECT_EQ(state.shadeAt(4, 0), Shade::LightGray);

  EXPECT_TRUE(state.setShade(PixelSwitchState::WIDTH - 1, PixelSwitchState::HEIGHT - 1, Shade::DarkGray));
  EXPECT_EQ(state.shadeAt(PixelSwitchState::WIDTH - 1, PixelSwitchState::HEIGHT - 1), Shade::DarkGray);
  EXPECT_EQ(state.bytes().back() & 0x03u, 0x02u);
}

TEST(PixelSwitchState, RejectsInvalidCoordinatesAndSameShade) {
  PixelSwitchState state;

  EXPECT_FALSE(state.setShade(-1, 0, Shade::Black));
  EXPECT_FALSE(state.setShade(PixelSwitchState::WIDTH, 0, Shade::Black));
  EXPECT_FALSE(state.setShade(0, 0, Shade::White));
  EXPECT_EQ(state.shadeAt(-1, 0), Shade::White);
}

TEST(PixelSwitchState, DetectsOnlyAnAllWhiteCanvasAsBlank) {
  PixelSwitchState state;
  EXPECT_TRUE(state.isBlank());

  EXPECT_TRUE(state.setShade(0, 0, Shade::Black));
  EXPECT_FALSE(state.isBlank());
  EXPECT_TRUE(state.setShade(0, 0, Shade::White));
  EXPECT_TRUE(state.isBlank());

  EXPECT_TRUE(state.setShade(PixelSwitchState::WIDTH - 1, PixelSwitchState::HEIGHT - 1, Shade::LightGray));
  EXPECT_FALSE(state.isBlank());
}

TEST(PixelSwitchState, ImportsOnlyExactSnapshots) {
  PixelSwitchState state;
  PixelSwitchState::Bytes snapshot{};
  snapshot.front() = 0x1Bu;
  snapshot.back() = 0xE4u;

  EXPECT_FALSE(state.assignSnapshot(nullptr, snapshot.size()));
  EXPECT_FALSE(state.assignSnapshot(snapshot.data(), snapshot.size() - 1));
  EXPECT_TRUE(state.assignSnapshot(snapshot.data(), snapshot.size()));
  EXPECT_EQ(state.bytes(), snapshot);
}

TEST(PixelSwitchState, ValidatesTopicAndPayloadLength) {
  PixelSwitchState state;
  PixelSwitchState::Bytes snapshot{};
  snapshot.front() = 0xFFu;

  EXPECT_TRUE(pixel_switch::importCanvasMessage(state, pixel_switch::MQTT_TOPIC, snapshot.data(), snapshot.size()));
  EXPECT_EQ(state.bytes(), snapshot);
  EXPECT_FALSE(pixel_switch::isValidCanvasMessage(nullptr, PixelSwitchState::BYTE_COUNT));
  EXPECT_FALSE(pixel_switch::isValidCanvasMessage("crossmux/pixel-switch/v1/other", PixelSwitchState::BYTE_COUNT));
  EXPECT_FALSE(pixel_switch::isValidCanvasMessage(pixel_switch::MQTT_TOPIC, PixelSwitchState::BYTE_COUNT - 1));

  EXPECT_FALSE(pixel_switch::importCanvasMessage(state, "crossmux/pixel-switch/v1/other", nullptr, snapshot.size()));
  EXPECT_FALSE(
      pixel_switch::importCanvasMessage(state, pixel_switch::MQTT_TOPIC, snapshot.data(), snapshot.size() - 1));
  EXPECT_EQ(state.bytes(), snapshot);
}

TEST(PixelSwitchRateLimiter, EnforcesRollingWindow) {
  PixelSwitchRateLimiter limiter;
  EXPECT_EQ(PixelSwitchRateLimiter::LIMIT, 10u);
  EXPECT_EQ(PixelSwitchRateLimiter::WINDOW_MS, 10000u);
  for (uint32_t i = 0; i < PixelSwitchRateLimiter::LIMIT; ++i) {
    EXPECT_TRUE(limiter.record(i * 100u));
  }

  EXPECT_FALSE(limiter.record(999u));
  EXPECT_EQ(limiter.remaining(999u), 0u);
  EXPECT_EQ(limiter.retryAfterMs(999u), PixelSwitchRateLimiter::WINDOW_MS - 999u);

  EXPECT_TRUE(limiter.record(PixelSwitchRateLimiter::WINDOW_MS));
  EXPECT_EQ(limiter.remaining(PixelSwitchRateLimiter::WINDOW_MS), 0u);
}

TEST(PixelSwitchRateLimiter, HandlesMillisWraparound) {
  PixelSwitchRateLimiter limiter;
  constexpr uint32_t start = std::numeric_limits<uint32_t>::max() - 100u;
  for (uint32_t i = 0; i < PixelSwitchRateLimiter::LIMIT; ++i) {
    EXPECT_TRUE(limiter.record(start + i));
  }

  EXPECT_FALSE(limiter.record(start + PixelSwitchRateLimiter::WINDOW_MS - 1u));
  EXPECT_TRUE(limiter.record(start + PixelSwitchRateLimiter::WINDOW_MS));
}

TEST(PixelSwitchBatch, CollapsesRepeatedChangesAndRevertsToBase) {
  PixelSwitchState state;
  PixelSwitchRateLimiter limiter;
  PixelSwitchPendingBatch batch;

  EXPECT_EQ(batch.place(state, limiter, 0, 0, Shade::Black, 0), pixel_switch::PlacementResult::Changed);
  EXPECT_EQ(batch.size(), 1u);
  EXPECT_EQ(batch.place(state, limiter, 0, 0, Shade::Black, 1), pixel_switch::PlacementResult::Unchanged);
  EXPECT_EQ(batch.place(state, limiter, 0, 0, Shade::DarkGray, 2), pixel_switch::PlacementResult::Changed);
  EXPECT_EQ(batch.size(), 1u);
  EXPECT_EQ(state.shadeAt(0, 0), Shade::DarkGray);

  EXPECT_EQ(batch.place(state, limiter, 0, 0, Shade::White, 3), pixel_switch::PlacementResult::Changed);
  EXPECT_TRUE(batch.empty());
  EXPECT_EQ(state.shadeAt(0, 0), Shade::White);
  EXPECT_EQ(limiter.remaining(0), PixelSwitchRateLimiter::LIMIT);
}

TEST(PixelSwitchBatch, ReservesQuotaForDistinctPendingPixels) {
  PixelSwitchState state;
  PixelSwitchRateLimiter limiter;
  PixelSwitchPendingBatch batch;

  for (int x = 0; x < PixelSwitchRateLimiter::LIMIT; ++x) {
    EXPECT_EQ(batch.place(state, limiter, x, 0, Shade::Black, x), pixel_switch::PlacementResult::Changed);
  }
  EXPECT_EQ(batch.size(), PixelSwitchRateLimiter::LIMIT);
  EXPECT_EQ(batch.place(state, limiter, PixelSwitchRateLimiter::LIMIT, 0, Shade::Black, 20),
            pixel_switch::PlacementResult::RateLimited);
  EXPECT_EQ(batch.place(state, limiter, 0, 0, Shade::DarkGray, 21), pixel_switch::PlacementResult::Changed);
  EXPECT_EQ(batch.size(), PixelSwitchRateLimiter::LIMIT);
  EXPECT_EQ(limiter.retryAfterMs(21, batch.size()), PixelSwitchRateLimiter::WINDOW_MS);
}

TEST(PixelSwitchBatch, CombinesCommittedAndPendingQuota) {
  PixelSwitchState state;
  PixelSwitchRateLimiter limiter;
  PixelSwitchPendingBatch batch;

  constexpr uint8_t committedCount = 2;
  for (uint8_t i = 0; i < committedCount; ++i) EXPECT_TRUE(limiter.record(i));
  for (int x = 0; x < PixelSwitchRateLimiter::LIMIT - committedCount; ++x) {
    EXPECT_EQ(batch.place(state, limiter, x, 0, Shade::Black, 100), pixel_switch::PlacementResult::Changed);
  }
  EXPECT_EQ(batch.place(state, limiter, PixelSwitchRateLimiter::LIMIT, 0, Shade::Black, 100),
            pixel_switch::PlacementResult::RateLimited);
  EXPECT_EQ(limiter.retryAfterMs(100, batch.size()), PixelSwitchRateLimiter::WINDOW_MS - 100u);
}

TEST(PixelSwitchBatch, RebasesLocalChangesOntoLatestSnapshot) {
  PixelSwitchState state;
  PixelSwitchRateLimiter limiter;
  PixelSwitchPendingBatch batch;

  EXPECT_EQ(batch.place(state, limiter, 0, 0, Shade::Black, 0), pixel_switch::PlacementResult::Changed);
  EXPECT_EQ(batch.place(state, limiter, 1, 0, Shade::LightGray, 1), pixel_switch::PlacementResult::Changed);

  PixelSwitchState remote;
  EXPECT_TRUE(remote.setShade(0, 0, Shade::DarkGray));
  EXPECT_TRUE(remote.setShade(2, 0, Shade::Black));
  EXPECT_TRUE(state.assignSnapshot(remote.bytes().data(), remote.bytes().size()));
  batch.rebase(state);
  EXPECT_EQ(state.shadeAt(0, 0), Shade::Black);
  EXPECT_EQ(state.shadeAt(1, 0), Shade::LightGray);
  EXPECT_EQ(state.shadeAt(2, 0), Shade::Black);
  EXPECT_EQ(batch.size(), 2u);

  EXPECT_TRUE(remote.setShade(1, 0, Shade::LightGray));
  EXPECT_TRUE(state.assignSnapshot(remote.bytes().data(), remote.bytes().size()));
  batch.rebase(state);
  EXPECT_EQ(batch.size(), 1u);
  batch.rollback(state);
  EXPECT_EQ(state.shadeAt(0, 0), Shade::DarkGray);
  EXPECT_EQ(state.shadeAt(1, 0), Shade::LightGray);
  EXPECT_EQ(state.shadeAt(2, 0), Shade::Black);
}

TEST(PixelSwitchBatch, RejectsBlankPublishWithoutUsingQuota) {
  PixelSwitchState state;
  PixelSwitchRateLimiter limiter;
  PixelSwitchPendingBatch batch;
  unsigned publishCalls = 0;

  EXPECT_TRUE(state.setShade(0, 0, Shade::Black));
  EXPECT_EQ(batch.place(state, limiter, 0, 0, Shade::White, 0), pixel_switch::PlacementResult::Changed);
  EXPECT_TRUE(state.isBlank());
  EXPECT_EQ(batch.flush(state, limiter, pixel_switch::PUBLISH_DEBOUNCE_MS,
                        [&publishCalls](const PixelSwitchState::Bytes&) {
                          ++publishCalls;
                          return true;
                        }),
            pixel_switch::FlushResult::BlankRejected);
  EXPECT_EQ(publishCalls, 0u);
  EXPECT_EQ(limiter.remaining(pixel_switch::PUBLISH_DEBOUNCE_MS), PixelSwitchRateLimiter::LIMIT);
  EXPECT_TRUE(batch.empty());
  EXPECT_EQ(state.shadeAt(0, 0), Shade::Black);
  EXPECT_FALSE(state.isBlank());
}

TEST(PixelSwitchBatch, CountsOnlyFinalPixelsAfterSuccessfulPublish) {
  PixelSwitchState state;
  PixelSwitchRateLimiter limiter;
  PixelSwitchPendingBatch batch;
  unsigned publishCalls = 0;

  EXPECT_EQ(batch.flush(state, limiter, 0,
                        [&publishCalls](const PixelSwitchState::Bytes&) {
                          ++publishCalls;
                          return true;
                        }),
            pixel_switch::FlushResult::NothingPending);
  EXPECT_EQ(publishCalls, 0u);

  EXPECT_EQ(batch.place(state, limiter, 0, 0, Shade::Black, 0), pixel_switch::PlacementResult::Changed);
  EXPECT_EQ(batch.place(state, limiter, 1, 0, Shade::DarkGray, 1), pixel_switch::PlacementResult::Changed);

  const auto publishFails = [&publishCalls](const PixelSwitchState::Bytes&) {
    ++publishCalls;
    return false;
  };
  EXPECT_EQ(batch.flush(state, limiter, 2000, publishFails), pixel_switch::FlushResult::PublishFailed);
  EXPECT_EQ(batch.size(), 2u);
  EXPECT_EQ(limiter.remaining(2000), PixelSwitchRateLimiter::LIMIT);

  const auto publishSucceeds = [&publishCalls](const PixelSwitchState::Bytes& bytes) {
    ++publishCalls;
    return bytes.front() == 0xE0u;
  };
  EXPECT_EQ(batch.flush(state, limiter, 4000, publishSucceeds), pixel_switch::FlushResult::Published);
  EXPECT_EQ(publishCalls, 2u);
  EXPECT_TRUE(batch.empty());
  EXPECT_EQ(state.shadeAt(0, 0), Shade::Black);
  EXPECT_EQ(state.shadeAt(1, 0), Shade::DarkGray);
  EXPECT_EQ(limiter.remaining(4000), PixelSwitchRateLimiter::LIMIT - 2);
}
