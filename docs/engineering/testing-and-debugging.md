# Testing, Debugging & Verification

> Deep reference for [CLAUDE.md](../../CLAUDE.md). Build/quality commands, the
> crash playbook, the agent vs human verification split, CI awareness, and live
> serial debugging. For the contributor-facing quick version see
> [../contributing/testing-debugging.md](../contributing/testing-debugging.md);
> for webserver issues see [../troubleshooting.md](../troubleshooting.md).

## Build Commands

**Via CLI**:
```bash
# Build firmware (default environment)
pio run

# Build and upload to device
pio run -t upload

# Build specific environment
pio run -e gh_release

# Build and run a native device simulator
pio run -e simulator -t run_simulator
pio run -e simulator_x3 -t run_simulator
pio run -e simulator_eego_a4 -t run_simulator
pio run -e simulator_murphy_m4 -t run_simulator

# Clean build artifacts
pio run -t clean

# Upload filesystem data (if using SPIFFS/LittleFS)
pio run -t uploadfs
```

**Via VS Code**:
* Use PlatformIO toolbar: Build (✓), Upload (→), Clean (🗑️)
* Or Command Palette: `PlatformIO: Build`, `PlatformIO: Upload`, etc.

## Monitoring and Debugging

```bash
# Enhanced monitor with color/logging (recommended)
python3 scripts/debugging_monitor.py

# Standard PlatformIO monitor
pio device monitor

# Combined upload + monitor
pio run -t upload && pio device monitor
```

**Via VS Code**: Click Monitor (🔌) button in PlatformIO toolbar

## Code Quality

```bash
# Complete local CI suite: format, static analysis, firmware builds, host tests
./bin/ci-check

# Check formatting without modifying files
./bin/clang-format-fix --check

# Apply formatting
./bin/clang-format-fix
```

## Debugging Crashes

**Common Crash Causes**:

1. **Out of Memory** (Most common):
   ```cpp
   LOG_DBG("MEM", "Free heap: %d bytes", ESP.getFreeHeap());
   ```
   - Monitor heap usage throughout activity lifecycle
   - Check if large allocations (>10KB) occur before crash
   - Verify buffers are freed in `onExit()`

2. **Stack Overflow**:
   ```cpp
   LOG_DBG("TASK", "Stack high water: %d", uxTaskGetStackHighWaterMark(taskHandle));
   ```
   - Occurs during deep recursion or large local variables
   - Increase task stack size in `xTaskCreate()` (2048 → 4096)
   - Reduce the frame or reuse an existing bounded scratch buffer; use a
     checked nothrow heap allocation only when no scoped scratch is available

3. **Use-After-Free**:
   - Activity deleted but task still running
   - Always `vTaskDelete()` in `onExit()` BEFORE activity destruction
   - Set pointers to `nullptr` after `free()`

4. **Corrupt Cache Files**:
   - Delete `.crosspoint/` directory on SD card
   - Forces clean re-parse of all EPUBs
   - Check file format versions in [../file-formats.md](../file-formats.md)

5. **Watchdog Timeout**:
   - Loop/task blocked for >5 seconds
   - Add `vTaskDelay(1)` in tight loops
   - Check for blocking I/O operations

### EPUB memory fallback on devices without PSRAM

Styled chapter parsing stops below 48 KiB free heap or 16 KiB largest block.
Allocation failures also stop parsing, including table and trailing-page layout.
The reader discards the failed build and retries once with embedded CSS disabled
for that reading session, retaining the selected font and content offset. XML
and I/O errors do not trigger this retry. A failed basic build shows the existing
index-failed popup. No failed build may become a complete or partial cache.

`EpubReaderActivity::handleBuildFailure()` owns the retry decision, reader
resource cleanup and failure latch for foreground and background builds. Callers
pass `Section::BuildError` explicitly, including `OutOfMemory` when the Section
object itself could not be allocated. The
error popup only draws. `Section::releaseBuildResources()` closes and removes
uncommitted resources without deciding whether an existing cache is valid:
initialization failures preserve it, while `abandonBuild()` invalidates it after
a parse failure. Cleanup leaves `BuildError` available to the reader.

Before starting or continuing a chapter, low-heap no-PSRAM builds reclaim
rebuildable font caches. Optional font prewarm must leave 16 KiB free heap and
8 KiB contiguous headroom; a skipped cache uses on-demand reads. Basic layout
keeps the 320-token soft-flush threshold instead of reverting to 750 tokens.

