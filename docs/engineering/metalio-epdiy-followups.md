# Metalio Plus EPDiy follow-ups

Status recorded on 2026-11-23. These are the independent improvements tracked in
[CrossMux #468](https://github.com/0x1abin/crossmux/issues/468), following the
EPDiy implementation that superseded the old vendor port in #460. No device was
flashed or physically accepted during this migration.

## Sources and review boundaries

The application candidate starts at `27d54817cd7c0ba4f6b87d034477727e82235f40`
and retains the committed SDK `c77ef90da4ffaa5e5ce35a238e572cc023260d5e`.
The independent SDK patches start at
`240e699b09e9a1358de2292e44b88596b19ae3ef`; the affected RTC, IMU and board
configuration sources are identical at those two SDK revisions. EPDiy remains
pinned to `edc78157fdf20d0ccff405b6e5164d0663acd803`.

Keep the EPDiy display, shared Wire/I2cBusLock owner, power policy, partitions
and USB/BLE implementation. The old vendor BSP and native I2C owner are not part
of these contributions. Historical vendor-firmware acceptance does not establish
acceptance of this EPDiy candidate. The old SDK #51 remains separate.

| Contribution | Review | Scope |
| --- | --- | --- |
| Automatic EPUB rotation | [CrossMux #475](https://github.com/0x1abin/crossmux/pull/475) | Default off, foreground EPUB only; no XTC or new flick gesture |
| SC7 mount correction | [SDK #55](https://github.com/0x1abin/freeink-sdk/pull/55) | Apply existing mounting transform to SC7; enable Plus profile |
| High-density vector sizes | [CrossMux #473](https://github.com/0x1abin/crossmux/pull/473) | Every integer 8–32 pt; ordinary profile remains 8–22 pt |
| PCF8563 correctness | [SDK #56](https://github.com/0x1abin/freeink-sdk/pull/56) | Validate clock state/calendar and make writes failure-safe |
| Render-safe diagnostics | [CrossMux #474](https://github.com/0x1abin/crossmux/pull/474) | Defer periodic heap/BLE sampling while rendering |
| Host compiler compatibility | [CrossMux #472](https://github.com/0x1abin/crossmux/pull/472) | Six inherited macOS test compilation failures; no firmware change |

The application rotation contribution depends on SDK #55. Its formal SDK pin is
deliberately unchanged: calibrated rotation must remain Draft until the SDK
change is integrated, the application dependency is advanced through the normal
review process, and hardware checks pass. An arbitrary local SDK checkout is not
evidence that the default CI checkout contains that dependency.

## Behavior and resource limits

Rotation shares the existing HAL IMU owner and sampling. Its allocation-free
classifier requires 600 ms of stable gravity, applies a 1200 ms cooldown and
invalidates dwell after a sample gap over 240 ms. Rendering, child activities,
popups, physical/screen/Home-key input and remote clicks block rotation and clear
the candidate. EPUB reuses its existing locked content-offset/reflow transaction.
Runtime sensor visibility does not determine persistence eligibility. Plus does
not expose the existing tilt-page-turn control as a new unvalidated capability.

The font change adds ten entries to the high-density table and existing
selection list. It retains the current FreeType engine and cache caps. Bitmap
sizes and the 12 pt built-in fallback are unchanged; larger glyphs can reach
those fixed caps sooner. No rendering speed or memory improvement is claimed.

PCF8563 reads control and calendar together, rejects STOP/TEST/VL and invalid
BCD/Gregorian dates, and preserves the caller output on failure. Explicit valid
sets use STOP → complete calendar → RUN under the existing recursive bus lock.
A failed calendar write leaves STOP asserted. TESTC/VL can recover through a
trusted set; TEST1 remains refused. The existing century-bit interpretation and
other RTC backends remain. The snapshot adds bounded stack bytes, not a heap
allocation. Application trusted-time/SNTP behavior already exists on main.

Diagnostics use the existing Try RenderLock. A busy render defers sampling
without advancing the timer; the ten-second interval starts after a completed
sample. The original Serial/ROM eligibility and heap/PSRAM/BLE fields remain.
There is no new task, buffer or mutex. Current Plus and Read Pico display drivers
expose synchronous completion while the reader holds this lock. This is a
scheduling change, not a reproduced EPDiy underrun or a guarantee against every
stream fault.

## Local verification

Application build/test results describe the combined migration candidate; each
PR remains independently based on main. SDK harness results are listed separately.

| Check | Result |
| --- | --- |
| Complete host CTest, combined candidate | 1585/1585 passed on macOS |
| Six inherited compiler failures | Reproduced with identical errors on pristine main; all six pass with only the compatibility patch |
| Shared host-fixture consumers | 53 tests passed; one existing optional SVG regeneration skip |
| Rotation/font/diagnostic/settings/time focused CTest | 10/10 passed |
| PCF8563 production driver and bus-lock harness | 49/49 ASan/UBSan cases; previous driver failed 40 |
| Existing RTC, PCF85063, PMIC and application time regressions | Passed |
| IMU driver/profile regressions | Original suites plus five real-profile/capability builds passed, including ASan/UBSan |
| BoardConfig consumers | 19 suites passed; inherited display warnings remain |
| clang-format | Version 21.1.8, application check passed |
| cppcheck | Version 2.20, default target passed |
| Plus firmware with formal SDK pin | Passed; 122,076 B static RAM, 6,434,442 B linked Flash |
| Shared C3 firmware with formal SDK pin | Passed; 68,368 B static RAM, 6,409,373 B linked Flash |
| Offline image validation | Plus/C3/Read Pico esptool image-info passed |
| Read Pico firmware with formal SDK pin | Passed; 81,832 B static RAM, 6,542,679 B linked Flash |

Read Pico firmware.bin is 6,547,936 B in a 6,553,600 B application slot: only
5,664 B remain. Preserve its partition policy and measure future Flash growth.
Build sizes describe static/link usage, not runtime headroom. Other hardware
environments and the entire `bin/ci-check` matrix have not been run. The bundled
cppcheck 2.11 reported the unchanged typed `unique_ptr` arithmetic in
`FootnoteEntry.h`; the recorded clean check uses the already available 2.20 tool.
No suppression or production footnote change was added for that diagnostic.

The RTC harness compiles the real driver and recursive bus guard with the
existing RTC BoardConfig stub. IMU mounting tests use actual board profiles.
Reader tests execute the production admission/reflow methods with input/storage
seams; they do not render a complete book. Font tests run actual selection
callbacks and normalization; complete settings-file/TTF-loader integration and
physical text layout remain separate acceptance items.

The six macOS failures involved a GCC-only warning flag, unused fixture
declarations, a class/struct mismatch and unused SDK keyboard tables. The
compatibility patch retains all C++ assertions and fixture `-Werror`; its narrow
Clang exception applies only inside the actual SDK translation unit. The two
additional environment failures were resolved using existing OpenSSL paths and
isolated pkgconf 2.3.0. Toolchains and dependencies stay in the independent
development directory, with no system/Brew environment changes.

GitHub Actions and local checks are separate. The latest old #460 runs expired
while waiting for maintainer approval, before any job executed. New fork PRs can
also require approval; a pending approval is not a failed compilation. Do not
weaken workflow guards or claim remote CI passed from local test results.

## Remaining acceptance

- On EPDiy hardware, verify four-way gravity/display/touch, position preservation,
  flat/diagonal rejection, popup/child/held-key/Home/BLE suppression, feature-off
  standby, sleep/wake and current. Integrate the SDK mounting dependency first.
- Verify 26/32 pt selection, actual preference reload after restart, bitmap/TTF
  switching, wrapping/missing glyphs and fixed-cache pressure on Plus/Read Pico.
- After trusted UTC synchronization, remove all device power and check offline
  RTC recovery and actual clock advancement. Repeat ordinary sleep/wake with
  touch/gauge/display traffic; backup retention, drift and alarm wake are not
  proved by host models.
- Read across multiple diagnostic intervals on EPDiy hardware and record internal
  free/largest/minimum heap, PSRAM and stream errors. Keep this separate from the
  old vendor-backend acceptance record.
- Obtain maintainer approval for the new code workflows and inspect their jobs
  after execution. Title checks passed; code workflows currently report
  action_required with no jobs. Physical feedback remains a separate requirement.
