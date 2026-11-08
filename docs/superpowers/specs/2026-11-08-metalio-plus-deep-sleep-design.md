# Metalio Plus V1.4 power-key deep sleep

This continues the user-authorized BSP port and persistent debugging. Preserve
CrossMux's existing deep-sleep/reboot/resume contract. User scope: continue
implementation and device debugging; keep firmware and SD data until explicitly
asked to restore. Reuse the current isolated checkout and inline execution.

## Evidence and route

`HalPowerManager.cpp:315-322` selects generic power-pin deep sleep. The Plus
profile has no direct power GPIO (`BoardConfig.h:1744`), so SDK
`PowerManager.cpp:133-135` cannot arm that wake. The real active-low power key
is TCA P17, and its shared IRQ reaches RTC-capable S31 GPIO2. Pinned vendor
`power_hw.cc:111-123` checks the pad and arms EXT1 ANY_LOW. Board vendor APIs
already provide TPS sleep, bus park/latch, TCA mask/power-input and rail cut.
The original SD mount releases held pins before controller setup; SD is not on
the P5/P14 camera/display and modem/audio switched rails. Do not copy the
vendor's application/light-sleep policy or its wake-only MPLL workarounds.

Selected approach: native checked EXT1 GPIO2 plus existing SDK deep sleep.
A light-sleep policy would require a different retained-task/SD lifecycle than
CrossMux currently uses. A shutdown pulse is hardware power-off and does not
implement the existing sleep-wake/resume contract. Both are deferred.

## Board/HAL contract

Add `freeink::metalioPlusPrepareDeepSleep()` to a separate power adapter in
MetalioPlusEpd. Require the display/native I2C owner to be initialized and SD
already flushed/unmounted. Bound release wait to 5s; reject I/O failure, invalid
wake pad or a stuck-low shared IRQ. Stop PA/motor, drive frontlight and SD pins
low/hold, put TPS in SLEEP, park display bus, mask unrelated TCA inputs, restore
P17 input, require IRQ idle-high, arm EXT1 GPIO2 ANY_LOW, then cut P5/P14 and
latch the bus. Recheck IRQ after rail transitions. No framebuffer, task or heap
allocation. No vendor-file or charger/eFuse writes.

Failure: disarm EXT1; restore attempted rail/key mutations where possible and
return false. HAL logs and restarts because display/filesystems are already
closed; it must never fall through to wake-less generic deep sleep. Parked
pads are released by the existing boot/display and SD initialization paths.
Success: HAL calls SDK deepSleep directly, preserving the explicit GPIO2 wake.
No change to other target paths or shared reader activities.

## Persistent debug updates

Add an explicit update-and-keep mode using the active original recovery manifest.
Validate the current artifact before writing, preserve original mutable snapshots
and the active manifest SHA/UUID, append a durable per-update journal, and retain
reader/NVS/SD on completion or failure. Do not prepare over the active record or
restore automatically to permit another build. Reject wrong device, changed
restore association, modified immutable Flash or a mismatched current image.

## Acceptance

Compile real production power code with a recording native API: ordering, held
power release, invalid pad, bad I2C, stuck IRQ, failures at every fallible step,
no success without armed wake, cleanup after partial rail/key changes. Test
persistent update retention and explicit eventual original restoration.
Build real S31 image and verify HAL linked call/wake APIs, image/partition/hash
and protected ranges. Ask the user for three actual sleep/wake cycles, checking
reader/input/storage/progress each time. Automatic sleep remains Never during
bring-up. Measured standby current remains pending without measurement hardware.
