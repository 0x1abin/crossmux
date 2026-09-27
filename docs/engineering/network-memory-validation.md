# X3 / X4 download memory

## Cause and production behavior

X3/X4 share the ESP32-C3 image and have no PSRAM. Linking BLE consumes SRAM even
when Bluetooth has been off since boot. Wi-Fi and TLS then compete with resident
reading statistics, font caches and the suspended Settings page for the remaining
heap. Total free heap and the largest contiguous block both matter.

`NetworkStartup::setMode()` already stops the BLE host before enabling Wi-Fi.
`BleInput::stop()` delegates teardown to the SDK, including worker/client cleanup
and NimBLE deinitialization. Stopping BLE returns dynamic allocations; it cannot
remove linked static data or guarantee coalescing of all free blocks. Do not add a
second teardown path or use irreversible controller-memory release: the page
turner must remain available after networking.

The production fix has two parts:

- Settings releases its rebuildable lists before launching the font downloader,
  using the existing helper already used by OTA and Bluetooth settings. Child
  allocation failure and normal return rebuild those lists.
- On C3, OTA and font activities call the existing
  `ReadingStatsStore::releaseMemoryForNetwork()` under `RenderLock` before
  allocating Wi-Fi selection. It ends the active session and saves dirty data
  **before** releasing the vectors. Save failure prevents network launch and
  leaves statistics resident. Each activity records successful release once.

The owning activity's existing exit path reloads statistics through reboot,
including cancellation or allocation failure before Wi-Fi starts. Font exits
retain their destination: Home, Reader, Reader with the prompt suppressed, or
Reader with Chinese font preload. Successful OTA installation keeps its normal
restart into the new image. The existing deep-sleep guard suppresses an exit
restart when sleep is already underway; wake loads statistics normally.

S3 and simulator builds do not unload statistics. In particular, touch devices
can stop Wi-Fi without rebooting and must retain their live statistics. This is
not a global Wi-Fi policy: background/live network applications may continue
using reading data. The existing font-cache preparation and BLE teardown remain
shared in `NetworkStartup`; only these two foreground download owners opt into
statistics release.

No new heap buffers, tasks, dependency, persistent setting or SDK API are added.
There is one boolean per download activity (object padding may absorb it).

## Review of the diagnostic iterations

The temporary C experiment mixed behavior into `NetMem::start/finish`: it freed
statistics and forced an unconditional Home restart. It established useful
memory evidence, but did not fix regular builds and intercepted the automatic
font flow's reader return route. The submitted implementation puts lifetime
ownership back in each activity and removes all temporary hooks, TLS wrappers,
heap dumps and A/B/C build profiles from the production tree. No Bluetooth
startup threshold or retry interval was changed during this investigation.

The diagnostic binaries, source snapshots and raw logs remain local under
`.pio/netmem-artifacts/`; they are not release artifacts. Flash backups contain
private device configuration and must not be uploaded. The final production
binary differs from those diagnostic binaries and needs its own hardware check.

## Physical evidence (September 19–20, 2026)

These are exploratory runs with user-confirmed visible results, not a completed
matched-workload endurance matrix. Byte counts are stage snapshots, not exact
allocation-time peaks. X3 and X4 used different stored statistics and workloads;
their headroom difference cannot be attributed solely to hardware model.

| Run | Before TLS: free / largest (bytes) | Result |
| --- | ---: | --- |
| X3 A0, BLE linked but off since boot | 29,600 / 23,540 | OTA TLS `MEMORY_E` (-125), failed 4,120/4,113-byte allocations; no HTTP body |
| X3 B, BLE omitted from the build | 39,376 / 32,756 | OTA HTTP 200, 335 bytes; no observed allocation failures |
| X3 C, BLE retained and statistics released | 38,216 / 31,732 | OTA HTTP 200, 335 bytes; no observed allocation failures |
| X3 font candidate, statistics and Settings lists released | 37,332 / 18,420 | Six MiSans files, 10,441,776 bytes; download/use confirmed, no observed allocation failures |
| X3 after BLE page turns | 37,568 / 23,540 | OTA passed; exit reboot followed by reader BLE connection/page turns |
| X4 after BLE page turns | 54,260 / 42,996 | OTA HTTP 200, 335 bytes; no observed allocation failures |

