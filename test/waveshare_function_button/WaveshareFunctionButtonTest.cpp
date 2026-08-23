#include <gtest/gtest.h>

#include "FunctionButtonGesture.h"
#include "Waveshare397Power.h"

using freeink::input::FunctionButtonGesture;
using Waveshare397Power::PowerKeyState;

namespace {

FunctionButtonGesture::State settle(FunctionButtonGesture& gesture, uint8_t raw, uint32_t changedAt) {
  gesture.update(raw, changedAt);
  return gesture.update(raw, changedAt + FunctionButtonGesture::DEBOUNCE_MS);
}

}  // namespace

TEST(WaveshareFunctionButton, BackDebouncesAndTracksLevel) {
  FunctionButtonGesture gesture;
  auto state = settle(gesture, FunctionButtonGesture::BACK, 10);
  EXPECT_EQ(state.pressed, FunctionButtonGesture::BACK);
  EXPECT_EQ(state.down, FunctionButtonGesture::BACK);

  state = settle(gesture, 0, 30);
  EXPECT_EQ(state.released, FunctionButtonGesture::BACK);
  EXPECT_EQ(state.down, 0);
}

TEST(WaveshareFunctionButton, DirectionShortPressEmitsOnceOnRelease) {
  FunctionButtonGesture gesture;
  auto state = settle(gesture, FunctionButtonGesture::LEFT, 10);
  EXPECT_EQ(state.pressed, 0);
  EXPECT_EQ(state.down, 0);

  state = settle(gesture, 0, 30);
  EXPECT_EQ(state.pressed, FunctionButtonGesture::LEFT);
  EXPECT_EQ(state.released, FunctionButtonGesture::LEFT);
  EXPECT_EQ(state.down, 0);

  state = settle(gesture, FunctionButtonGesture::RIGHT, 50);
  EXPECT_EQ(state.pressed, 0);
  state = settle(gesture, 0, 70);
  EXPECT_EQ(state.pressed, FunctionButtonGesture::RIGHT);
  EXPECT_EQ(state.released, FunctionButtonGesture::RIGHT);
}

TEST(WaveshareFunctionButton, DirectionReleaseBelowThresholdRemainsShort) {
  FunctionButtonGesture gesture;
  settle(gesture, FunctionButtonGesture::LEFT, 0);  // stable from 5 ms
  gesture.update(0, 654);                           // held for 649 ms
  const auto state = gesture.update(0, 659);

  EXPECT_EQ(state.pressed, FunctionButtonGesture::LEFT);
  EXPECT_EQ(state.released, FunctionButtonGesture::LEFT);
}

TEST(WaveshareFunctionButton, DirectionHoldAtBoundaryMapsAndReleasesOnce) {
  FunctionButtonGesture gesture;
  settle(gesture, FunctionButtonGesture::LEFT | FunctionButtonGesture::RIGHT, 0);  // stable from 5 ms
  auto state = gesture.update(FunctionButtonGesture::LEFT | FunctionButtonGesture::RIGHT, 655);
  EXPECT_EQ(state.pressed, FunctionButtonGesture::UP | FunctionButtonGesture::DOWN);
  EXPECT_EQ(state.down, FunctionButtonGesture::UP | FunctionButtonGesture::DOWN);
  EXPECT_EQ(state.startedMs, 5);

  state = gesture.update(FunctionButtonGesture::LEFT | FunctionButtonGesture::RIGHT, 900);
  EXPECT_EQ(state.pressed, 0);
  EXPECT_EQ(state.down, FunctionButtonGesture::UP | FunctionButtonGesture::DOWN);

  state = settle(gesture, 0, 1000);
  EXPECT_EQ(state.pressed, 0);
  EXPECT_EQ(state.released, FunctionButtonGesture::UP | FunctionButtonGesture::DOWN);
  EXPECT_EQ(state.down, 0);
}

TEST(WaveshareFunctionButton, DirectionDebounceRejectsBounce) {
  FunctionButtonGesture gesture;

  gesture.update(FunctionButtonGesture::RIGHT, 50);
  const auto state = gesture.update(0, 54);
  EXPECT_EQ(state.pressed, 0);
  EXPECT_EQ(state.down, 0);
}

TEST(WaveshareFunctionButton, DirectionHoldHandlesTimestampWrap) {
  FunctionButtonGesture gesture;
  constexpr uint32_t changedAt = UINT32_MAX - 10;
  settle(gesture, FunctionButtonGesture::LEFT, changedAt);  // stable at UINT32_MAX - 5

  const auto state = gesture.update(FunctionButtonGesture::LEFT, 644);
  EXPECT_EQ(state.pressed, FunctionButtonGesture::UP);
  EXPECT_EQ(state.down, FunctionButtonGesture::UP);
}

