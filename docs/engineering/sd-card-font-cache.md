# SD-Card Font Cache

The inactive OTA application slot doubles as a disposable cache for one SD-card
reader font. This removes most filesystem and SD-SPI latency from the reader's
initial font load, first-page prewarm, later page prewarms, and on-demand glyph
reads.

The cache holds only the selected reader family and point size. It does not
cache the 8/10/12pt UI fallback fonts, add an LRU, or make an SD font part of
the firmware image. `SdCardFont` still provides the renderer-facing font
interface; only its random-access storage backend changes.

The canonical byte layout is documented in
[file-formats.md](../file-formats.md#sd-card-font-cache).

## Partition reuse model

CrossPoint keeps two interchangeable OTA application slots. At any point one
contains the running firmware; `HalOtaSlot::inactive()` selects the other. The
following diagram is one snapshot of those roles, not a fixed assignment:

```text
ESP32 Flash
+-----------------------------------+
| Bootloader / Partition Table      |
+-----------------------------------+
| NVS                               |
+-----------------------------------+
| OTA metadata                      |  cache never writes here
+===================================+
| OTA App Slot A                    |  running firmware
+===================================+
| OTA App Slot B                    |  inactive -> SD font cache
| +-------------------------------+ |
| | Cache header                  | |  Magic/path/size/hash/CRC
| +-------------------------------+ |
| | Current .cpfont payload       | |  bounded random Flash reads
| +-------------------------------+ |
+===================================+
| SPIFFS                            |  not used by this cache
+-----------------------------------+
| Core dump                         |
+-----------------------------------+
```

The cache uses the partition layout as follows:

1. `HalOtaSlot::inactive()` locates the application slot that is not running.
2. `safeForScratchWrite()` permits erasing it only when the running image is
   confirmed or not rollback-tracked; pending and unsafe images are rejected.
3. Preprocessing invalidates the old cache header, writes and verifies the
   `.cpfont` payload, then commits the `CPSDFC1` header and CRC last.
4. `SdCardFont` keeps the same renderer interface while bounded partition reads
   replace SD filesystem reads. A Flash failure retries the same offset on SD.
5. An OTA download overwrites the cache with the new image. After that image is
   confirmed, the former firmware slot becomes inactive and may hold the next
   font cache.

The roles rotate symmetrically in both OTA directions:

```text
Normal boot             First boot after OTA       After confirmation
Slot A: running    ->    Slot A: rollback image ->  Slot A: font cache
Slot B: font cache ->    Slot B: new / pending  ->  Slot B: running
```

`verifyRollbackLater()` prevents Arduino from confirming a pending image before
`setup()`. On the first boot after OTA, the application initializes storage,
settings, the display, the render task, and the font decompressor, then
physically displays the firmware-verification page. Only after that frame
finishes does it mark the running image valid.

Until confirmation succeeds, `safeForScratchWrite()` rejects every cache
write. A reset, crash, watchdog, or power loss before confirmation therefore
still permits bootloader rollback. A power loss while rebuilding after
confirmation leaves the new firmware valid but the cache header invalid, so
the next boot uses SD. Recovery-firmware mode confirms a successfully displayed
new image but skips automatic font rebuilding.

## Why it is faster

An SD font normally pays for SD-SPI transactions, filesystem traversal, and
many seeks while the prewarm code gathers glyph metadata, bitmaps, kerning, and
advance tables. A valid cache preserves the same offset-sorted read algorithm
but serves it with bounded `esp_partition_read()` calls through `HalOtaSlot`.
The font is not memory-mapped, so it does not consume the ESP32-C3's limited
MMU pages.

Startup still opens the SD source long enough to compare its path, size, and
`.cpfont` header/style TOC identity. The large random reads that dominate page
prewarm then come from internal Flash.

```text
SdCardFont
    |
    v
cache header/path/size/TOC valid?
    | yes                         | no
    v                             v
HalOtaSlot bounded random read    HalStorage / SD
    |
    +-- Flash read failure ------> same-offset SD fallback
```

Every Flash read is bounded by the committed payload size, not merely by the
OTA partition capacity. A partition read error disables Flash for that loaded
font and retries the same offset from SD.

## Cache identity and commit

The first 4KiB erase sector contains a versioned `CPSDFC1` header. It records
the source path, payload size, an FNV-1a identity of the `.cpfont` header and
style TOC, the complete payload CRC-32, and a header CRC-32. The current
0x640000-byte OTA slot leaves 6,549,504 bytes for the `.cpfont` payload.

A preload uses the following commit sequence:

1. Reject an unsafe slot, invalid source, or oversized payload.
2. Allocate one fallible, short-lived 4KiB copy buffer.
3. Erase the header sector first, invalidating any previous cache.
4. Erase and copy the payload while calculating its CRC.
5. Read the payload back from Flash and compare the complete CRC.
6. Write the valid header last.

A partial header write is rejected by its Magic/version/header CRC. The normal
boot path validates the header and SD source identity but deliberately does not
rescan the entire payload CRC; doing so would add a full-font read before the
first page. The complete CRC is guaranteed at commit time, while later
Flash-driver read failures fall back to SD. Silent Flash bit rot after a
successful commit is not detected without rebuilding the cache.

The earlier `CPOTAF1` Magic is intentionally rejected. After a direct USB
flash, that legacy cache falls back to SD until the user confirms the font or
changes its point size. A normal OTA overwrites the cache slot and rebuilds it
after the new firmware is confirmed.

## Rebuild triggers and progress

The hidden `sdFontFlashPreload` setting stores the user's preference:

- Confirming an SD reader family with “Preload to internal Flash?” set to Yes
  copies that family and point size immediately.
- Changing the point size while the preference is enabled replaces the cache.
- A newly installed OTA image automatically rebuilds once, after successful
  firmware confirmation, when the selected SD font exists and the cache is
  invalid.
- An ordinary boot does not retry a failed post-OTA rebuild. Reconfirming the
  font or changing its point size provides the next explicit retry.

Manual preloads and post-OTA rebuilds share the same storage-neutral
preprocessing page. It shows the selected family and physical point size,
per-pass byte count, total percentage, and power-loss warning without naming
OTA, copying, or verification.

Internally, copying still occupies total progress 0-50% and read-back
verification occupies 50-100%. The page uses fast refreshes at the pass
transition and 10% increments. Both entry points physically complete the
verified 100% frame and the Ready page. Manual preloads then return to Text
Settings; post-OTA rebuilds continue to Home without clearing the last-open
book or its reading position.

Automatic sleep and cancellation are disabled during the operation. Failures
are grouped as source too large, OOM, SD read, Flash erase/write, or
verification failure; all continue with the SD font.

## Performance verification

Debug builds expose the storage source and timing needed for an A/B comparison:

- `Initial load source=flash|sd load_ms=...`
- `[epub-page]` or `[txt-page] source=flash|sd total=... read_ms=...`
- `First page displayed: open_total=...`
- Page rendering logs report display time separately from font prewarm.

Compare the same device, SD card, font, point size, and book in SD and Flash
modes. Measure cold initial load and first-page display, then at least 30 text
pages for median and p95 prewarm/read time. Keep e-paper refresh time separate:
the cache accelerates font I/O, not the physical panel waveform.

The implementation adds no resident cache buffer, background task, or LRU. Its
only new working allocation is the existing 4KiB preload buffer, which is
released immediately after the copy completes or fails.
