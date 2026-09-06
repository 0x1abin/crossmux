#include <gtest/gtest.h>

#include "BleButtonMapActivity.h"
#include "BleInput.h"
#include "BluetoothSettingsActivity.h"
#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"

class BleSettingsTest : public testing::Test {
 protected:
  HalGPIO gpio;
  GfxRenderer renderer;
  MappedInputManager input{gpio, renderer};
  void SetUp() override {
    BleHid = {};
    SETTINGS = {};
    sdFontSystem = {};
    freeInternal = bleinput::kExplicitMinFreeInternal - 1;
    largestInternal = bleinput::kExplicitMinLargestInternal;
    renderer.cache.recoveredInternal = 1;
  }
};

TEST_F(BleSettingsTest, SettingsAndMappingRecoverUnderOneRenderLock) {
  BluetoothSettingsActivity settings(renderer, input);
  settings.onEnter();
  EXPECT_TRUE(BleHid.running);
  EXPECT_TRUE(BleHid.startUnderLock);
  EXPECT_TRUE(renderer.cache.recoveredUnderLock);
  EXPECT_EQ(renderLockDepth, 0);
  EXPECT_EQ(sdFontSystem.unloads, 1);
  BleButtonMapActivity mapping(renderer, input);
  mapping.onEnter();
  EXPECT_EQ(sdFontSystem.unloads, 1);
  bleinput::stop();
  freeInternal = bleinput::kExplicitMinFreeInternal - 1;
  BleButtonMapActivity retryMapping(renderer, input);
  retryMapping.onEnter();
  EXPECT_TRUE(BleHid.running);
  EXPECT_TRUE(renderer.cache.recoveredUnderLock);
  EXPECT_EQ(sdFontSystem.unloads, 2);
  EXPECT_EQ(renderLockDepth, 0);
}

TEST_F(BleSettingsTest, PairingDoesNotOverwriteStartGateFailure) {
  renderer.cache.recoveredInternal = 0;
  BluetoothSettingsActivity settings(renderer, input);
  settings.onEnter();
  UiScreen screen;
  settings.testBuild(screen);
  settings.testActivate(2);  // Paired devices; available even while host is stopped.
  settings.testBuild(screen);
  settings.testActivate(0);
  settings.testDraw();
  EXPECT_STREQ(UITheme::getInstance().lastSubHeader, "low memory");
  EXPECT_EQ(BleHid.connects, 0);
  EXPECT_FALSE(BleHid.running);
  EXPECT_EQ(renderLockDepth, 0);
}

TEST_F(BleSettingsTest, ConnectionFailureIsStillReportedAfterSuccessfulStart) {
  BleHid.connectResult = false;
  BluetoothSettingsActivity settings(renderer, input);
  settings.onEnter();
  UiScreen screen;
  settings.testBuild(screen);
  settings.testActivate(3);  // Running test host reports connected, adding Disconnect.
  settings.testBuild(screen);
  settings.testActivate(0);
  settings.testDraw();
  EXPECT_STREQ(UITheme::getInstance().lastSubHeader, "connection failed");
  EXPECT_EQ(BleHid.connects, 1);
}
