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
