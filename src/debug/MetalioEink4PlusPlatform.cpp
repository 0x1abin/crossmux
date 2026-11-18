#if defined(CROSSMUX_METALIO_EINK4_PLUS_PLATFORM)

#include <Arduino.h>
#include <esp_arduino_version.h>
#include <esp_system.h>
#include <sdkconfig.h>
#include <soc/soc_caps.h>

#if CROSSMUX_METALIO_EINK4_PLUS_SDK_PROBE
#include <BoardConfig.h>
#include <EpdiyLcd.h>
#include <FrontlightManager.h>

static_assert(FREEINK_MCU_S31 && FREEINK_DRIVER_EPDIY_LCD && FREEINK_FB_PSRAM);
static_assert(FREEINK_SD_SDMMC && BoardConfig::DEFAULT_DEVICE.sdmmc.busWidth == 4);
static_assert(FREEINK_CAP_FRONTLIGHT && FREEINK_CAP_WARMLIGHT);
static_assert(BoardConfig::DEFAULT_DEVICE.frontlight.maxDutyPercent == 10);
static_assert(BoardConfig::MAX_FRAMEBUFFER_BYTES == 103968);

namespace {
// Keep the real SDK entry points in the linked image without initializing the panel or frontlight.
auto* volatile displayBegin = &freeink::epdiyLcdBegin;
decltype(&FrontlightManager::begin) volatile frontlightBegin = &FrontlightManager::begin;
}  // namespace
#endif

#if !CONFIG_IDF_TARGET_ESP32S31
#error "The Metalio E-Ink 4 Plus platform probe requires ESP32-S31"
#endif

static_assert(ESP_ARDUINO_VERSION_MAJOR == 4);
static_assert(SOC_LCD_RGB_SUPPORTED && SOC_RMT_SUPPORTED);
static_assert(SOC_SDMMC_HOST_SUPPORTED && SOC_LEDC_SUPPORTED);

void setup() {
  Serial.begin(115200);
#if CROSSMUX_METALIO_EINK4_PLUS_SDK_PROBE
  Serial.printf("%s SDK link probe, display=%d, frontlight=%d\n", BoardConfig::DEFAULT_DEVICE.name,
                displayBegin != nullptr, frontlightBegin != nullptr);
#endif
}

void loop() {
  Serial.printf("CrossMux Metalio E-Ink 4 Plus platform probe: IDF %s, Flash %lu B, PSRAM %lu B\n",
                esp_get_idf_version(), static_cast<unsigned long>(ESP.getFlashChipSize()),
                static_cast<unsigned long>(ESP.getPsramSize()));
  delay(2000);
}

#endif
