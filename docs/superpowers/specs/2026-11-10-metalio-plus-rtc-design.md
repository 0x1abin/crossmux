# Metalio Plus native RTC integration

The user is away and authorized independent adaptation development, deferring
manual acceptance until return. Implement RTC timekeeping as a separate increment
on top of the preserved four-gray candidate. Do not flash, commit or push implicitly.

## Evidence and approach

Vendor V1.4 `config.h:87-91` identifies PCF8563 at 0x51 on native I2C1 SDA0/SCL1,
with INT on TCA P16, not an ESP GPIO. `MetalioPlusBoot.cpp:44-65` already brings up
the same idempotent native bus before SD; `HalGPIO::begin()` calls it before
`main.cpp:954` calls `halClock.begin()`. No display initialization or new Wire bus
is needed. Generic `Rtc.cpp` currently creates Wire/Wire1 and disables CLKOUT on
begin; Plus must instead reuse the native owner and leave unrelated RTC state alone.

Use the existing `Rtc`/`HalClock` contract with a Plus-native transport. Other
platform paths stay unchanged. Alternative creating Wire1 is electrically incompatible
with the native owner; deferring the whole clock until display would also reorder
trusted-time startup unnecessarily. Reuse the existing early native bus instead.

## Contract

- Profile: PCF8563, address0x51, native bus1, SDA0/SCL1/100kHz; no IMU/temp capability.
- Begin only probes control0 through the native owner, with bounded retries. No
  RTC register writes, expander reset, bus creation or display initialization.
- Native time reads capture control0 through year in one nine-byte transaction;
  reject STOP, TEST modes, VL, malformed BCD, impossible dates and unsupported
  years. Preserve the output on failure. UTC convention remains HalClock's existing
  2000–2099 register encoding; previous firmware's timezone interpretation is
  unverified and must be checked/re-synced with trusted time during acceptance.
- An explicit set validates the full date first. Under one native mutex, stop the
  clock, write exactly seven time bytes at0x02, then restart it. Clear reserved
  control0 bits as required; refuse TEST1 external-clock mode. An explicit trusted
  set may clear TESTC POR override to normal operation, per NXP8.11.1; read-only
  begin/now still reject an untrusted TESTC calendar and do not clear it. A partial failed time write leaves
  STOP asserted, so a mixed calendar cannot become a valid time on reboot. Preserve
  control2/alarm, CLKOUT and timer registers. No alarm/RTC wake routing is enabled.
- Keep transport/codec failures false, retain the software clock, and do not
  initialize the native bus on read failure. No new heap buffers/tasks or dependency.
  First access to0x51 uses the native owner's existing lazy IDF device-handle
  allocation; its internal heap cost is not measured. Subsequent transactions
  reuse that cached handle. Do not claim zero additional total heap use.
- Hardware acceptance: first verify UTC/timezone semantics, then trusted sync,
  offline reboot, manual sleep/wake and time continuity. Alarm wake, absolute
  drift/current/endurance measurement remain separate pending work.

Primary register/STOP/VL reference:
[NXP PCF8563 Rev11.1](https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf),
sections8.2–8.4 and8.10. Source evidence is the matching pinned V1.4 vendor board,
not the separate S3 Metalio device.
