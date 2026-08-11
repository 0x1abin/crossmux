# EEGO A4 Hardware Template Firmware

[中文 README](README.md)

An independent ESP32-S3 hardware-support, diagnostics, and development baseline for
the EEGO A4 e-paper reader. It combines the official firmware's touch
calibration and key mapping with the faster UC8279C refresh behavior recovered
from CrossLink firmware.

This is not an everyday reading application. It is a hardware template for
developers who need a known-good boot path, reusable drivers, non-destructive
diagnostics, examples, safe flashing, and recovery documentation.

## Quick start

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python scripts/validate_project.py --quick
pio run -e eego_a4_quickstart
```

Start with the [documentation map](docs/README.md) and
[getting-started guide](docs/GETTING_STARTED.md). The detailed documentation
is currently Chinese-first; source identifiers, commands, tables, and examples
are language-independent.

## Included

- UC8279C full, fast, and four-level grayscale refresh
- full GSLX680 controller upload and calibrated portrait touch mapping
- physical Up, Down, and Power buttons plus short/long front capacitive key
- microSD, PCF8563 RTC, battery ADC, charge-state input
- Wi-Fi and BLE scanning, native USB CDC, 16 MiB Flash and 8 MiB PSRAM checks
- optional LM3630A frontlight probing without writing to absent hardware
- CPFONT v4 Chinese and English rendering tests from microSD
- five independently buildable examples and a complete diagnostic app
- guarded flashing with mandatory backup, target confirmation, and verification

Unsupported or unconfirmed components are not guessed into existence. Current
evidence does not establish an onboard speaker, microphone, LED, IMU, or
environment sensor.

## Safety contracts

- GPIO 4 is asserted at the first hardware operation to keep power latched.
- Automatic sleep is disabled. Explicit deep sleep is clamped to 3–60 seconds
  and always configures timer wake.
- Ordinary UI uses the tested `28,28 496×712 R0` rectangle inside the rounded
  `12,12 528×744 R48` safe area.
- Full-flash installation changes the partition table. Always preserve a new
  16 MiB backup first.
- Build and package commands never flash a connected device.

See [safe flashing and recovery](docs/RECOVERY.md) before writing hardware.

## Project layout

```text
src/                    diagnostic application
lib/EegoA4Support/      board contract, bootstrap, coordinate and power policy
lib/EegoA4Ui/           portrait canvas and CPFONT renderer
lib/*                   reusable FreeInk hardware drivers
examples/               five minimal applications
scripts/                validation, packaging, capture, extraction, safe flash
docs/                   developer, hardware, recovery, and maintainer docs
```

Generated `.pio/`, `release/`, and `captures/` directories are intentionally
excluded from source control. Publish release binaries as GitHub Release assets.

## Validation

Version 1.0.18 was built in all six PlatformIO environments and tested on a
physical no-frontlight EEGO A4. See the [validation summary](docs/VALIDATION.md)
for exact scope, hashes, framebuffer evidence, and limitations.

## Contributing and licensing

Read [CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md),
[NOTICE.md](NOTICE.md), and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before submitting
hardware-derived data. Source code is MIT licensed. Third-party firmware,
fonts, trademarks, and binary-derived interoperability data require separate
provenance review as described in the notice.
