# CrossPoint Reader Simulator

Runs the CrossPoint Reader firmware UI/EPUB logic on a desktop machine (macOS/Linux)
without flashing the Xteink X4 hardware. Backed by SDL2 for window+input and a POSIX
filesystem for the "SD card".

## Layered architecture

```
┌─ src/ + lib/ (CrossPoint application code, unchanged) ────┐
│  Activities, GfxRenderer, Epub engine, I18n, fonts        │
└────────────────────────┬──────────────────────────────────┘
                         │ HAL interface (lib/hal/Hal*.h)
┌────────────────────────▼──────────────────────────────────┐
│ simulator/hal_native/ — CrossPoint-specific HAL backends   │
│   HalDisplay_native:  48 KB framebuffer → SDL2 texture     │
│   HalGPIO_native:     SDL keys → BTN_* logical buttons     │
│   HalStorage_native:  POSIX files under sd_root/           │
│   HalPower/Tilt/System: stubs (battery 87%, no IMU)        │
│ simulator/missing_symbols.cpp — link glue:                 │
│   • Activity vtables for excluded screens (WiFi/OTA/...)   │
│   • Image-decoder + obfuscation symbols (excluded)         │
│   • MySerialImpl::instance + uzlib checksum stubs          │
│   • WiFi global instance                                   │
└────────────────────────┬──────────────────────────────────┘
                         │
┌────────────────────────▼──────────────────────────────────┐
│ simulator/shims/ — project- and ecosystem-specific headers │
│   • open-x4-sdk:   EInkDisplay, InputManager, BatteryMonitor, SDCardManager │
│   • SdFat:         common/FsApiConstants.h                 │
│   • Arduino-ESP32: WiFi, NetworkClient(Secure), HTTPClient, StreamString, NetworkUdp, WebServer, WebSocketsServer, MD5Builder, esp_ota_ops │
│   • 3rd-party:     PNGdec, JPEGDEC, base64                 │
│   Most are no-op/failure stubs; HTTPClient is libcurl-     │
│   backed (real network — see below). QRCode is fetched,    │
│   not shimmed.                                             │
└────────────────────────┬──────────────────────────────────┘
                         │ depends on
┌────────────────────────▼──────────────────────────────────┐
│ simulator/arduino-host/ — project-independent runtime      │
│   Arduino core + FreeRTOS + ESP-system APIs over           │
│   std::thread, std::chrono, std::mutex, std::cv.           │
│   Zero SDL / project-specific dependencies. Own CMake      │
│   target + headless example + tests.                       │
└───────────────────────────────────────────────────────────┘
```

The boundary between `arduino-host/` and `simulator/shims/` is strict: **arduino-host
only contains Arduino core + FreeRTOS + ESP-IDF base APIs**. Anything Arduino-ESP32
ecosystem (WiFi, HTTPClient, OTA, web servers) or third-party (image codecs, base64)
lives in `simulator/shims/`. The QRCode lib is fetched (`FetchContent`, like
ArduinoJson) rather than shimmed, so the real generator runs. That keeps `arduino-host`
cleanly reusable —
`grep -rni "crosspoint|xteink|epub|wifi|http|opds|qrcode"` over its tree returns no
hits (apart from a single comment that documents what is *not* in scope).

Once `arduino-host`'s API has been validated by a second consumer project, it can be
lifted out into its own repository (or vendored via `add_subdirectory` /
`FetchContent`) with no further refactoring.

## Build

```sh
# Prerequisites
brew install sdl2 cmake      # macOS
# apt install libsdl2-dev cmake  # Linux

# From repo root:
cmake -S simulator -B simulator/build
cmake --build simulator/build -j
```

CMake fetches ArduinoJson and `ricmoo/QRCode` via `FetchContent` on first configure
(shallow clones, a few seconds).

## Run

```sh
./simulator/build/crosspoint_simulator                    # opens 800×480 window
./simulator/build/crosspoint_simulator --sd-root /tmp/sd  # custom sd_root
./simulator/build/crosspoint_simulator --scale 2          # 2× window magnification
```

Keyboard mapping (matches `MappedInputManager::Button::*`):

| Key       | Logical button | Default mapping              |
|-----------|----------------|------------------------------|
| Backspace | BTN_BACK       | Back / cancel                |
| Enter     | BTN_CONFIRM    | Select / next                |
| ← / →     | BTN_LEFT/RIGHT | Page back / forward (reader) |
| ↑ / ↓     | BTN_UP/DOWN    | Cursor / scroll              |
| Esc       | BTN_POWER      | Power button (exits sim)     |
| F1        | —              | Toggle simulator settings overlay |

## Host-side settings

Press **F1** to summon a translucent settings panel rendered on top of the eink
area. Left-click a row to toggle that option. Press F1 again to dismiss.

Available toggles:

- **Show device shell** — wraps the eink area in an antialiased, rounded
  black bezel modeled on the real Xteink X4 (114 × 69 × 5.9 mm @ 220 PPI →
  598 × 987 px window at 1×). Renders three right-side button bumps
  (Power / Previous / Next) and four front-face buttons along the bottom
  in a 2+2 grouping (Back+OK · Next+Previous). Turning it off shrinks
  the window back to a bare 480×800.