Build and run the `ChapterHtmlSlimParserTest`, `SdCardFontMemoryTest`, `PageLinkTest`,
`footnote_list_test`, and `CssParserTest` host targets for fault injection,
text/offset preservation, and cache capability checks. These include each mini-cache
replacement allocation failing, initialization failures preserving an existing
complete/partial cache, and a pre-refactor mixed-text cache/LUT digest. The digest
uses the host page-serialization stub; it does not establish pixel equivalence.
`ReaderBuildFailureTest` compiles the actual failure-handling method with
lightweight collaborators for no-PSRAM and PSRAM policies. It covers allocation
failure before a Section exists, one CSS retry, target preservation and repeated
failures; it is not a full Activity or device-lifecycle integration test.
Build firmware with
`pio run -e default -e simulator_x3 -e murphy_m4`.

Hardware acceptance still requires **both X3 and X4**: open the reported EPUB
with a cold section cache using LXGW WenKai size 18, turn across chapters, and
reopen it. Check text against the source for missing or reordered words/lines.
Capture `SCT` cache-reclaim, `SDCF` prewarm-budget, `EHP` failure-stage and
`ERS` fallback/ready/error logs, including
`free/min/maxAlloc`. Under additional pressure, basic styling or an index error
is acceptable; a restart or silently incomplete text is not. Exit and reopen to
confirm the next session attempts the user's embedded-style setting again.
Compilation and simulator checks do not establish hardware acceptance.

### September 6, 2026: reader memory hardening checkpoint

This checkpoint is **not full hardware acceptance**. The preceding X4 candidate
application had SHA256
`7b030e735aa636b96360ce5c7b34d0152526f00edbde601496a9a8800c4b23fa`.
Application readback matched that candidate and the then-current build byte for
byte; the indexing failure was not an older firmware image.

With the reported Jobs biography, LXGW WenKai size 18 and Flash font reads:

| Observation | Result |
| --- | --- |
| Ordinary reopen at spine 41, saved page 2 | CSS retry occurred; basic layout failed after page 6 |
| Failing line-break workspace | 334 tokens, 2,672 bytes requested; maxAlloc 2,036 bytes |
| Free heap after CSS-retry cleanup in failing session | 36,756 bytes |
| Same candidate after a commanded reset | Spine 41 completed all 11 pages; restored offset 390/page 2 and continued into spine 42 |
| Free heap after CSS-retry cleanup following reset | 55,808 bytes |

The 19,052-byte difference identifies a session-dependent memory baseline, not
its owner. Standby clock synchronization/network lifetime is a hypothesis for
follow-up, not a confirmed leak or a fix included in this checkpoint. A reset
that permits reading does not resolve the ordinary-reopen failure.

The phase-closing candidate application has SHA256
`e64b85aa5c8b0f297526a75cdcf66c143211ad7a753ee9ba0bf0b0d9b9cd3da3`
and is 5,905,008 bytes. Its production source is recorded in commit `ef006bf0`;
use the binary hash to identify it, since the build began before that commit.
All 63 targeted host checks, clang-format checks, and the `default`,
`simulator_x3`, and `murphy_m4` builds passed. The hardware builds reported an
existing WebSockets dependency warning about deprecated `NetworkClient::flush()`.
X4 application flashing and hash verification succeeded; a separate capture
started for this candidate. Reading/visual acceptance remains incomplete and
must not inherit the preceding candidate's results.

Keep X3 acceptance, SD-direct font mode, and physical text completeness separate.
For subsequent candidates, record the new application hash and repeat cold-cache
opening, spine 5 and 41, cross-chapter turns, TOC navigation, exit/reopen, and
standby/clock-sync followed by reading. An index error during ordinary use fails
acceptance; safe error handling is acceptable only in additional-pressure tests.
Do not infer hardware acceptance from host serialization stubs or successful
firmware builds. Binary images, original books and device-specific raw logs are
local artifacts and are not committed to the repository.

### C3 Bluetooth page-turner development validation

See [C3 Bluetooth](c3-bluetooth.md) for the opt-in build profile, lifecycle and
font-memory ownership, reproducible checks, X4 evidence and remaining acceptance
limits. Default/release BLE remains disabled. Distinguish host/build success,
application hash verification and physical-device acceptance.

### Distinguishing TCP Stalls, Watchdogs, and Restarts

- A task-watchdog failure prints the task watchdog banner and subscribed task
  names before the register dump. A later `RTC_SW_CPU_RST` is the panic restart
  path; it does not make the original failure an intentional software restart.
- A normal software restart has no preceding watchdog or panic report. Check
  the earlier application log for an expected transition such as leaving
  network mode before treating the reset reason itself as a crash.
