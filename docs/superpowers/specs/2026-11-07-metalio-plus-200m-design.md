# Metalio Plus 200MHz core and peripheral contract

Continue the previously proposed S31 port in the existing independent checkout.
The user's instruction to continue covers the stated next step: rebuild the
core for the accepted 200MHz memory configuration and resolve peripheral bus
ownership before enabling input/storage. Implement inline; no commit or flash.

## Approach

Use the pinned pioarduino RC1 Arduino-as-IDF-component build with existing local
Arduino/IDF sources. Retain Arduino APIs and the SDK/HAL boundary. The initial
custom-sdkconfig investigation found that HybridCompile copies archives but
leaves the older IDF headers in place. Native component compilation keeps
headers, sources and archives in the same graph.
Changing application macros alone cannot change the precompiled PSRAM code.
Removing Arduino from the reader adds unrelated API porting work; adopting a newer unverified framework discards the established source pins.

The platform mutates Arduino component manifests while resolving dependencies. Use a separate package/core
cache under the independent directory; retain the previous prebuilt packages
and native probe toolchain. This isolation is an implementer decision. Select
200MHz octal PSRAM, DIO/80MHz 16MiB Flash and a 64KiB internal DMA reserve;
fail when mandatory PSRAM is absent. Retain the current ThingsOS partitions.
All upload/debug/erase protections remain active after a successful rebuild.

## Evidence and boundaries

- Reader `platformio.ini:831-841` selects prebuilt qio_opi/QIO scaffolding.
- Installed RC1 `builder/frameworks/arduino.py:475` invokes core compilation
  for custom sdkconfig; `:163` requires an owned .platformio package path for
  later restoration. `builder/frameworks/espidf.py:3197-3210` copies rebuilt
  archives and generated sdkconfig into the Arduino package.
- SDK `MetalioPlusEpd/src/lcd_arduino_rc1.c:8-10` asserts the old group-index
  LCD signatures. Recheck against the headers and archives actually rebuilt;
  do not cast pointers or edit the 74 frozen vendor files.
- SDK `MetalioPlusEpd/src/vendor/bus/metalio_i2c1.h:14-27` already exposes a
  single synchronous native bus and mutex. InputManager's current FT6336U
  Arduino Wire initialization would create a competing bus; it must not be
  enabled unchanged. Record exact reset, key-strip and SD prerequisites.

This milestone delivers a reproducible 200MHz complete build and a grounded
peripheral integration contract. Input/storage may only be enabled once their
ownership and startup order are implemented and covered. No CrossMux runtime,
cryptographic hardware, touch calibration or SD acceptance follows from build.

## Acceptance

Verify generated sdkconfig, compiled PSRAM archive provenance, DIO image,
partition identity and application headroom. Reject a 250MHz/stale core or
missing DMA reserve in the verifier, including negative fixtures. Compile any
LCD API adapter against the actual selected HAL. Run affected host checks,
formatting and strict static checks; retain known baseline failures as failures.
Document uncompleted peripheral and runtime work separately.
