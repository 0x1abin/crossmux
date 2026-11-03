# Read Pico: update onto upstream main (done)

> Historical record of the earlier port. For the active epdiy implementation,
> dependency checkout, review fixes and pending acceptance, use
> [read-pico.md](read-pico.md#current-implementation--2026-09-30).

Result: **the Read Pico port now builds on the latest upstream main.** `pio run -e
readpico` → SUCCESS, Flash 89.9% (5,892,271 B), RAM 32.6%.

## Final refs

| repo | ref | sha |
| --- | --- | --- |
| parent | `main` | `d7c1e6bb` (merge of `upstream/main` `01d13408`) |
| parent | `readpico-port-backup` | same |
| submodule | `readpico-port` | `ca707f0` (merge of `34d36ecc`) |
| submodule | `readpico-port-backup` | same |

Roll back with `git reset --hard readpico-port-backup` in the parent plus
`git submodule update`.

## What the update required

Two layers, and **both had to move**:

1. **Parent** (`0x1abin/crossmux` `80c67543` → `01d13408`, 59 commits). Only the
   `freeink-sdk` gitlink conflicted; `README*`, `docs/engineering/index.md`,
   `lib/hal/HalDisplay.{h,cpp}`, `lib/hal/HalGPIO.cpp`, `platformio.ini` and
   `src/main.cpp` auto-merged.
2. **SDK** (`0x1abin/freeink-sdk` `094976e` → `34d36ecc`, 101 commits), because the
   parent's new `HalDisplay.cpp` calls APIs that only exist in the newer SDK. The
   parent-only merge fails to link-to-compile with six errors:

   ```
   HalDisplay.cpp:106  'TextOnlyAntiAliasing' is not a member of RefreshContext
   HalDisplay.cpp:108  'ImageReading'         is not a member of RefreshContext
   HalDisplay.cpp:198  no member named 'supportsTextOnlyCombinedBase'
   HalDisplay.cpp:199  no member named 'supportsReaderTransitions'
   HalDisplay.cpp:200  no member named 'supportsContinuousImageReading'
   HalDisplay.cpp:201  no member named 'canUseTextTransition'
   ```

The SDK merge was 18 hunks in 6 files. `InputManager.cpp`, `InputManager.h`,
`Rtc.cpp`, `Imu.cpp`, `FreeInkDisplay.cpp`, `LgfxEpdDriver.cpp` and the
`SDCardManager` `.cpp`s auto-merged — including the two files most central to the port.

## The trap that cost the most time — read this before merging again

The readpico SDK work is mostly "add my device name to the project's device lists".
That makes most conflicts look like *both sides appended something*, and the obvious
resolution is "keep both". **For preprocessor and multi-line-macro hunks that is
wrong**, because the closing token is often a *shared* line outside the conflict:

- `#ifndef FREEINK_DEVICE_READPICO` / `#define … 0` conflicted with upstream's WS397
  pair, and the single `#endif` sat **outside** the conflict. Concatenating gave two
  `#ifndef`s and one `#endif`, so with `-DFREEINK_DEVICE_READPICO=1` the *entire*
  `BoardConfig.h` was skipped and `namespace BoardConfig` never existed. The symptom
  was misleading: `BatteryMonitor.cpp` / `LgfxEpdDriver.cpp` reporting
  `'BoardConfig' has not been declared`.
- The same shape broke the `#error` device guard, `FREEINK_MCU_S3`,
  `FREEINK_BATTERY_I2C_GAUGE`, `FREEINK_CAP_RTC`, `FREEINK_CAP_IMU`, `FREEINK_SD_SDMMC`
  and the profile-selector `switch` (whose `break;` + `#endif` were shared).

The correct resolution for a "both sides extended the *same* list" hunk is **one list
containing both names**, not two lists. Patch-on-patch repair of that file failed twice
(a regex for orphan tails also matched legitimate merged lines and deleted 7 of them),
so the file was redone from scratch: check out upstream's version, replay the readpico
side as a patch (`git diff 094976e 0b3bcac` + `git apply --3way`), then resolve each of
the 12 hunks with an explicit rule.

**Verification that actually catches this class of error** — compare against upstream as
a baseline:

- every logical macro (joining `\` continuations) has balanced parentheses — mine: none
  unbalanced, upstream: none;
- `#if*` count equals `#endif` count — mine 84/84, upstream 81/81;
- the set of macros mentioning `FREEINK_DEVICE_WS397` is **identical** to upstream's
  (8 macros). This is the check that caught three macros I had wrongly given WS397
  (`FREEINK_CAP_TOUCH`, `FREEINK_CAP_BUZZER`, `FREEINK_FB_PSRAM`) — a silent
  behaviour change for the Waveshare board.

## Post-merge fixes

- `LgfxEpdConfig.h`: upstream's `cleanBankNeedsFreshBackground` is placed **before** the
  two readpico members, because every board brace-initialises this struct positionally
  and upstream's boards expect that member at its own position. The two readpico members
  have defaults and stay last, which is what their comment already documented.
- `ReadPicoLgfxConfig.cpp`: the readpico initialiser then needed a value for the new
  member or the braced `dataPinsHigh[8]` array landed on a `bool`. Set to `false` (the
  struct default, and correct here: the shipping build drives the panel through the
  epdiy LCD backend and this Lgfx config is the fallback).

## Still to verify on hardware

The merge was verified by **compiling**, not by running. Nothing about the readpico
runtime was re-tested, so check on the device: display and refresh quality, touch axes,
the PMU power key (short-press off), SD, and EPUB inline images.

## Do not forget

- `BoardReadPico` and `EpdiyLcd` (with its vendored epdiy sources) are new untracked-by-
  baseline files that exist only because they were committed into the readpico snapshot.
  Dropping the submodule side of a conflict would lose them.
- VCOM is read-only and factory-written. Nothing in this port may write it.
