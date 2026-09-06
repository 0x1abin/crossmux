#include <GfxRenderer.h>
#include <Logging.h>
#include <gtest/gtest.h>

#include "BleInput.h"
#include "SdCardFontSystem.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "nimble/esp_port/port/include/esp_nimble_mem.h"
#include "platform/NimblePsramConfig.h"

#if CROSSPOINT_BLE_HOST_PSRAM
#ifdef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL
#error "PSRAM override must remove the internal macro even when sdkconfig defines it"
#endif
static_assert(CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL == 1);
#else
static_assert(CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL == 1);
#endif

class BleInputTest : public testing::Test {
 protected:
  GfxRenderer renderer;
  bleinput::StartResult start(bleinput::StartContext context) {
    RenderLock lock;
    return bleinput::ensureStarted(renderer, context);
  }
  void SetUp() override {
    sdFontSystem = {};
    BleHid = {};
    freeInternal = 100 * 1024;
    largestInternal = 40 * 1024;
    freePsram = 512 * 1024;
    largestPsram = 128 * 1024;
    probes = probeFrees = 0;
    probeFails = false;
    probeExternal = true;
  }
};

#if FREEINK_CAP_BLE_HID_HOST
TEST_F(BleInputTest, InternalGateBoundariesRemainUnchanged) {
  for (const auto context : {bleinput::StartContext::Reader, bleinput::StartContext::Explicit}) {
    const size_t minFree = context == bleinput::StartContext::Reader ? 81920 : 71680;
    const size_t minLargest = context == bleinput::StartContext::Reader ? 32768 : 24576;
    freeInternal = minFree - 1;
    largestInternal = minLargest;
    EXPECT_EQ(start(context), bleinput::StartResult::LowMemory);
    freeInternal = minFree;
    largestInternal = minLargest - 1;
    EXPECT_EQ(start(context), bleinput::StartResult::LowMemory);
    EXPECT_EQ(probes, 0);
    largestInternal = minLargest;
    EXPECT_EQ(start(context), bleinput::StartResult::Started);
    bleinput::stop();
    probes = 0;
  }
}

TEST_F(BleInputTest, RepeatedRejectionsKeepRetryingWithoutRepeatingDiagnostics) {
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Started);
  bleinput::stop();
  largestInternal = 29684;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::LowMemory);
  const auto firstLogs = testLogCount;
  const auto firstRecovery = renderer.cache.releases;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::LowMemory);
  EXPECT_EQ(testLogCount, firstLogs);
  EXPECT_EQ(renderer.cache.releases, firstRecovery + 1);
  freeInternal = 0;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::LowMemory);
  EXPECT_GT(testLogCount, firstLogs);
  freeInternal = 100 * 1024;
  largestInternal = 40 * 1024;
#if !CROSSPOINT_BLE_HOST_PSRAM
  BleHid.beginResult = false;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Failed);
  const auto failedLogs = testLogCount;
  const auto begins = BleHid.begins;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Failed);
  EXPECT_EQ(testLogCount, failedLogs);
  EXPECT_EQ(BleHid.begins, begins + 1);
  BleHid.beginResult = true;
#endif
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Started);
}

TEST_F(BleInputTest, RunningHostDoesNotAllocateOrStartAgain) {
  BleHid.running = true;
  freeInternal = freePsram = 0;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::AlreadyRunning);
  EXPECT_EQ(BleHid.begins, 0);
  EXPECT_EQ(probes, 0);
  EXPECT_EQ(renderer.cache.releases, 0);
  EXPECT_EQ(renderer.cache.clears, 0);
  EXPECT_EQ(sdFontSystem.unloads, 0);
}