Settings persist to `~/.crosspoint_simulator.json` (or `./simulator_settings.json`
if `$HOME` is unset). This file is host-only — it has no effect on the firmware's
own settings, which live on the simulated SD card under `.crosspoint/`.

## SD card layout

Drop an `.epub` into `simulator/sd_root/`. The CrossPoint cache lives under
`simulator/sd_root/.crosspoint/` (gitignored):

```
simulator/sd_root/
├── sample.epub
└── .crosspoint/
    ├── settings.json
    ├── state.json
    ├── recent.json
    └── epub_<hash>/
        ├── book.bin           # metadata cache
        ├── css_rules.cache
        ├── progress.bin       # reading position
        └── sections/0.bin     # rendered section cache
```

To boot directly into the reader (skip the home screen, useful for testing rendering):

```json
{
  "openEpubPath": "/sample.epub",
  "lastSleepFromReader": true,
  "readerActivityLoadCount": 0,
  "recentSleepImages": []
}
```

Save as `simulator/sd_root/.crosspoint/state.json` before launching.

## What works in first version

- Boot → Home → Settings / FileBrowser / RecentBooks navigation
- EPUB engine end-to-end: zip extraction, content.opf/toc.xhtml parse, CSS
  parse, section layout, font glyph rendering, page progress save/load
- All on-disk caches (book.bin, sections/*.bin, css_rules.cache, progress.bin)
- Multi-language UI via the `tr()` macro (generated I18nStrings)
- 1-bpp framebuffer rendering and font system (text-only EPUBs)
- **WeRead (微信读书) app: real HTTPS to i.weread.qq.com via libcurl.** WiFi
  shim reports `WL_CONNECTED`; HTTPClient/NetworkClientSecure shims delegate to
  libcurl on the host. Drop your `wrk-…` API key as plain text into
  `simulator/sd_root/.crosspoint/weread_apikey_plain.txt` — first boot migrates
  it to base64-stored `weread_apikey.txt` and deletes the plain seed.
- **AirPage standby face: real QR + real cloud image fetch.** The QR (rendered
  by the real `ricmoo/QRCode` lib) encodes the device's upload URL. Pressing ▼
  runs the real `HttpDownloader` over libcurl, saving the latest image to
  `sd_root/.crosspoint/airpage/latest.bmp`, then Confirm toggles QR ⇄ image.
  The simulated MAC is fixed (`02:00:00:00:00:01`), so the 16-char device id is
  stable across runs (`NSlZI3rjcCSnrbDQ`) — upload to that id to see your image.

## What's out of scope (first version)

- WiFi config UI / OPDS / KOReader sync / Calibre / file transfer / OTA / web server
- Image rendering inside EPUBs (PNG/JPEG decoders are stubbed)
- 4-level grayscale (grayscale buffers fall through to 1-bpp — e.g. the AirPage
  image renders in plain B&W in the sim, though the BW frame still displays)
- Real e-ink refresh timing / ghosting

Activities for those features still build (we provide empty out-of-line stubs in
`missing_symbols.cpp`) but the underlying functionality returns failure if the
user navigates into them.

## Architectural compromises (worth knowing)

- **FreeRTOS uses real OS threads** (`std::thread`, not single-thread cooperative
  scheduling). The firmware's `[[noreturn]] renderTaskLoop` does
  `while (true) { ulTaskNotifyTake(portMAX_DELAY); ... }`, which can only be
  modelled correctly with real blocking. A future single-thread mode would need
  stackful coroutines (ucontext) — not built yet.
- **`SDL_HINT_NO_SIGNAL_HANDLERS` + `std::_Exit(0)`** is set so SIGTERM (e.g. from
  `timeout`, Ctrl+C, the OS shutting the window) terminates immediately instead of
  walking out through C++ static destruction. Several firmware singletons —
  `ActivityManager` most notably — have destructors that assert `false` because
  the device only ever resets; a "clean" exit would trip them.
- **Activities whose `.cpp` files are excluded** (WiFi / OPDS / OTA / KOReader sync /
  Calibre / SD firmware update / font download) have their virtual overrides
  defined empty in `missing_symbols.cpp` so the vtable links. Navigating to those
  screens in the simulator shows an empty screen rather than crashing.
- **`simulator/shims/` is mostly a parse-only layer.** Headers like `<PNGdec.h>`
  or `<JPEGDEC.h>` exist so consumer `.cpp` files compile and link, but their
  methods return an error / empty value. The exception is `<HTTPClient.h>` (and
  `NetworkClient(Secure)`), which is **libcurl-backed and does real host network
  I/O** — that's what lets WeRead and the AirPage fetch work. Adding a real codec
  (libpng wrapper, etc.) would similarly light up the stubbed features.

## Verifying arduino-host stays project-independent

```sh
cmake -S simulator/arduino-host -B simulator/arduino-host/build \
    -DARDUINO_HOST_BUILD_EXAMPLES=ON -DARDUINO_HOST_BUILD_TESTS=ON
cmake --build simulator/arduino-host/build -j
./simulator/arduino-host/build/examples/blink_headless/blink_headless
ctest --test-dir simulator/arduino-host/build
grep -rni "crosspoint\|xteink\|epub" \
    simulator/arduino-host/include simulator/arduino-host/src    # should print nothing
```
