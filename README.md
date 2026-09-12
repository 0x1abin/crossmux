# CrossMux

**English** | [简体中文](./README.zh-CN.md)

**CrossMux** is a community fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for ESP32 e-ink devices. Reading comes first, with lightweight apps, reading analytics, standby faces, and on-demand services alongside the reader.

[Releases](https://github.com/0x1abin/crossmux/releases) · [User guide](./USER_GUIDE.md) · [Contributing](./docs/contributing/README.md)

![CrossMux running on an Xteink device](./docs/images/cover.jpg)

## Features

- **Reading and library**: EPUB, TXT, XTC/XTCH and images; chapter navigation, bookmarks, dictionaries, custom fonts, reading backgrounds, and KOReader progress sync.
- **Wireless workflows**: browser file transfer and settings, Calibre wireless, OPDS downloads, WebDAV, and device OTA updates.
- **Apps**: lightweight games and tools including Sudoku, Gomoku, Chinese Chess, Minesweeper, 2048, Electronic Woodfish, and Ugly Avatar. [Apps guide](./src/activities/apps/README.md).
- **AirPage**: scan to upload content, then display BMP/JPEG images with manual refresh or foreground live delivery; images can become a sleep screen. [Usage and network behavior](./src/activities/apps/README.md#airpage).
- **WeRead**: QR login, bookshelf browsing, EPUB downloads for offline reading, and progress sync. Available in the China content profile. [WeRead guide (Chinese)](./src/activities/apps/weread/README.md).
- **Reading analytics and standby**: reading statistics, heatmaps, profiles and achievements; clock and Chinese almanac faces. [Analytics guide](./src/activities/apps/reading-stats/README.md).
- **Languages and development**: 33 UI languages in one firmware per hardware target, plus desktop simulators for UI development.

> **WeRead security:** this unofficial Web protocol may change. Device traffic is encrypted, but its client does not verify the server certificate or hostname; use it only on a trusted network. The native simulator verifies certificates through the host trust store. See the [transport details](./docs/engineering/chinese-build.md#weread-transport).

## Devices and release channels

| Device | Chip | Published channels |
|---|---|---|
| Xteink X3 / X4 (shared image) | ESP32-C3 | Stable, Nightly |
| Seeed Sticky | ESP32-S3 | Nightly |
| Xteink X4 Pro | ESP32-S3 | Nightly |
| M5Stack Paper Mono | ESP32-S3 | Nightly |
| eego A4 | ESP32-S3 | Nightly |
| Murphy M4 | ESP32-S3 | Nightly |
| Waveshare ePaper 3.97 | ESP32-S3 | Nightly |
| [Metalio E-Ink 4](./docs/engineering/metalio-eink4.md) | ESP32-S3 | Nightly |

This table describes configured release targets, not a claim that every feature has passed hardware acceptance. Each S3 target needs its own image. X4 Classic has a build-only target and is absent from public release/OTA indexes. See [device variants](./docs/engineering/device-variants.md) for target-specific limitations.

Use [Stable](https://github.com/0x1abin/crossmux/releases/tag/stable) for the stable X3/X4 channel, or [Nightly](https://github.com/0x1abin/crossmux/releases/tag/nightly) for development builds. The [target table](./scripts/nightly_targets.py) defines channels and artifact names; [release architecture](./docs/engineering/firmware-release.md) explains packaging and OTA. Current source version and build environments live in [platformio.ini](./platformio.ini).

## Install firmware

1. Open [CrossMux Releases](https://github.com/0x1abin/crossmux/releases), choose the channel and exact device, and follow that release's asset links and installation notes. X3/X4 share an image; S3 images are board-specific.
2. Back up your SD card data before changing firmware. Use the matching installation package; an application-only `firmware.bin` is not a complete first-install image.
3. For an existing X3/X4 installation, the [upstream CrossPoint web flasher](https://crosspointreader.com/#flash-tools) offers a custom binary upload: select X3/X4 and upload the **CrossMux** application binary. Choosing an upstream release installs CrossPoint instead.
4. For S3 installation and recovery, follow the matching [device documentation](./docs/engineering/device-variants.md) and release instructions. Do not reuse X3/X4 flash commands or offsets for another board.

To build and flash X3/X4 from source, use the [development commands](#development-quick-start) below. For an existing CrossMux installation, device OTA selects the model, content profile, and channel; S3 targets have no Stable channel.

Metalio E-Ink 4 uses the `metalio-eink4` Nightly package and model/board tag `metalio_eink4`. Both language entries point to the same multilingual firmware. Follow the [Metalio guide](./docs/engineering/metalio-eink4.md) for first installation, wiring, and hardware validation status. The [global Web tool](https://crossmux.com) and [China Web tool](https://crossmux.cn) show its install option once Web support is deployed and a matching Nightly package is present in the release catalog.

### USB-locked Xteink devices

Some devices may restrict USB flashing. The [upstream Xteink Unlocker](https://crosspointreader.com/#unlock-tool) is a separate tool; consult its current compatibility and recovery instructions before use. CrossMux compatibility with a locked device must not be inferred from CrossPoint compatibility. Flashing unsupported firmware may leave the device without a recovery path. If the serial device is missing, also check the data cable, port, and browser permissions.

## Chinese fonts and content profiles

Every hardware target builds one language-unified firmware. Simplified Chinese selects the China content profile (`crossmux.cn`); other UI languages select Global (`crossmux.com`). Changing the UI language updates the profile and regional apps, including WeRead and Chinese Chess.

The UI includes compact 8/10/12pt Simplified-Chinese fallback fonts. Built-in reader font choices share a 12pt offline fallback; complete families, other sizes, style variants, and broader Unicode coverage use SD-card `.cpfont` files. Embedded fonts are a subset, so rare or Traditional Chinese characters may require an appropriate SD font.

Download fonts from **Settings > Reader > Manage Fonts**, or copy converted fonts to the SD card. See [SD-card fonts](./docs/sd-card-fonts.md) for installation and conversion, and [Chinese support](./docs/engineering/chinese-build.md) for the embedded-font toolchain. Normal builds need no font regeneration.

## Development quick start

Install PlatformIO Core (`pio`) and Python 3; the repository pins its pioarduino platform. Full code checks also need clang-format 21+, CMake, and Ninja. See [Getting Started](./docs/contributing/getting-started.md) for setup.

```bash
git clone --recursive https://github.com/0x1abin/crossmux.git
cd crossmux

# If submodules were not initialized:
git submodule update --init --recursive

# X3/X4 development build
pio run -e default

# X3/X4 unified-language stable build
pio run -e gh_release

# Build and flash that image to a connected X3/X4
pio run -e gh_release -t upload
```

The application binary is `.pio/build/gh_release/firmware.bin`. For other boards, use the matching environment in [build-system.md](./docs/engineering/build-system.md).

For Metalio E-Ink 4:

```bash
pio run -e metalio_eink4
CROSSPOINT_RC_HASH=$(git rev-parse --short=7 HEAD) pio run -e metalio_eink4_nightly
```

The development application is `.pio/build/metalio_eink4/firmware.bin`; first installation also requires the matching bootloader and partition layout described in the [Metalio guide](./docs/engineering/metalio-eink4.md).

### Desktop simulator

Install SDL2 and curl (plus OpenSSL development headers on Linux), place EPUBs in `fs_/books/`, then run:

```bash
pio run -e simulator -t run_simulator           # X4
pio run -e simulator_x3 -t run_simulator        # X3
pio run -e simulator_eego_a4 -t run_simulator   # eego A4
pio run -e simulator_murphy_m4 -t run_simulator # Murphy M4
```

The [CrossMux simulator fork](https://github.com/0x1abin/crosspoint-simulator) is pinned in `platformio.ini`. It previews UI and input flows; it does not validate display waveforms, power consumption, or physical hardware timing.

### Checks and debugging

```bash
./bin/ci-check       # Full code-change checks; does not rewrite sources
pio device monitor  # Serial logs from a connected device
```

Use [Testing and Debugging](./docs/contributing/testing-debugging.md) for focused checks and the enhanced serial monitor. Documentation-only changes need link, command, and whitespace checks rather than firmware builds.

## Documentation and contributing

- [User guide](./USER_GUIDE.md) · [Web transfer](./docs/webserver.md) · [Web API](./docs/webserver-endpoints.md)
- [Project scope](./SCOPE.md) · [Governance](./GOVERNANCE.md) · [Contributor guide](./docs/contributing/README.md)
- [Agent instructions](./AGENTS.md) · [Engineering reference](./docs/engineering/index.md) · [Touch and UI](./docs/contributing/touch-and-ui.md)
- [Cache management](./docs/engineering/cache-management.md) · [File formats](./docs/file-formats.md)

Report bugs and propose changes in [CrossMux Issues](https://github.com/0x1abin/crossmux/issues). Contributions target **`0x1abin/crossmux:main`**; keep each PR focused and describe its verification. Existing CrossPoint class names and the `/.crosspoint` SD data directory remain compatibility details, not instructions to target the upstream repository. That directory also holds settings and progress; do not delete it merely to clear a book cache.

## Credits

Thanks to [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), [Inx](https://github.com/obijuankenobiii/inx), [cpr-vcodex](https://github.com/franssjz/cpr-vcodex), and their contributors, and to [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader) for the original inspiration.

CrossMux is not affiliated with Xteink or any device manufacturer. Upstream tools and communities are independent of this fork. See [LICENSE](./LICENSE) for the repository license.
