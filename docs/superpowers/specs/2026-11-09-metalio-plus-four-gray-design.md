# Metalio Plus four-level grayscale and text AA

## Intent and chosen scope

The user chose staged delivery and delegated implementation details: first
four-level grayscale and text antialiasing, then evaluate native sixteen-level
images/four-bit glyphs after physical acceptance. Preserve the previously accepted
reduced-flash reading behavior and configured Full cleanup interval. Continue in
the existing `yios` development checkout; no implicit commit/push or restoration.

## Evidence and approach

- `MetalioPlusEpd.cpp:49-88` currently expands only B/W into the vendor's native
  415,872-byte PSRAM front buffer, then performs checked DU/GL16/GC16 updates.
- `MetalioPlusDriver.h:9-25` implements no grayscale capability or staging hooks.
- `BoardConfig.h:1751-1788` advertises two levels for Plus.
- `EpubReaderActivity.cpp:3048-3074,3149-3168,3210-3350` already renders gray masks
  and supports a combined base with tiled uploads. Its image condition currently
  excludes combined strip drivers and must admit an explicitly Combined capability.
- The frozen vendor `highlevel.c:81-169` owns the completed native back buffer;
  `my_waveform.c:49-75` maps DU targets to binary endpoints. Gray commits must use
  GL16, retaining Full GC16 for cleanup/recovery. Do not copy SSD1677 calibration.

Chosen: compose B/W plus both masks in the existing native front buffer, upload
strips through the existing HAL, and activate the finished target once. The
whole-plane compatibility entrypoints use the same encoder. Alternative native
sixteen-level integration also changes font/decode and refresh bookkeeping; defer
it as agreed. A separate B/W activation followed by a gray activation conflicts
with the user's reduced-flash requirement and is excluded.

## Contracts

- Overlay only: masks `(LSB,MSB)` are black/white `00` (base), dark `11`, light
  `01`; map to native levels 0/5/10/15. Native packing: even pixel low nibble.
- Capability: OverlayMasks, Combined, strip uploads; no asynchronous refresh or
  staging while BUSY. Advertise four levels, not sixteen.
- Begin stages the B/W target without power/update. Complete contiguous LSB rows
  precede complete contiguous MSB rows; missing, reordered, out-of-range or invalid
  masks cannot be submitted. Each new begin discards older staged data.
- Ordinary gray commits use GL16, Full/unknown history uses existing clear+GC16.
  The cadence stays owned by the reader. Cleanup after success must not replace
  the native gray baseline with a B/W proxy. The first B/W draw after a gray frame
  uses GL16 instead of DU; later B/W pages retain DU.
- A cancelled unfinished pass performs no activation. Cleanup with an intact
  B/W proxy may complete an unsubmitted pass as B/W (OOM fallback). Failed hardware
  submission is not immediately retried by cleanup; it keeps unknown history and
  the existing power-fault inhibition. Sleep/resync cancels staging.
- Reuse native buffers: no added full-screen planes/driver heap allocations. The
  existing synchronous reader strip path uses 152×80 = 12,160 bytes scratch;
  whole-plane callers retain their existing chunked B/W snapshot/failure behavior.
- Driver-specific code stays in SDK. Keep application/HAL API signatures,
  geometry, vendor blobs/waveforms, rail/temperature values, C3/S3 policies,
  settings and file formats unchanged except the narrow Combined image routing.
- Restore-only-on-request, existing Flash recovery association and backed-up SD
  data remain in force. Use the temporary tested SDK checkout; retain the main
  repository's baseline gitlink until SDK integration is authorized/completed.

## Acceptance

Host: real driver/backend with recording vendor API; no activation during staging,
all four pixel values, whole/strip parity, boundaries, invalid/cancel/failed passes,
gray→gray/BW/menu/sleep transitions and Full cadence. Verify shared legacy policies,
frozen blobs, formatting and full host suite with known baseline failures listed.
Build the native S31 candidate in a fresh relocated build directory, check unique
BIN/ELF/map, native config, startup order and original recovery guards before app-only
deployment. Review the finished changes independently before flashing.

Physical: four distinguishable tones in an image; CJK/Latin edges without missing
or faded strokes; at least 20 turns crossing periodic Full; explicit refresh,
menu/Home/reopen, AA disabled, image→text and sleep/wake. Log update mode/completion
and internal/PSRAM headroom separately. Optical acceptance remains pending until
the user actually observes it; do not increase cleaning frequency to mask defects.