TEST(WaveshareFunctionButton, SingleClickWaitsBeyondDoubleClickWindow) {
  FunctionButtonGesture gesture;
  settle(gesture, FunctionButtonGesture::CONFIRM, 0);
  settle(gesture, 0, 50);

  auto state = gesture.update(0, 355);
  EXPECT_EQ(state.pressed, 0);
  state = gesture.update(0, 356);
  EXPECT_EQ(state.pressed, FunctionButtonGesture::CONFIRM);
  EXPECT_EQ(state.released, FunctionButtonGesture::CONFIRM);
}

TEST(WaveshareFunctionButton, DoubleClickAcceptsExactBoundaryAndEmitsBackOnce) {
  FunctionButtonGesture gesture;
  settle(gesture, FunctionButtonGesture::CONFIRM, 0);
  settle(gesture, 0, 50);  // pending confirm starts at 55 ms
  settle(gesture, FunctionButtonGesture::CONFIRM, 355);
  auto state = settle(gesture, 0, 400);

  EXPECT_EQ(state.pressed, FunctionButtonGesture::BACK);
  EXPECT_EQ(state.released, FunctionButtonGesture::BACK);
  state = gesture.update(0, 800);
  EXPECT_EQ(state.pressed, 0);
}

TEST(WaveshareFunctionButton, PressAfterWindowCommitsPriorClick) {
  FunctionButtonGesture gesture;
  settle(gesture, FunctionButtonGesture::CONFIRM, 0);
  settle(gesture, 0, 50);
  gesture.update(FunctionButtonGesture::CONFIRM, 356);
  const auto state = gesture.update(FunctionButtonGesture::CONFIRM, 361);

  EXPECT_EQ(state.pressed, FunctionButtonGesture::CONFIRM);
  EXPECT_EQ(state.released, FunctionButtonGesture::CONFIRM);
}

TEST(WaveshareFunctionButton, HoldAtBoundaryKeepsConfirmDownAndNeverEmitsPower) {
  FunctionButtonGesture gesture;
  settle(gesture, FunctionButtonGesture::CONFIRM, 0);

  auto state = gesture.update(FunctionButtonGesture::CONFIRM, 300);
  EXPECT_EQ(state.pressed, FunctionButtonGesture::CONFIRM);
  EXPECT_EQ(state.down, FunctionButtonGesture::CONFIRM);
  EXPECT_EQ(state.startedMs, 0);

  for (const uint32_t now : {500u, 1000u, 1500u}) {
    state = gesture.update(FunctionButtonGesture::CONFIRM, now);
    EXPECT_EQ(state.pressed, 0);
    EXPECT_EQ(state.down, FunctionButtonGesture::CONFIRM);
  }

  state = settle(gesture, 0, 1600);
  EXPECT_EQ(state.pressed, 0);
  EXPECT_EQ(state.released, FunctionButtonGesture::CONFIRM);
  EXPECT_EQ(state.down, 0);
}

TEST(WaveshareFunctionButton, HoldingSecondClickCancelsBackAndBecomesConfirmHold) {
  FunctionButtonGesture gesture;
  settle(gesture, FunctionButtonGesture::CONFIRM, 0);
  settle(gesture, 0, 50);
  settle(gesture, FunctionButtonGesture::CONFIRM, 100);

  auto state = gesture.update(FunctionButtonGesture::CONFIRM, 400);
  EXPECT_EQ(state.pressed, FunctionButtonGesture::CONFIRM);
  EXPECT_EQ(state.released, 0);
  EXPECT_EQ(state.down, FunctionButtonGesture::CONFIRM);

  state = settle(gesture, 0, 500);
  EXPECT_EQ(state.pressed, 0);
  EXPECT_EQ(state.released, FunctionButtonGesture::CONFIRM);
  EXPECT_EQ(state.released & FunctionButtonGesture::BACK, 0);
}

TEST(WavesharePowerKey, NegativePressAndPositiveReleaseTrackLevel) {
  PowerKeyState state;
  EXPECT_EQ(PowerKeyState::PRESS_IRQ, 1u << 1);
  EXPECT_EQ(PowerKeyState::RELEASE_IRQ, 1u << 0);
  EXPECT_TRUE(state.apply(PowerKeyState::PRESS_IRQ));
  EXPECT_TRUE(state.apply(0));
  EXPECT_TRUE(state.apply(0));
  EXPECT_FALSE(state.apply(PowerKeyState::RELEASE_IRQ));
}

TEST(WavesharePowerKey, BootReleaseAfterClearedPressStaysReleased) {
  PowerKeyState state;
  EXPECT_FALSE(state.apply(PowerKeyState::RELEASE_IRQ));
  EXPECT_FALSE(state.apply(0));
  EXPECT_FALSE(state.current());
}

TEST(WavesharePowerKey, CoalescedEdgesProduceOneBufferedClick) {
  PowerKeyState state;
  EXPECT_TRUE(state.apply(PowerKeyState::PRESS_IRQ | PowerKeyState::RELEASE_IRQ));
  EXPECT_FALSE(state.apply(0));
  EXPECT_FALSE(state.apply(0));
}