TEST_F(BleInputTest, ReaderRecoversOnceWithoutUnloadingFonts) {
  freeInternal = bleinput::kReaderMinFreeInternal - 1;
  renderer.cache.recoveredInternal = 1;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Started);
  EXPECT_EQ(renderer.cache.releases, 1);
  EXPECT_TRUE(renderer.cache.recoveredUnderLock);
  EXPECT_TRUE(BleHid.startUnderLock);
  EXPECT_EQ(renderer.cache.clears, 0);
  EXPECT_EQ(sdFontSystem.unloads, 0);
}

TEST_F(BleInputTest, ExplicitRecoversFontsAndCachesUsingItsOwnGate) {
  freeInternal = bleinput::kExplicitMinFreeInternal - 1;
  largestInternal = bleinput::kExplicitMinLargestInternal;
  renderer.cache.recoveredInternal = 1;
  EXPECT_EQ(start(bleinput::StartContext::Explicit), bleinput::StartResult::Started);
  EXPECT_EQ(renderer.cache.releases, 0);
  EXPECT_EQ(renderer.cache.clears, 1);
  EXPECT_EQ(sdFontSystem.unloads, 1);
}

TEST_F(BleInputTest, HealthyMemoryDoesNotDiscardCaches) {
  EXPECT_EQ(start(bleinput::StartContext::Explicit), bleinput::StartResult::Started);
  EXPECT_EQ(renderer.cache.clears, 0);
  EXPECT_EQ(renderer.cache.releases, 0);
  EXPECT_EQ(sdFontSystem.unloads, 0);
}

TEST_F(BleInputTest, InsufficientRecoveryReturnsAfterOneAttempt) {
  for (const auto context : {bleinput::StartContext::Reader, bleinput::StartContext::Explicit}) {
    renderer.cache = {};
    sdFontSystem = {};
    freeInternal = 0;
    EXPECT_EQ(start(context), bleinput::StartResult::LowMemory);
    EXPECT_EQ(renderer.cache.releases + renderer.cache.clears, 1);
    EXPECT_EQ(BleHid.begins, 0);
    EXPECT_EQ(probes, 0);
  }
}

TEST_F(BleInputTest, ReportsStartFailureAndStopIsIdempotent) {
  BleHid.beginResult = false;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Failed);
  BleHid.beginResult = true;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Started);
  bleinput::stop();
  bleinput::stop();
  EXPECT_EQ(BleHid.ends, 1);
}
#if CROSSPOINT_BLE_HOST_PSRAM
TEST_F(BleInputTest, RejectsMissingOrInsufficientPsramBeforeAllocating) {
  for (const size_t available : {size_t{0}, bleinput::kMinFreePsram - 1}) {
    freePsram = available;
    EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::LowMemory);
  }
  freePsram = bleinput::kMinFreePsram;
  largestPsram = bleinput::kMinLargestPsram - 1;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::LowMemory);
  EXPECT_EQ(probes, 0);
  EXPECT_EQ(BleHid.begins, 0);
  largestPsram = bleinput::kMinLargestPsram;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Started);
  EXPECT_EQ(probes, 1);
  EXPECT_EQ(probeFrees, 1);
}

TEST_F(BleInputTest, ProbeFailureOrWrongPoolNeverStartsHost) {
  probeFails = true;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Failed);
  EXPECT_EQ(probeFrees, 0);
  probeFails = false;
  probeExternal = false;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Failed);
  EXPECT_EQ(probeFrees, 1);
  EXPECT_EQ(BleHid.begins, 0);
}
#else
TEST_F(BleInputTest, InternalModeNeedsNoPsramOrProbe) {
  freePsram = largestPsram = 0;
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Started);
  EXPECT_EQ(probes, 0);
}
#endif
#else
TEST_F(BleInputTest, DisabledBuildNeverStartsHost) {
  EXPECT_EQ(start(bleinput::StartContext::Reader), bleinput::StartResult::Unavailable);
  bleinput::stop();
  bleinput::logDiagnostics("disabled");
  EXPECT_EQ(BleHid.begins, 0);
  EXPECT_EQ(BleHid.ends, 0);
  EXPECT_EQ(probes, 0);
}
#endif
