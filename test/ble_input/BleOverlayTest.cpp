#include <GfxRenderer.h>
#include <HalHaptics.h>
#include <gtest/gtest.h>

#include "BleInput.h"
#include "BleRuntimeHarness.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"

using Button = MappedInputManager::Button;
class BleOverlayTest : public testing::Test {
 protected:
  HalGPIO gpio;
  GfxRenderer renderer;
  MappedInputManager input{gpio, renderer};
  void SetUp() override {
    BleHid = {};
    SETTINGS = {};
    HalHaptics::reset();
    freeInternal = 100 * 1024;
    largestInternal = 40 * 1024;
    bleharness::reset(renderer, &input);
    ASSERT_EQ(bleinput::ensureStarted(renderer, bleinput::StartContext::Explicit), bleinput::StartResult::Started);
    bleharness::frame(input);  // establish this reader's routing before a remote press
    ASSERT_TRUE(bleinput::assign(SETTINGS.bleKeyMap, 1, 4, bleinput::Action::PageForward));
  }
  void key() { BleHid.events.push_back({freeink::SpecialKey::None, 4}); }
};

TEST_F(BleOverlayTest, ReleaseDoesNotInheritPhysicalHeldTime) {
  gpio.heldTime = 5000;
  key();
  bleharness::frame(input);
  EXPECT_TRUE(input.wasPressed(Button::NavNext));
  EXPECT_TRUE(input.wasReleased(Button::NavNext));
  EXPECT_EQ(input.getHeldTime(), 0U);
  EXPECT_FALSE(input.wasLongPressed(Button::NavNext, 700));
  bleharness::frame(input);
  EXPECT_FALSE(input.wasPressed(Button::NavNext));
  EXPECT_FALSE(input.wasReleased(Button::NavNext));
  EXPECT_EQ(input.getHeldTime(), 5000U);
}

TEST_F(BleOverlayTest, ReleaseDoesNotInheritTouchHeldTime) {
  gpio.touchTap = true;
  gpio.touchHeldTime = 3000;
  int x, y;
  ASSERT_TRUE(input.wasScreenTapped(x, y));
  gpio.touchTap = false;
  key();
  bleharness::frame(input);
  EXPECT_TRUE(input.wasReleased(Button::NavNext));
  EXPECT_EQ(input.getHeldTime(), 0U);
  bleharness::frame(input);
  EXPECT_EQ(input.getHeldTime(), 3000U);
}

TEST_F(BleOverlayTest, QueuedBurstCannotLeakIntoTheNextOwnerOrFrame) {
  gpio.heldTime = 5000;
  key();
  key();
  bleharness::frame(input);
  EXPECT_TRUE(input.wasPressed(Button::NavNext));
  EXPECT_TRUE(input.wasReleased(Button::NavNext));
  EXPECT_EQ(input.getHeldTime(), 0U);
  EXPECT_FALSE(input.wasLongPressed(Button::NavNext, 700));
  bleharness::frame(input);
  EXPECT_FALSE(input.wasPressed(Button::NavNext));
  key();
  bleharness::frame(input);
  EXPECT_TRUE(input.wasReleased(Button::NavNext));
}

TEST_F(BleOverlayTest, CaptureDoesNotLeakMappedEdgesIntoNextOwner) {
  input.setBleCaptureMode(true);
  key();
  bleharness::frame(input);
  EXPECT_FALSE(input.wasPressed(Button::NavNext));
  EXPECT_FALSE(input.wasReleased(Button::NavNext));
  uint8_t kind, value;
  ASSERT_TRUE(input.takeCapturedBleKey(kind, value));
  EXPECT_EQ(value, 4);
  input.setBleCaptureMode(false);
  bleharness::frame(input);
  EXPECT_FALSE(input.wasPressed(Button::NavNext));
  key();
  bleharness::frame(input);
  EXPECT_TRUE(input.wasReleased(Button::NavNext));
}

TEST_F(BleOverlayTest, PhysicalLongPressAndDirectionMappingRemainUnchanged) {
  gpio.held[HalGPIO::BTN_DOWN] = true;
  gpio.heldTime = 900;
  bleharness::frame(input);
  EXPECT_TRUE(input.wasLongPressed(Button::PageForward, 700));
  EXPECT_FALSE(input.wasLongPressed(Button::PageForward, 700));
  gpio.held.fill(false);
  gpio.released[HalGPIO::BTN_DOWN] = true;
  bleharness::frame(input);
  EXPECT_TRUE(input.consumeSuppressedRelease());
  EXPECT_FALSE(input.consumeSuppressedRelease());
  gpio = {};
  renderer.orientation = GfxRenderer::PortraitInverted;
  gpio.pressed[HalGPIO::BTN_UP] = true;
  EXPECT_TRUE(input.wasPressed(Button::PageForward));
}

