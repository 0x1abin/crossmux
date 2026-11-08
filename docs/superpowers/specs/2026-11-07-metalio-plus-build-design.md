# Metalio E-Ink4-Plus V1.4: first CrossMux integration

Continuation of the user's approved independent-directory port. The first native-IDF frame, USB recovery, PSRAM sample and restoration have passed on the connected V1.4 device. This stage produces a buildable CrossMux target; device installation follows after the complete runtime initialization is reviewed.

## Approach

Use a separate ESP32-S31 environment, a new SDK board identity and a dedicated raw-parallel display backend. Retain CrossMux's HAL and reader rendering interfaces. Reuse the exact vendor display sources already tested on this device. The older Metalio E-Ink4 SPI profile cannot drive the Plus. Extending ReadPico's S3 LCD_CAM implementation would couple two different DMA/timing backends; retain it unchanged.

This is the implementer's choice under the user's instruction to continue the proposed S31/display integration. No new UI or release channel is required.

## Boundaries

- Pin CrossMux to `0ab0af23f69cca73af7cf0143eafbfe9f5d6d648`, its SDK to `d9a1304cd234b0536136302f629304b609f62b32`, and vendor display source to `2b2c36ae58f32a9cb1a721d1000883a2693651a4`.
- Add `FREEINK_DEVICE_METALIO_EINK4_PLUS` / `FREEINK_MCU_S31`; do not alias S31 to S3 or add it to X3/X4 runtime detection.
- Native geometry 1216×684, 103,968-byte 1bpp facade buffer in PSRAM. Reuse the vendor 4bpp highlevel buffers; add no second native framebuffer.
- Initial backend supports synchronous B/W full refresh. Half/fast/window requests use the same checked full-frame fallback. Gray and async capabilities stay disabled until separately verified.
- A commit succeeds only after checked clear, checked draw and confirmed display power-off. Failures leave display history unknown. Failed power-off inhibits further panel work until reinitialization.
- Vendor initialization uses the existing native I2C bus implementation. Other I2C peripherals remain disabled in this initial build; a shared-bus ownership design is required before enabling Arduino Wire clients.
- Other inputs, SD, frontlight, audio, Bluetooth and sleep acceptance are separate stages. Profiles must not borrow another board's active pins.
- Build-only upload guard rejects upload/erase/uploadfs/debug. Arduino's prebuilt S31 core uses 250MHz PSRAM; this build must not be flashed. The already validated native runtime used 200MHz. A 200MHz Arduino/core integration is mandatory before runtime acceptance.
- Preserve ThingsOS layout and app offset 0x220000. Do not write the device in this stage. No commits, pushes or release registration.

## Evidence

- `platformio.ini:27`: existing shared platform; `:788`: older Metalio S3/SPI target; `:915`: ReadPico external-bus target.
- `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h:115-125`: MCU families; `:2368-2370`: maximum framebuffer; `:2375`: default profile.
- `freeink-sdk/libs/display/FreeInkDisplay/src/FreeInkDisplay.cpp:285-288`: external-bus backend selection.
- `../../bringup-display-v14/src/main.c:118-148`: tested checked clear/draw/power-off sequence.
- `../../sources/metalio-display-reference/main/boards/metalio-e-ink4-plus/config.h:57-103`: V1.4 control pins and native I2C bus.

## Acceptance

Compile production adapter against a recording vendor API: byte polarity/stride and final pixel, init/power/clear/draw failures, successful retry, synchronous/window fallback and power-off inhibition. Compile actual BoardConfig for Plus and existing C3/S3 boards. Build the full new CrossMux environment and run applicable existing SDK tests plus repository checks where installed prerequisites permit. Record failures and missing measurements separately. No hardware success claim follows from compilation.
