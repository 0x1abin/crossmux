# Metalio Plus V1.4 frontlight increment

Continue the authorized BSP port inline; USB intermittent replug debugging is
explicitly deferred by the user. Preserve the retained reader, original recovery
association and SD data. No commits, releases or automatic restoration.

The pinned vendor `periph/frontlight.cc:15-21,227-259,288-299` and `.h:41`
establish cool GPIO6, warm GPIO7, active-high PWM, 20kHz/10-bit and a 10% PWM
ceiling per channel. Existing CrossMux brightness/warmth, persistence and sleep
UX should drive these channels through HAL and the existing SDK manager.
Retain CrossMux's constant-total warm/cool mix, within the vendor ceiling.

Extend FrontlightConfig with a trailing PWM output ceiling (default 1000
permille so existing profiles retain their outputs). The Plus sets 100.
Scale total duty before warm/cool splitting, including startup kicks, retaining hardware-full
polarity for active-low boards. Zero brightness is fully off; small nonzero
levels should remain nonzero when the configured ceiling allows one duty step.
Do not add an I2C bus, custom heap allocation, timer/task or vendor driver copy.
Existing Arduino LEDC allocations become active and must be accounted for in
runtime heap records.

Keep the existing Plus checked GPIO6/7 low/hold sleep path and release holds
before PWM attach on wake. Physically validate brightness, cold/neutral/warm,
off, stored settings and one sleep/wake with light recovery. Current/power
measurement remains pending. Battery gauge is a separate next increment.