TEST_F(BleOverlayTest, BackKeepsStripRemappedAndBleEdgesSeparate) {
  for (const uint8_t hardware : {HalGPIO::BTN_BACK, HalGPIO::BTN_RIGHT}) {
    gpio = {};
    SETTINGS.frontButtonBack = hardware;
    gpio.pressed[hardware] = gpio.held[hardware] = true;
    bleharness::frame(input);
    EXPECT_TRUE(input.wasPressed(Button::Back));
    EXPECT_TRUE(input.isPressed(Button::Back));
    EXPECT_FALSE(input.wasReleased(Button::Back));
    gpio = {};
    gpio.released[hardware] = true;
    bleharness::frame(input);
    EXPECT_FALSE(input.wasPressed(Button::Back));
    EXPECT_TRUE(input.wasReleased(Button::Back));
  }
  gpio = {};
  ASSERT_TRUE(bleinput::assign(SETTINGS.bleKeyMap, 1, 4, bleinput::Action::Back));
  key();
  bleharness::frame(input);
  EXPECT_TRUE(input.wasPressed(Button::Back));
  EXPECT_FALSE(input.isPressed(Button::Back));
  EXPECT_TRUE(input.wasReleased(Button::Back));
  bleharness::frame(input);
  EXPECT_FALSE(input.wasPressed(Button::Back));
  EXPECT_FALSE(input.isPressed(Button::Back));
  EXPECT_FALSE(input.wasReleased(Button::Back));
}

TEST_F(BleOverlayTest, CapacitiveFeedbackKeepsLongPressOwnershipUnderRemapping) {
  struct Case {
    int layout;
    GfxRenderer::Orientation orientation;
    uint8_t hardware;
  };
  constexpr Case cases[] = {{CrossPointSettings::PREV_NEXT, GfxRenderer::Portrait, HalGPIO::BTN_DOWN},
                            {CrossPointSettings::NEXT_PREV, GfxRenderer::Portrait, HalGPIO::BTN_UP},
                            {CrossPointSettings::PREV_NEXT, GfxRenderer::PortraitInverted, HalGPIO::BTN_UP},
                            {CrossPointSettings::NEXT_PREV, GfxRenderer::PortraitInverted, HalGPIO::BTN_DOWN}};
  for (const auto& item : cases) {
    gpio = {};
    HalHaptics::reset();
    renderer.orientation = item.orientation;
    SETTINGS.sideButtonLayout = item.layout;
    SETTINGS.vibration = CrossPointSettings::VIBRATION_TOUCH_PAGE;
    SETTINGS.hapticIntensity = CrossPointSettings::HAPTIC_MEDIUM;
    gpio.capacitivePageMask = 1u << item.hardware;
    gpio.pressed[item.hardware] = gpio.held[item.hardware] = true;
    gpio.heldTime = 900;
    bleharness::frame(input);
    EXPECT_EQ(HalHaptics::pulses, 1);
    EXPECT_TRUE(input.wasLongPressed(Button::PageForward, 700));
    EXPECT_FALSE(input.wasLongPressed(Button::PageForward, 700));
    EXPECT_EQ(HalHaptics::longPresses, 1);
    EXPECT_EQ(HalHaptics::lastIntensity, CrossPointSettings::HAPTIC_MEDIUM);
    gpio.pressed.fill(false);
    gpio.held.fill(false);
    gpio.released[item.hardware] = true;
    bleharness::frame(input);
    EXPECT_TRUE(input.consumeSuppressedRelease());
    EXPECT_FALSE(input.consumeSuppressedRelease());
    EXPECT_EQ(HalHaptics::pulses, 1);
    EXPECT_EQ(HalHaptics::longPresses, 1);
  }
}

TEST_F(BleOverlayTest, TouchOnlyAndOffKeepCapacitivePageFeedbackDisabled) {
  for (const auto mode : {CrossPointSettings::VIBRATION_TOUCH, CrossPointSettings::VIBRATION_TOUCH_PAGE,
                          CrossPointSettings::VIBRATION_OFF}) {
    gpio = {};
    HalHaptics::reset();
    SETTINGS.vibration = mode;
    gpio.capacitivePageMask = 1u << HalGPIO::BTN_DOWN;
    gpio.pressed[HalGPIO::BTN_DOWN] = true;
    bleharness::frame(input);
    EXPECT_EQ(HalHaptics::pulses, mode == CrossPointSettings::VIBRATION_TOUCH_PAGE ? 1 : 0);
  }
}

TEST_F(BleOverlayTest, BlePageEventsDoNotBecomeCapacitiveMotorFeedback) {
  SETTINGS.vibration = CrossPointSettings::VIBRATION_TOUCH_PAGE;
  key();
  bleharness::frame(input);
  EXPECT_TRUE(input.wasPressed(Button::NavNext));
  EXPECT_TRUE(input.wasReleased(Button::NavNext));
  bleharness::frame(input);
  EXPECT_EQ(HalHaptics::pulses, 0);
  EXPECT_EQ(HalHaptics::longPresses, 0);
}

TEST_F(BleOverlayTest, DragPressEdgeDoesNotReplaceDelayedRowSelection) {
  gpio.touchDown = true;
  int x = 0, y = 0;
  EXPECT_TRUE(input.wasScreenTouchPressed(x, y));
  EXPECT_EQ(x, 240);
  EXPECT_EQ(y, 400);
  EXPECT_FALSE(input.wasScreenTouchDown(x, y));
  gpio.touchDown = false;
  EXPECT_FALSE(input.wasScreenTouchPressed(x, y));
}
