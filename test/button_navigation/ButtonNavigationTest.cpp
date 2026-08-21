#include <gtest/gtest.h>

#include "ButtonNavigator.h"

namespace {

unsigned long nowMs = 0;

}  // namespace

unsigned long millis() { return nowMs; }

TEST(ButtonNavigation, PressAndContinuousHelperRepeatsOneStepAtTheConfiguredInterval) {
  MappedInputManager input;
  ButtonNavigator::setMappedInputManager(input);
  ButtonNavigator navigator(500, 500);
  int moves = 0;

  input.pressed = true;
  nowMs = 100;
  navigator.onNext([&moves] { ++moves; });
  EXPECT_EQ(moves, 1);

  input.pressed = false;
  input.held = true;
  input.heldMs = 501;
  nowMs = 1000;
  navigator.onNext([&moves] { ++moves; });
  EXPECT_EQ(moves, 2);

  nowMs = 1200;
  navigator.onNext([&moves] { ++moves; });
  EXPECT_EQ(moves, 2);

  nowMs = 1601;
  navigator.onNext([&moves] { ++moves; });
  EXPECT_EQ(moves, 3);
}

TEST(ButtonNavigation, ListContractMovesOnReleaseAndPagesOnHold) {
  MappedInputManager input;
  ButtonNavigator::setMappedInputManager(input);
  ButtonNavigator navigator(500, 500);
  int selected = 0;

  input.released = true;
  navigator.onNextRelease([&] { selected = ButtonNavigator::nextIndex(selected, 10); });
  EXPECT_EQ(selected, 1);

  input.released = false;
  input.held = true;
  input.heldMs = 501;
  nowMs = 1000;
  navigator.onNextContinuous([&] { selected = ButtonNavigator::nextPageIndex(selected, 10, 4); });
  EXPECT_EQ(selected, 4);

  input.held = false;
  input.released = true;
  navigator.onNextRelease([&] { selected = ButtonNavigator::nextIndex(selected, 10); });
  EXPECT_EQ(selected, 4);
}

TEST(ButtonNavigation, TabListContractStepsTabOnHoldWithoutWalkingRingOnRelease) {
  MappedInputManager input;
  ButtonNavigator::setMappedInputManager(input);
  ButtonNavigator navigator(500, 500);
  int ring = 0;
  int tab = 0;

  input.held = true;
  input.heldMs = 501;
  nowMs = 1000;
  navigator.onNextContinuous([&] { ++tab; });
  EXPECT_EQ(tab, 1);

  input.held = false;
  input.released = true;
  navigator.onNextRelease([&] { ring = ButtonNavigator::nextIndex(ring, 5); });
  EXPECT_EQ(ring, 0);
}

TEST(ButtonNavigation, ReleaseFiresOnlyWhenContinuousNavigationDidNotRun) {
  MappedInputManager input;
  ButtonNavigator::setMappedInputManager(input);
  ButtonNavigator navigator(500, 500);
  int activations = 0;

  input.released = true;
  navigator.onNextRelease([&activations] { ++activations; });
  EXPECT_EQ(activations, 1);

  input.released = false;
  input.held = true;
  input.heldMs = 501;
  nowMs = 1000;
  navigator.onNextContinuous([] {});

  input.held = false;
  input.released = true;
  navigator.onNextRelease([&activations] { ++activations; });
  EXPECT_EQ(activations, 1);
}
