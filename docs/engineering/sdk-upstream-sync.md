# FreeInk SDK synchronization — September 2026

This integration preserves CrossMux's device support and UI extensions while
bringing the SDK fork through upstream `14028b179cc5a85a7a4cf0ccb33980dce8737735`.
The `freeink-sdk` gitlink is the authoritative fork revision; the SDK review is
[freeink-sdk#24](https://github.com/0x1abin/freeink-sdk/pull/24). The simulator
remains pinned to `dacbbbcdc133a052f9122347429fdcb8cddf80af`, which already
contains its reviewed upstream snapshot. Reader behavior comparisons use
`da7feed5c7e777b2bb9be6e0848c9e42a5faea29`.

## Review decisions

- Preserve the fork's dark-background redrive. Add the UC8279 four-tone image
  bank with upstream's 60% timing and 25% coverage threshold. Its five 49-byte
  tables and initialization flag use 246 static bytes; bounded phase fields
  keep the temporary array at 140 bytes, without a new heap buffer.
- Keep device-specific input edges and transition isolation. Standalone Home
  events count as activity; a Home event during a held screen contact must not
  retire that contact's suppression.
- Keep the fork's SPI configuration. Large stream reads service a subscribed
  task watchdog and yield every 100 ms in the shared SD streaming routine.
- Preserve list styling, add section headings, and use measured row counts for
  clipped-list scrolling and scrollbar placement. Draw the scrollbar after a
  full-width selected background.
- Keep selected tab text and add optional arrows. Measure with the same style
  used for drawing; reuse each tab's measured geometry within the drawing pass.
  Content-width layout still needs a separate width preflight. No metrics cache
  or new allocation is introduced.
- Resolve solid foreground ink once. Native text must not invert white twice;
  the legacy renderer adapter retains its explicit-inverted semantics.

The temporary lightless-page control-center extension was removed completely.
There is no Sticky-specific replacement handler. Menu dispatch follows the
existing upstream capability rule:

| Input | No frontlight, including Sticky | Frontlight present, including X4 Pro |
| --- | --- | --- |
| Ordinary-page top-edge down-swipe | Delivered to the current page | Control center |
| Reading-page top-edge down-swipe | Reading menu | Control center |
| Reading-page center tap | Reading menu when enabled by settings | Same |

Status-bar taps retain the existing top-level page whitelist. The Inx recent
page does not acquire a new tap region. The control-center regression executes
production dispatch, including pending transitions, exclusive storage,
allocation failure, and suppression; it adds no production policy abstraction.

## Validation and limits

The SDK's four host suites retain both upstream and fork coverage. Regression
checks cover Home/contact overlap, clipped-list tails, selected-font arrows,
white foreground pixels, and generated UC8279 LUT bytes at five timing settings.
Tab measurement checks also cover content-width and equal-width layouts.

Final publication passed builds for `default`, `gh_release`, `x4c`, `eego_a4`,
`murphy_m4`, `waveshare_epaper_397`, and `sticky`. SDK integration also runs the
normal CrossMux CI, including the X4/X4 Pro build and hardware/simulator matrix.
A successful build is not a hardware acceptance result.

The user confirmed the three Sticky menu gestures above and reported normal
X4 validation after testing the candidate. Those hardware runs used CrossMux
`c2fb3467c39a3e067fe4e41303fa4226e40e986a` and SDK candidate tree
`0eb44b2461b3112c8bdc94035b1aa1cd1e4de86a`; the final review additionally reuses
tab metrics and integrates the current CrossMux base. These later changes are
covered by host/build checks, not a claim of another hardware run.

UC8279 physical waveform quality, Home/screen overlap timing, SD long-transfer
watchdog behavior, and other boards still need their own hardware acceptance.
Sticky logs also recorded existing duplicate framebuffer-storage and power-lock
warnings; the menu acceptance does not establish that these unrelated paths
are warning-free.

For repeatable validation, the sync tool exports the reviewed Git index into a
real directory rather than linking or copying an entire working directory.
Only staged sources enter the build; untracked debug files, ignored artifacts,
and Git metadata do not. The conflict scanner checks text files, excluding
binary font bytes. See the [sync skill](../../.claude/skills/sync-upstream/SKILL.md)
for the candidate and publication workflow.
