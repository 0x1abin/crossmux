# Metalio Plus read-only battery telemetry

Continue the existing authorized BSP port inline, retaining reader/SD state and
original recovery association; restore only on explicit request. USB replug
investigation is deferred. No commits, dependencies or release changes.

Evidence: SDK BatteryMonitor.cpp:61-75 currently starts Wire; the native IO
adapter MetalioPlusIo.cpp:17-22 already locks vendor I2C1. BoardConfig.h:1800
currently disables the gauge. The pinned vendor BQ27220 source uses address
0x55, Voltage 0x08 and signed Current 0x0C. TI SLUSCB7A section 7.3.1 documents
incremental reads, and section 7.3.1.3 limits standard command polling. TI
SLUUBD4A documents SOC 0x2C. The charger is CX25601N, not BQ25896.

Enable Plus BQ27220 with native I2C1 SDA0/SCL1, charger address zero and capacity
zero. Route its 16-bit reads to a board adapter that exposes only voltage,
current and SOC. A one-second cached snapshot uses an incremental 6-byte read
at 0x08 (voltage, flags, current) and a 2-byte SOC read at 0x2C. Thus at most
two standard commands per second, including failures. No blocking 500ms sleep,
new task, bus recreation, calibration/unseal/charger write or explicit heap
allocation. Native IDF device registration may allocate its existing helper's
one cached handle on first use; no separate driver ownership is introduced.

All cache/transfer state is under the native mutex (50ms acquisition/transfer
bounds); 66us bus-free delay separates the two gauge packets. Cache expires at
one second; failed fields become unknown rather than retaining stale values.
Lock/transfer failures do not modify caller outputs. Decode little endian
bytewise. Reject SOC >100 and voltage outside the vendor's 2500..5000mV sanity
range. Current 0xFFFF is valid -1mA. Charging is current >0, never proof of USB
presence. Disable Wire initialization in this Plus compilation, so unsupported
legacy helpers fail closed. Existing other-board behavior remains unchanged.

Validate the production adapter + BatteryMonitor with a model native bus,
including cache/time wrap, per-field failure, lock pairing and no Wire or
charger/config writes; run existing gauge/profile/power tests and S31 build.
Archive unique artifacts before full CTest. Update-and-keep through the first
active recovery manifest, inspect live battery values and request physical
percentage/frontlight sleep/persistence acceptance. SOC accuracy and actual
current consumption remain unmeasured.

Review exposed a newly enabled charge-current fallback into USB cold-boot sleep.
Plus observes native USB SOF for host presence and stays awake for AfterUSBPower
before display initialization, just like the existing native-USB board group.
This is a battery integration regression fix, not the deferred replug work.
