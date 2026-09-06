#include <GfxRenderer.h>
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
