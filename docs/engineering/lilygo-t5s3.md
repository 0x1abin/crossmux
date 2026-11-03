# LilyGo T5 S3 4.7 PRO / Lite development target

`lilygo_t5s3` is a separate ESP32-S3 N16R8 build for the upstream SDK LilyGo
T5 S3 4.7 profile. It is a local development target and is absent from the
Nightly, channel and OTA registries. X3/X4 keep their shared C3 image; existing
S3 environment names and fork-only profiles remain intact.

```sh
pio run -e lilygo_t5s3
```

## SDK and HAL contract

Reuse SDK `BoardT5S3` and `lilygoT5S3LgfxConfig`, not a second local board driver.
The environment selects `FREEINK_DEVICE_LILYGO`, the SDK parallel-panel config
and M5GFX Git tag `0.2.20`. Preserve the prebuilt `dio_opi` core graph, DIO flash,
OPI PSRAM and existing partition sizes. The old registry sample `M5GFX @ 0.2.20`
is not the dependency used by this target.

The ED047TC1 parallel panel is 960×540, with a 64,800-byte B/W frame. GT911 touch,
PCA9535 expander, TPS65185 EPD power, BQ27220 gauge/BQ25896 charger and PCF8563 RTC
belong to the SDK. `BoardT5S3::begin()` initializes I²C SDA39/SCL40, prepares SD
SPI SCLK14/MISO21/MOSI13/CS12, deselects LoRa CS46 and registers the expander
button hook before input/storage/display initialization through `HalGPIO`.
BOOT GPIO0 is Power; the expander button is Down. Frontlight uses GPIO11 PWM.
GPIO2 retains the power latch.

The SDK's static panel config uses rotation 0, a 16 MHz parallel clock and
`pinOe=-1`: output-enable comes from the expander. GPIO46 is held high to avoid
selecting the shared LoRa SPI device. Full/Half use the text waveform and Fast
uses the fast waveform. Keep SDK allocation checks and PSRAM-only failure
handling while adopting these waveforms.

## Memory and sleep

The existing Lgfx 8-bit canvas is 518,400 bytes, plus two 64,800-byte planes:
648,000 bytes of PSRAM, excluding M5GFX panel/DMA state and the B/W framebuffer.
The geometry/config is static; CrossMux adds no second UI canvas or generic
adapter framework. Measure internal and PSRAM free bytes/largest blocks at boot,
book open, grayscale, WiFi and exit; total free heap does not establish internal
DMA capacity. A failed allocation must leave an error/fallback, not a partial
initialized display.

Display power-off follows the SDK hook; retain GPIO2 before deep sleep. GPIO0
is the deep-sleep wake source. The expander button is not a deep-sleep wake
source. RTC restore/writeback uses the shared PCF8563 bus. Verify actual power
sequencing, retained sleep screen, BOOT wake and post-wake SD/touch/frontlight
on the physical board.

## Acceptance and references

Builds and host/simulator checks do not establish electrical or optical
acceptance. Test both retail PRO/Lite variations against their actual panel,
PSRAM, expander and frontlight population. Exercise four orientations, touch,
buttons, SD, Full/Half/Fast/grayscale, charging, network transitions, and repeated
USB/battery sleep/wake; record firmware hash and memory/stack minima.

- [Build environment](../../platformio.ini)
- [HAL input initialization](../../lib/hal/HalGPIO.cpp)
- [HAL sleep](../../lib/hal/HalPowerManager.cpp)
- [SDK board API](../../freeink-sdk/libs/hardware/BoardT5S3/include/BoardT5S3.h)
- [SDK integration notes](../../freeink-sdk/docs/lilygo-t5s3-support.md)
- [Release target registry](../../scripts/nightly_targets.py)


### Pinned M5GFX allocation audit

The actual Git dependency at tag `0.2.20` resolves to
`d8074bbac8b75d22149d128d71d55b6a34dbc808`. At 960×540, its
`LGFX_Sprite.cpp` requests 518,401 bytes for the 8-bit canvas (one guard byte)
and `common.hpp` rounds this to 518,404 bytes. `SpriteBuffer.cpp` may fall back
from PSRAM to DMA memory; the SDK checks `esp_ptr_external_ram()` and releases
the canvas on null or non-PSRAM storage. The two SDK planes still request exactly
64,800 bytes each. These checked, persistent buffers are reused after initialization.

The pinned `platforms/esp32/Panel_EPD.cpp` additionally allocates:

| Buffer | Requested bytes | Capability |
| --- | ---: | --- |
| Step framebuffer | 1,036,800 | PSRAM, 16-byte alignment |
| Current 4-bit pixels | 259,200 | PSRAM, 16-byte alignment |
| Two scanline buffers | 248 each | DMA |
| Pixel-pair LUT | 45,056 | DMA |

The LUT uses 88 steps: eraser 4, quality 32, text 32, and the SDK's fast/fastest
10 each. This makes the known canvas/planes/panel PSRAM requests 1,944,004 bytes,
plus the SDK's 64,800-byte single B/W framebuffer (PSRAM-first, checked malloc
fallback). The display task requests a 4,096-byte stack and an eight-entry update
queue; allocator overhead, bus-driver state and FreeRTOS control blocks are extra.
These are source-derived allocation requests, not measured heap margins. Record
internal/PSRAM free and largest blocks plus task high-water marks on real hardware;
no runtime margin is claimed by the host or firmware build.
