# Plus differential refresh correction

User explicitly prioritizes comfortable refresh over RTC: every update currently
flashes a full clear. Continue the authorized retained-device workflow inline;
no new dependencies, commits, restores or USB replug investigation.

Root cause: MetalioPlusDriver.cpp:16-24 discards mode and MetalioPlusEpd.cpp:55-74
always runs checked clear plus from-white GC16. Frozen highlevel.c:205-295 has
checked differential update preserving back_fb only on success; my_waveform.c:
22-24,122-132 supplies DU(20 phases), GC16/GL16(48). No waveform/table/timing or
VCOM changes are required. Existing 25C choice is retained, not recalibrated.

Route Full/first-after-init/invalidated/failed frames through the accepted clear
and from-white GC16 path. With a known baseline route Fast to DU and Half to
GL16 via epd_hl_update_screen. Preserve the vendor PSRAM back framebuffer across
ordinary checked power-off. Sleep/resync and any failed power/draw/off operation
invalidate the baseline; off failure remains latched until reboot. Only commit
new baseline/display-success after waveform and checked power-off complete.
Do not add a custom LUT, frame buffer, heap allocation, timer/task, asynchronous
path or native grayscale capability. Window/split fallback remains synchronous
full-frame composition with differential changed-pixel driving; no rectangle
performance claim. Existing temperature and power-down policy remain unchanged.

ReaderRefresh.h:11-25 currently emits HALF on periodic cleanup except ReadPico.
Add Plus to the explicit FULL cleanup policy, preserving configured cadence,
deferred cleanup debt and every other target. Ordinary pages remain FAST. No
new setting or cadence default change. Grayscale remains unsupported, so gray
image optical quality and native text AA are not claimed.

Tests compile actual Plus driver/backend against recording external hardware
calls, check MSB/nibble geometry, baseline lineage, clear counts and concrete
modes, failed waveform/power/off, re-sync/sleep and synchronous window/split.
Compile the actual reader helper for Plus and legacy targets and assert cadence.
Run relevant regressions and repository checks, build S31 once, archive unique
BIN/ELF/map before full CTest, review focused diff, then update-and-keep the
original manifest. Log requested/actual mode, first/recovery path and duration.
Physical acceptance requires ordinary text pages, configured cleanup cadence,
explicit full, popup/Home transitions and sleep/wake; judge flash, ghosting and
contrast with user feedback. Actual elapsed timing comes from logs, not phase
counts. User may ask for further tuning after the first native-waveform trial.

Review found a second caller path: no-gray image pages previously bypassed the
shared reader cadence because Plus does not advertise continuous gray-image
reading. Extract the existing image-refresh decision into a real tested reader
helper; Plus binary images use the same Full/Fast cadence, other panels retain
the existing capability/debt behavior. This enables no new gray capability.
