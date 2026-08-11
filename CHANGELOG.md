# Changelog

This project follows [Semantic Versioning](https://semver.org/).

## 1.0.18 - 2026-07-26

### Included

- Calibrated `28,28 496×712 R0` rectangular UI contract inside the rounded
  `12,12 528×744 R48` safe area.
- UC8279C display, GSLX680 touch, physical and capacitive keys, microSD,
  PCF8563 RTC, battery/charge input, Wi-Fi, BLE, USB CDC, optional LM3630A
  probing, safe sleep, and CPFONT v4 Chinese/English rendering.
- A complete non-destructive diagnostic app, five buildable examples, guarded
  flashing/recovery tools, and hardware/API documentation.

### Verified

- Built all six PlatformIO environments.
- Flashed the diagnostics app to a no-frontlight EEGO A4.
- Verified display, touch, keys, storage, RTC, battery/charge input, wireless,
  USB, safe sleep, safe-area layout, and Chinese/English fonts on hardware.
