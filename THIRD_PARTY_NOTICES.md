# Third-party notices

This file summarizes direct build dependencies. PlatformIO downloads their
source and license files during the build; they are not vendored in this
repository.

| Component | Pinned version | License | Project |
|---|---:|---|---|
| SdFat | 2.3.1 | MIT | <https://github.com/greiman/SdFat> |
| Adafruit GFX Library | 1.12.6 | BSD-3-Clause | <https://github.com/adafruit/Adafruit-GFX-Library> |
| Adafruit BusIO | resolved by Adafruit GFX | MIT | <https://github.com/adafruit/Adafruit_BusIO> |
| NimBLE-Arduino | 2.5.0 | Apache-2.0 | <https://github.com/h2zero/NimBLE-Arduino> |
| Arduino core for ESP32 | 3.3.7 in pinned platform | LGPL-2.1 | <https://github.com/espressif/arduino-esp32> |
| ESP-IDF libraries | 5.5 lineage in pinned platform | Apache-2.0 and component-specific terms | <https://github.com/espressif/esp-idf> |
| pioarduino platform-espressif32 | 55.03.37 | Apache-2.0 | <https://github.com/pioarduino/platform-espressif32> |

The table is an aid, not a replacement for the complete license files shipped
with each dependency. A binary publisher must preserve notices and satisfy the
license obligations of the exact dependency artifacts used for that build.

The project also contains FreeInk-derived source and binary-derived EEGO A4
interoperability data. Those are documented separately in
[`NOTICE.md`](NOTICE.md).