- A slow HTTP reader can fill the TCP send window and keep a synchronous SDK
  write waiting for several seconds. Correlate the request URI and elapsed
  time with the serial log. Do not classify this as OOM solely from the minimum
  historical heap value; also inspect current free heap and the largest
  allocatable block.
- Web request handlers execute on the task that calls `handleClient()`. Do not
  subscribe that task to the task watchdog merely so handlers can feed it: a
  legitimate SDK network wait can then be misclassified as a CPU lockup. Keep
  explicit watchdog resets conditional so other platform configurations remain
  compatible.

**Verification Steps**:
1. Check serial output for stack traces
2. Monitor heap with `ESP.getFreeHeap()` before/after operations
3. Verify task deletion with task list (`vTaskList()`)
4. Test with `LOG_LEVEL=2` (debug logging enabled)

---

## Testing and Verification Workflow

### Testing Checklist

**AI agent scope** (what you CAN verify):
1. ✅ **Build**: `pio run -t clean && pio run` (0 errors/warnings)
2. ✅ **Quality**: `./bin/ci-check` and `./bin/clang-format-fix --check`
3. ✅ **Format**: Commit messages (`feat:`/`fix:`), no `.gitignore`-excluded files staged (e.g., `*.generated.h`, `.pio/`, `platformio.local.ini`)
4. ✅ **CI**: Fix GitHub Actions failures before review
5. ✅ **Code review**: Ensure orientation-aware logic is correct in all 4 modes by inspecting switch/case coverage

**Human tester scope** (flag these for the user):
6. 🔲 **Device**: Test on hardware
7. 🔲 **Orientations**: Verify all 4 modes (Portrait/Inverted/Landscape CW/CCW)
8. 🔲 **Memory**: record `ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`,
   `ESP.getMaxAllocHeap()`, and task stack high-water marks. Memory-sensitive
   EPUB paths must still work when the largest contiguous block is about 16–19KB;
   no task may fall below 512 bytes of remaining stack.
9. 🔲 **Cache**: If EPUB modified, delete `.crosspoint/` and verify re-parse

### CI/CD Pipeline Awareness

**GitHub Actions** run automatically on pull requests:

| Workflow | File | Purpose |
|----------|------|---------|
| Core Build Check | `.github/workflows/ci.yml` | Builds `default` (shared X3/X4) and `x4pro` |
| Hardware CI | `.github/workflows/hardware-ci.yml` | Builds all simulators and global S3 targets for hardware-sensitive changes or manual runs |
| Format Check | `.github/workflows/pr-formatting-check.yml` | Validates clang-format |
| Firmware Release | `.github/workflows/nightly.yml` | Stable releases from SemVer tags and scheduled/manual Nightly releases |

**Rules**:
- **Fix CI failures BEFORE** requesting review
- CI runs on: Push to PR, PR updates
- Hardware CI runs only for its configured paths, or from **Run workflow**
- Format check fails → Run clang-format locally
- Build check fails → Fix compile errors

Firmware build jobs call `select-build-runner.yml` before they start. Trusted
same-repository PRs, pushes, tags, schedules, and manual runs use the H2O
self-hosted runner only when it is online and idle; fork PRs, Dependabot, a
missing `H2O_RUNNER_TOKEN`, API errors, and an unavailable H2O fall back to
`ubuntu-latest`. The token must be repository-scoped with read-only
Administration permission. Selection is best effort: a runner that disconnects
after selection can still leave the selected job queued. Formatting, static
analysis, unit tests, and GitHub publishing remain GitHub-hosted. Nightly COS
publishing is the exception: it requires the H2O labels with no fallback, has
read-only repository contents, and receives COS credentials but no GitHub write
credential. If H2O is unavailable, that regional job waits while the independent
GitHub publish job can continue.

---

## Serial Monitoring and Live Debugging

### Serial Monitor Options

1. **Enhanced**: `python3 scripts/debugging_monitor.py` (color-coded, recommended)
2. **Standard**: `pio device monitor` (basic, no colors)
3. **VS Code**: Monitor (🔌) button (IDE-integrated)

### Live Debugging Patterns

**Heap**: `LOG_DBG("MEM", "Free: %d", ESP.getFreeHeap());` (every 5s in loop)
**Stack**: `uxTaskGetStackHighWaterMark(nullptr)` (< 512 bytes → increase stack)
**Flush**: `logSerial.flush();` (force output before crash)

**Port Detection**: Windows: `mode` | Linux: `ls /dev/ttyUSB* /dev/ttyACM*` or `dmesg | grep tty`
