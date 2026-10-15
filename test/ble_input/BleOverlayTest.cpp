#include <GfxRenderer.h>
#include <HalHaptics.h>
#include <gtest/gtest.h>

#include "BleInput.h"
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
    ASSERT_TRUE(bleinput::assign(SETTINGS.bleKeyMap, 1, 42, bleinput::Action::PageForward));
  }
  void key() { BleHid.events.push_back({freeink::SpecialKey::None, 42}); }
};

TEST_F(BleOverlayTest, ReleaseDoesNotInheritPhysicalHeldTime) {
  gpio.heldTime = 5000;
  key();
  input.update();
  EXPECT_TRUE(input.wasPressed(Button::PageForward));
  EXPECT_EQ(input.getHeldTime(), 0U);
  EXPECT_FALSE(input.wasLongPressed(Button::PageForward, 700));
  input.update();
  EXPECT_TRUE(input.wasReleased(Button::PageForward));
  EXPECT_TRUE(input.wasAnyReleased());
  EXPECT_EQ(input.getHeldTime(), 0U);
  input.update();
  EXPECT_FALSE(input.wasAnyReleased());
  EXPECT_EQ(input.getHeldTime(), 5000U);
}

TEST_F(BleOverlayTest, ReleaseDoesNotInheritTouchHeldTime) {
  gpio.touchTap = true;
  gpio.touchHeldTime = 3000;
  int x, y;
  ASSERT_TRUE(input.wasScreenTapped(x, y));
  gpio.touchTap = false;
  key();
  input.update();
  EXPECT_EQ(input.getHeldTime(), 0U);
  input.update();
  EXPECT_TRUE(input.wasReleased(Button::PageForward));
  EXPECT_EQ(input.getHeldTime(), 0U);
  input.update();
  EXPECT_EQ(input.getHeldTime(), 3000U);
}

TEST_F(BleOverlayTest, RepeatedKeysRemainSeparateShortPresses) {
  gpio.heldTime = 5000;
  key();
  key();
  for (int frame = 0; frame < 4; ++frame) {
    input.update();
    EXPECT_EQ(input.wasPressed(Button::PageForward), frame % 2 == 0);
    EXPECT_EQ(input.wasReleased(Button::PageForward), frame % 2 == 1);
    EXPECT_EQ(input.getHeldTime(), 0U);
    EXPECT_FALSE(input.wasLongPressed(Button::PageForward, 700));
  }
}

TEST_F(BleOverlayTest, CaptureDoesNotLeakMappedEdgesIntoNextOwner) {
  key();
  input.update();
  input.setBleCaptureMode(true);
  key();
  input.update();
  EXPECT_FALSE(input.wasAnyPressed());
  EXPECT_FALSE(input.wasAnyReleased());
  uint8_t kind, value;
  ASSERT_TRUE(input.takeCapturedBleKey(kind, value));
  EXPECT_EQ(value, 42);
  input.setBleCaptureMode(false);
  input.update();
  EXPECT_FALSE(input.wasAnyPressed());
  EXPECT_FALSE(input.wasAnyReleased());
  key();
  input.update();
  EXPECT_TRUE(input.wasPressed(Button::PageForward));
}

TEST_F(BleOverlayTest, PhysicalLongPressAndDirectionMappingRemainUnchanged) {
  gpio.held[HalGPIO::BTN_DOWN] = true;
  gpio.heldTime = 900;
  input.update();
  EXPECT_TRUE(input.wasLongPressed(Button::PageForward, 700));
  EXPECT_FALSE(input.wasLongPressed(Button::PageForward, 700));
  gpio.held.fill(false);
  gpio.released[HalGPIO::BTN_DOWN] = true;
  input.update();
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
    input.update();
    EXPECT_TRUE(input.wasPressed(Button::Back));
    EXPECT_TRUE(input.isPressed(Button::Back));
    EXPECT_FALSE(input.wasReleased(Button::Back));
    gpio = {};
    gpio.released[hardware] = true;
    input.update();
    EXPECT_FALSE(input.wasPressed(Button::Back));
    EXPECT_TRUE(input.wasReleased(Button::Back));
  }
  gpio = {};
  ASSERT_TRUE(bleinput::assign(SETTINGS.bleKeyMap, 1, 42, bleinput::Action::Back));
  key();
  input.update();
  EXPECT_TRUE(input.wasPressed(Button::Back));
  EXPECT_TRUE(input.isPressed(Button::Back));
  EXPECT_FALSE(input.wasReleased(Button::Back));
  input.update();
  EXPECT_FALSE(input.wasPressed(Button::Back));
  EXPECT_FALSE(input.isPressed(Button::Back));
  EXPECT_TRUE(input.wasReleased(Button::Back));
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
    input.update();
    EXPECT_EQ(HalHaptics::pulses, 1);
    EXPECT_TRUE(input.wasLongPressed(Button::PageForward, 700));
    EXPECT_FALSE(input.wasLongPressed(Button::PageForward, 700));
    EXPECT_EQ(HalHaptics::longPresses, 1);
    EXPECT_EQ(HalHaptics::lastIntensity, CrossPointSettings::HAPTIC_MEDIUM);
    gpio.pressed.fill(false);
    gpio.held.fill(false);
    gpio.released[item.hardware] = true;
    input.update();
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
    input.update();
    EXPECT_EQ(HalHaptics::pulses, mode == CrossPointSettings::VIBRATION_TOUCH_PAGE ? 1 : 0);
  }
}

TEST_F(BleOverlayTest, BlePageEventsDoNotBecomeCapacitiveMotorFeedback) {
  SETTINGS.vibration = CrossPointSettings::VIBRATION_TOUCH_PAGE;
  key();
  input.update();
  EXPECT_TRUE(input.wasPressed(Button::PageForward));
  input.update();
  EXPECT_TRUE(input.wasReleased(Button::PageForward));
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
