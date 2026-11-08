# Metalio Plus native input and SD increment

Continue the user's requested V1.4 port in the existing isolated checkout. Repeated
instructions to continue select inline implementation of the previously stated
native I2C/input/SD step; no commit, release or hardware run is part of this increment.

## Approach

Reuse the vendor I2C1 owner for bounded synchronous FT6336U register transfers and
TCA input reads. Expose a small Plus I/O facade beside the frozen BSP; InputManager
must not include vendor internals or start Wire. A separate Plus FT6336U branch
uses existing touch event/release and button debounce machinery, with an explicit
raw screen/strip classifier. Touch reset runs once after vendor board/display
initialization; never reset on a polling failure. Do not initialize framebuffers
merely to poll buttons. Defer input using HalGPIO's existing post-display seam.

The vendor's coordinate heuristic cannot establish this unit's transform. Use an
explicit provisional portrait sensor mapping into the native landscape panel:
raw (x,y) -> (y,683-x). Keep runtime blocked until measured at edges/center.
Strip Y=1600 +/-30, X=100/200/300 +/-30 maps home/up/down; a contact cannot cross
between screen and strip during one gesture. Multi-contact and failed frames must
not cause phantom taps. Home uses existing home-key semantics. Expander keys are
wired at indices 8/9/15; active-low is provisional and requires physical samples.
BOOT61 must not be treated as a power-off gesture without physical acceptance.

Reuse the SDK's existing eight-sector (4KiB) bounce buffer for SD. On S31 select
internal DMA capabilities, query cache alignment, allocate aligned storage and
supply it to IDF's host along with an internal-only alignment callback. Preserve
all legacy allocations and host defaults on C3/S3. Enable the Plus SDMMC profile
(24/25/20-23, 4-bit, slot0 selected by native IDF) and block-device build define.
Mount attempts do not format a card. Host initialization and actual sector0 reads
are required before publishing a card. No filesystem/SD writes are executed here.

Alternatives considered: enabling generic Wire creates a competing owner; copying
vendor touch tasks duplicates existing InputManager state/gesture machinery; a new
SD filesystem bypasses the established FsFile interface. All add avoidable state.
These are implementer choices, constrained by the previous peripheral contract.

## Evidence

- InputManager.cpp:3688-3789 generic FT path initializes and transacts on Wire.
- BoardConfig.h:1729-1741 Plus profile deliberately has no touch/SD yet.
- HalGPIO.cpp:254-270 already provides deferred post-display input initialization.
- Vendor metalio_i2c1.h:14-31 exposes the locked synchronous I2C1 API.
- SdmmcBlockDevice.cpp:134-143 owns a bounded DMA buffer but does not query alignment.
- Native IDF sdmmc_cmd.c:489,652 invokes the host alignment callback.
- Vendor reference SdCardManager.cc:42-61 supplies aligned internal DMA storage.

## Acceptance and limits

Compile real facade, InputManager and SdmmcBlockDevice with recording native APIs;
check lock pairing on errors, reset once, short/held/release/failure, strip isolation,
coordinate boundaries, multi-contact rejection, SD allocation/host/slot/read cleanup
and transfer chunking. Compile both S31 and a legacy S3 SD path. Run existing affected
SDK tests and reader host tests, full guarded 200MHz build and formatting.

Arduino NVS/startup restoration and physical touch/keys/SD/sleep acceptance remain
pending. Upload guard stays enabled regardless of build success. Existing ThingsOS
and all frozen vendor files are preserved. New allocations: none for input; SD
reuses the existing 4KiB allocation with stronger S31 alignment/capabilities.

Final review clarification: native button sampling now enters the Plus physical
backend with explicit success/failure, so a failed port read cancels a held key
without generating a normal release action. A status-free ButtonHook could not
preserve that distinction. Other boards retain their existing hooks and paths.
