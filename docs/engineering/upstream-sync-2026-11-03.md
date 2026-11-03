# Upstream integration review — 2026-11-03

This draft integrates three frozen upstream revisions on feature branches. No
base branch is merged or changed. The original CrossMux checkout is preserved.

| Component | Fork base | Reviewed upstream | Integration dependency |
| --- | --- | --- | --- |
| Reader | `6b5566c844667857acebfe565d373423cdd16e06` | `136c26553980579d9f9c9176f11ce0870087cb80` (develop) | This branch |
| SDK | `65323afd0cbfa7bcfbabafd509248d024ceaab14` | `1e1cec259e7b983be2b9762f41fbf350e138cca9` (main) | [SDK #49](https://github.com/0x1abin/freeink-sdk/pull/49), `d9a1304cd234b0536136302f629304b609f62b32` |
| Simulator | `1cbe1b775c9b37e23551d2ca9334052f1fb3b0e3` | `4c78f3ff51b5b1e5d2f69f595dba79bc159c4c1f` (main) | [Simulator #13](https://github.com/0x1abin/crosspoint-simulator/pull/13), `e5e716a40379d925ef6a31f7aafccf0b2f8ebcdb` |

The SDK candidate preserves the histories of SDK #45, #47 and #48. The Reader
pins that common candidate rather than sequencing three incompatible gitlinks
from CrossMux #429, #449 and #443. Its wolfSSL consumer patches are required
with the new SDK reservation contract; do not transplant just this gitlink into
an older Reader. Dependency branches are reviewable without merging their main
branches. Final merges and physical acceptance belong to the maintainer.

## Resolution decisions

- **SDK display and boards:** combine upstream asynchronous refresh/baseline,
  BMI270, touch and board improvements with CrossMux native-gray/optical paths,
  Picco power-down policy, stable appended board enum values and per-board SD
  frequency. Preserve the aligned 105-byte SSD1677 LUT, shared-SPI MISO/CS fix,
  and GPIO6 ownership based on an acknowledged gauge probe. A bus failure must
  retry; a valid gauge absence selects the display rail. No blanket GPIO6 mode.
- **SDK networking:** retain bounded streaming/capture APIs while appending the
  new prefix callback. TLS reservation now belongs to each connection. The
  Reader patch retains an input buffer between records and releases an empty,
  unreferenced buffer before growth. Forced teardown still zeroes/frees it.
  Patch anchors fail closed on unknown wolfSSL source. The connection reservation
  is 16,704 bytes; it is not a global heap loan or a device heap measurement.
- **Simulator:** keep NVS paths, validation, atomic writes and recursive SD
  transactions. Add upstream byte storage, HEAD and timeout compatibility while
  retaining final/duplicate headers and cancellation. Network workers queue
  requests; handlers execute on the application's `handleClient()` thread.
- **Reader text/cache/fonts:** combine upstream CJK breaks, focus/RTL spacing,
  line-height options and font point-size metadata with compact CJK12 aliases,
  SD-font paths, fork render-spec fields and grayscale routes. Section versions
  are 88/89 (normal/Chinese); partial versions are 194/195. Render-signature seed
  invalidation discards stale layout anchors. Existing user state layout stays
  unchanged. Merge promoted refresh through the existing one-shot override.
- **Settings and activities:** use the shared bounded descriptor visitor, keep
  fork setting IDs/persistence/capability checks and actions, and migrate legacy
  line spacing. Preserve download cancellation/retry and OPDS/render ownership.
  Keep CrossMux apps and reading statistics. New SDK rectangles are explicitly
  initialized where the current host compiler requires it.
- **Themes:** keep the INX tab and four-tile control panel geometry. Other themes
  use current upstream safe areas and icon control rows. Preserve compact reader
  font exceptions and canonical English theme names in Chinese. Tests compare
  actual incoming SDK/Reader drawing and hit geometry, not substituted metrics.
- **Web:** retain bilingual assets, bounded file listing and startup-failure
  cleanup; use one checked nothrow route-handler allocation owned by WebServer.
  Add upstream plugin pages, host/cookie handling, streamed settings and shared
  assets. Keep the Chinese EPUB helper; rewrite its nested template string for
  the vendored minifier and test generated gzip JavaScript. The old shared plugin
  host include remains because Chinese pages still consume it.
- **Documentation/build:** keep the CrossMux AGENTS map, engineering docs,
  branding and release workflows. Do not resurrect old upstream release jobs or
  `.agents/rules/generated-files-cache.md`: its applicable guidance is already
  in `docs/engineering/generated-files.md`. Retain incoming license/notices,
  build minifiers and platform-cache changes. Add host zlib CI dependency.
  SDK agent-guide incoming delta was empty; simulator thread ownership guidance
  was updated in its existing CLAUDE.md.

Two deliberate fork exceptions need to remain visible in review: SleepActivity
keeps CrossMux's native-gray/layered/original user-cover pipeline instead of the
incoming custom-sleep BMP pipeline. Dictionary keeps complete, bounded homograph
groups and failure semantics instead of adopting the incoming inline-type parser
wholesale. The incoming BMP converter and its tests are included independently.
Neither exception is evidence that an open hardware or dictionary feature issue
has been resolved.

## Verification

Passed locally on macOS/Apple clang:

- CMake host build and all 1,518 tests in the original merged CTest registration.
  The newly adapted Text Settings geometry and release-license tests also pass
  and are now registered for CI. The geometry test covers all fork Style labels
  through scrolling, rather than assuming four upstream rows.
- C3 reading-statistics/network and download-lifecycle memory checks.
- Real patched wolfSSL reservation tests with sanitizers: both baseline flags
  and alignment variants, admission rejection and connection ownership.
- SDK driver/bus, input/battery/IMU, SD, font, UI, JSON/catalog and HTTP suites;
  UI reports 321,385 passing checks. Focused GPIO6, LUT and shared-SPI regressions.
- Simulator eight-profile compatibility, NVS/clock/storage, release-duration and
  application-thread network tests; real loopback HTTP timeout/header/cancel tests.
- Read Pico display/input checks pass for all four orientations and power-only
  wake/relaunch. The wake test now sets its script before GPIO initialization,
  which upstream changed to parse startup input. Before this fixture correction,
  the orientations passed but wake hung; the corrected full check took 6.17 seconds
  locally. Its CI step now has a five-minute timeout.
- Bilingual generated web JavaScript checks, keyboard geometry/behavior, INX
  historical style traces and current-upstream theme/menu comparisons. The theme
  comparison reports 9,360 upstream traces and 1,872 INX bridge traces.

Host warning-only compatibility flags were used for existing Apple clang
linkage/unused-variable/tag diagnostics. This is not a claim of a warning-free
unmodified Apple build. Initial failures from stale test interfaces were fixed;
initial concurrent PlatformIO builds raced on generated i18n headers, so final
builds run serially. Generated outputs are not committed.

Firmware/simulator build and final CI status are recorded in the pull request.
No physical hardware, sleep current, SD concurrency endurance, panel waveform,
full-page screenshot parity or long-reading heap acceptance has been performed.
Drawing/hit traces and compilation do not establish those results. Do not mark
related issues fixed until the maintainer merges and the relevant checks pass.

## Reviewed Reader overlap inventory

All 60 unresolved-index paths from the manual merge were resolved, together with
the following paths changed on both sides since the merge base. The decisions
above apply by behavior chain. Additional cross-file adaptations include the
TLS patch, Chinese web assets, renderer cache consumers and geometry test stubs.
The helper's initial `start` failed before producing review state; this is a
manual review record, not a fabricated helper approval or publish-state file.

```text
.agents/rules/generated-files-cache.md
.gitattributes
.github/workflows/ci.yml
.github/workflows/release.yml
.github/workflows/release_candidate.yml
LICENSE
USER_GUIDE.md
docs/contributing/architecture.md
docs/dictionary.md
docs/file-formats.md
docs/i18n.md
docs/sd-card-fonts.md
docs/sd-plugins.md
docs/webserver-endpoints.md
docs/webserver.md
freeink-sdk
lib/EpdFont/EpdFontData.h
lib/EpdFont/EpdFontFamily.h
lib/EpdFont/SdCardFontManager.cpp
lib/EpdFont/TtfEpdFont.cpp
lib/EpdFont/TtfEpdFont.h
lib/EpdFont/scripts/fontconvert.py
lib/Epub/Epub/ParsedText.cpp
lib/Epub/Epub/ReaderRenderSpec.h
lib/Epub/Epub/Section.cpp
lib/Epub/Epub/blocks/TextBlock.cpp
lib/Epub/Epub/converters/DirectPixelWriter.h
lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h
lib/GfxRenderer/Bitmap.cpp
lib/GfxRenderer/Bitmap.h
lib/GfxRenderer/FontCacheManager.cpp
lib/GfxRenderer/FontCacheManager.h
lib/GfxRenderer/GfxRenderer.cpp
lib/GfxRenderer/GfxRenderer.h
lib/I18n/translations/arabic.yaml
lib/I18n/translations/azerbaijani.yaml
lib/I18n/translations/belarusian.yaml
lib/I18n/translations/bosnian.yaml
lib/I18n/translations/bulgarian.yaml
lib/I18n/translations/catalan.yaml
lib/I18n/translations/czech.yaml
lib/I18n/translations/danish.yaml
lib/I18n/translations/dutch.yaml
lib/I18n/translations/english.yaml
lib/I18n/translations/finnish.yaml
lib/I18n/translations/french.yaml
lib/I18n/translations/german.yaml
lib/I18n/translations/hebrew.yaml
lib/I18n/translations/hungarian.yaml
lib/I18n/translations/indonesia.yaml
lib/I18n/translations/italian.yaml
lib/I18n/translations/kazakh.yaml
lib/I18n/translations/lithuanian.yaml
lib/I18n/translations/norwegian.yaml
lib/I18n/translations/orangutan.yaml
lib/I18n/translations/persian.yaml
lib/I18n/translations/polish.yaml
lib/I18n/translations/portuguese-BR.yaml
lib/I18n/translations/portuguese-PT.yaml
lib/I18n/translations/romanian.yaml
lib/I18n/translations/russian.yaml
lib/I18n/translations/slovak.yaml
lib/I18n/translations/slovenian.yaml
lib/I18n/translations/spanish.yaml
lib/I18n/translations/swedish.yaml
lib/I18n/translations/turkish.yaml
lib/I18n/translations/ukrainian.yaml
lib/I18n/translations/valencian.yaml
lib/I18n/translations/vietnamese.yaml
lib/InflateReader/InflateReader.h
lib/hal/HalFrontlight.cpp
lib/hal/HalGPIO.cpp
lib/hal/HalGPIO.h
platformio.ini
scripts/patch_pioarduino_cache.py
src/CrossPointSettings.cpp
src/CrossPointSettings.h
src/CrossPointState.cpp
src/CrossPointState.h
src/MappedInputManager.cpp
src/MappedInputManager.h
src/SdCardFontSystem.cpp
src/SettingsList.cpp
src/SettingsList.h
src/activities/ActivityManager.cpp
src/activities/UiTabListActivity.cpp
src/activities/boot_sleep/SleepActivity.cpp
src/activities/boot_sleep/SleepActivity.h
src/activities/browser/OpdsBookBrowserActivity.cpp
src/activities/library/LibraryListActivity.cpp
src/activities/plugins/PluginCatalogActivity.cpp
src/activities/reader/ClipSelectionActivity.cpp
src/activities/reader/EpubReaderActivity.cpp
src/activities/reader/EpubReaderActivity.h
src/activities/reader/EpubReaderMenuActivity.cpp
src/activities/reader/EpubReaderMenuActivity.h
src/activities/reader/ReaderActivity.cpp
src/activities/reader/ReaderToolbarUi.cpp
src/activities/reader/XtcReaderActivity.cpp
src/activities/settings/FontDownloadActivity.cpp
src/activities/settings/FontDownloadActivity.h
src/activities/settings/TextSettingsActivity.cpp
src/activities/settings/TextSettingsActivity.h
src/activities/settings/TextSettingsPreview.cpp
src/activities/settings/TextSettingsPreview.h
src/activities/util/FrontlightPanelActivity.cpp
src/components/icons/listIcons.manifest
src/components/themes/BaseTheme.cpp
src/components/themes/BaseTheme.h
src/main.cpp
src/network/CrossPointWebServer.cpp
src/network/CrossPointWebServer.h
src/network/WebDAVHandler.cpp
src/network/html/assets/web.css
src/network/html/shared/Header.html.inc
src/util/Dictionary.cpp
src/util/Dictionary.h
test/CMakeLists.txt
test/chapter_html_slim_parser/CMakeLists.txt
test/chapter_html_slim_parser/ChapterHtmlSlimParserTest.cpp
test/chapter_html_slim_parser/stubs/GfxRenderer.h
test/chapter_html_slim_parser/stubs/HalStorage.h
test/em_dash_line_breaking/CMakeLists.txt
test/em_dash_line_breaking/EmDashLineBreakingTest.cpp
test/font_cache_manager/FontCacheManagerTest.cpp
test/korean_line_breaking/CMakeLists.txt
test/test_clipping_selection_text.py
test/test_recursive_dictionary.py
```