Removing BLE in A/B reduced IRAM text by 1,292 bytes, DRAM data by 2,960 and BSS
by 5,136: **9,388 bytes** of static sections. Reported total heap increased by
9,632 bytes; this is a separate runtime measure. A/B startup histories were not
identical, so this supports BLE as a contributor, not the sole cause.

Observed BLE stop returned about **42.8 KiB** of dynamic heap (43,836 bytes on
one X3 run and 43,828 on X4). Statistics release returned about **8.7 KiB** on X3
and **15.3 KiB** on X4, depending on saved data. X4 also downloaded and used all
six MiSans files and connected the reader page turner after the normal reboot.
The final capture also confirms BLE connected again after the X4 OTA-exit
reboot (boot `cc007e9a`, connected at 19,697 ms).

Latest shared diagnostic candidate SHA256:
`66209d0bef31109b147dddace4bfe9dde825182ce2ad7d26f374a6177b9e8adf`.
Earlier MiSans X3 candidate SHA256:
`7cd14b00adbd5e56b7f89c8290649238f35877126e28f55f77373f89a60941b1`.

### Limits still relevant to acceptance

- A later X3 NotoSansSC download after BLE use completed all six files
  (11,063,884 bytes), but recorded two failed allocations. One coincided with
  TLS `PEER_KEY_ERROR` (-342), recovered by the existing TLS 1.2 retry. The other
  occurred during a successful transfer; its call site was not identified.
  Functionality passed, but this was not a zero-allocation-failure run.
- A hot X3 reader previously failed the unchanged BLE startup gates at
  81,992 free / 32,756 largest bytes. A subsequent cold boot connected and turned
  pages. That does not prove the hot-run fragmentation issue is fixed; keep the
  [BLE acceptance limits](c3-bluetooth.md#validation-and-known-limits) open.
- CSS low-memory fallback was observed on X3 and on X4 after the OTA-exit
  reboot. No parser or BLE threshold was
  relaxed to hide it. No repeated baseline failure was captured on X4.

## Verification

Run `./bin/ci-check`; `DownloadMemoryLifecycle` compiles the actual OTA/font
entry and exit methods against host doubles for C3 and S3. It covers failed
statistics save, failed child allocation, cancellation before Wi-Fi, repeated
entry, all font restart routes and unchanged non-C3 behavior. For a focused run:

```sh
python3 scripts/tests/test_download_memory_lifecycle.py
pio run -e gh_release
```

On **each** X3 and X4 with the final production image:

1. Record firmware SHA256, device model, AP, channel, EPUB, font/size and reading
   statistics. Use the same workload between repetitions. Keep OTA checking
   separate from actual installation, which replaces the tested image.
2. Connect the page turner, turn pages, then check OTA. Confirm the update page.
   Exit, reopen the book and confirm BLE connection and real page turns.
3. Download an actually missing font family, select it and read with it after
   exit. Repeat after prior BLE use; already-verified files are not a download
   test. Check that reading statistics survived the reboot.
4. Cancel Wi-Fi selection and exercise automatic Chinese-font setup from the
   reader. Verify the intended return destination and prompt suppression. Open
   and dismiss the initial prompt/OTA ready page without starting networking:
   this must not unload statistics or cause a new reboot.
5. Verify sleep while on a network page stays asleep, then check statistics on
   wake. With an SD write failure and dirty statistics, network launch must be
   refused and in-memory data retained.

Use `scripts/debugging_monitor.py` for serial logs. Existing RST/BLE diagnostic
logs and OTA TLS preflight identify release and handshake headroom in a logging
build; their absence in release logs is not proof of zero allocation failures.
For stability acceptance, repeat each network workflow ten times per device,
including warm BLE use, and investigate any TLS retry, unexpected reboot or
missing statistics. Build and host-test success do not replace these checks.
