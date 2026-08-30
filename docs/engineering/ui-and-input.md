# UI, Orientation & Input

> Deep reference for [CLAUDE.md](../../CLAUDE.md). All rendering goes through the
> `GUI`/UITheme macro; all input goes through logical buttons. Hardcoded screen
> dimensions or raw hardware button indices are bugs.

## Orientation-Aware Logic
* No Hardcoding: Never assume 800 or 480. Use renderer.getScreenWidth() and renderer.getScreenHeight().
* Viewable Area: Use renderer.getOrientedViewableTRBL() to stay within physical bezel margins.
* Safe Content: Start from `UITheme::getScreenSafeArea()`, then reserve the active theme's header, spacing, and
  footer metrics before fitting custom boards or grids.
* Coordinate Bounds: `drawLine()` endpoints are inclusive, while rectangle width and height are extents. A
  full-width line therefore ends at `renderer.getScreenWidth() - 1`, never at the screen width itself.

## Logical Button Mapping

**Source**: [src/MappedInputManager.cpp:20-55](../../src/MappedInputManager.cpp)

Constraint: Physical button positions are fixed on hardware, but their logical functions change based on user settings and screen orientation.

**Button Categories**:
1. **Physical Fixed** (Up/Down side buttons):
   - `Button::Up` → Always `HalGPIO::BTN_UP`
   - `Button::Down` → Always `HalGPIO::BTN_DOWN`

2. **User Remappable** (Front buttons):
   - `Button::Back` → Maps to `SETTINGS.frontButtonBack` (hardware index)
   - `Button::Confirm` → Maps to `SETTINGS.frontButtonConfirm`
   - `Button::Left` → Maps to `SETTINGS.frontButtonLeft`
   - `Button::Right` → Maps to `SETTINGS.frontButtonRight`

3. **Reader-Specific** (Page navigation with optional swap):
   - `Button::PageBack` → Uses side button (swappable via `SETTINGS.sideButtonLayout`)
   - `Button::PageForward` → Uses side button (swappable)

**Implementation**:
- Activities use **logical buttons** (e.g., `Button::Confirm`)
- `MappedInputManager` translates to **physical hardware buttons**
- User can remap front buttons in settings
- Orientation changes handled separately by renderer coordinate transforms

**Rule**: Always use `MappedInputManager::Button::*` enums, never raw `HalGPIO::BTN_*` indices (except in ButtonRemapActivity).

## Input Frames and Event Semantics

The main loop updates `MappedInputManager` once per frame before dispatching
the active Activity. Normal Activities only read that shared snapshot; they
must not call `mappedInput.update()` themselves. A second update can erase an
edge before another input owner sees it.

| Query | Meaning | Typical use |
|---|---|---|
| `wasPressed(button)` | Press edge in the current frame | Immediate action whose duration does not matter |
| `wasReleased(button)` | Release edge in the current frame | Short action that waits for a complete gesture |
| `isPressed(button)` | Current held state | Long-press timing and transition guards |

Use one physical gesture for one semantic action:

- Use the press edge for a simple immediate action when that gesture cannot
  cross an input-owner boundary.
- Use the release edge for a short action when short and long presses coexist,
  or when the next owner should open only after the gesture is complete.
- After a long-press action fires, keep a handled flag until release and
  consume that release instead of also running the short action.

In the main Settings menu, short presses move one item while a held
`NavNext`/`NavPrevious` gesture moves by a full visible page at the shared
continuous-navigation interval. Settings subpages retain their own input
semantics.

## Activity and Popup Transitions

An Activity, popup, and resumed parent are separate input owners. If ownership
changes while the triggering key is held, the receiving owner needs a
**release barrier**:

- When opening a child while its trigger is still held, the child waits until
  that logical button is released before accepting input.
- When a child closes on a press edge, its result callback arms the parent's
  barrier only if `isPressed()` is still true.
- The frame that observes the release clears the barrier and still returns.
  This consumes the release edge instead of allowing it to trigger the parent.

Minimal parent-side pattern:

```cpp
void ParentActivity::onChildResult() {
    waitForConfirmRelease_ =
        mappedInput.isPressed(MappedInputManager::Button::Confirm);
}

void ParentActivity::loop() {
    if (waitForConfirmRelease_) {
        if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
            waitForConfirmRelease_ = false;
        }
        return;  // Consume the release frame too.
    }

    // Handle normal input from the shared snapshot.
}
```

Initialize a barrier from the held state, not from an edge query: the callback
may run after the edge's frame. Do not simulate consumption with another
`update()`, a delay, or raw GPIO access. Continue using logical buttons.
Touch input remains independent; only arm a physical-button barrier when that
button is actually held.

Settings enums normally cycle in place when they have two choices and open an
`OptionPopup` when they have more. A dynamic enum marked with
`withManagedEnumPicker()` always opens the popup and receives callbacks only
when the selected index changes. Its setter owns the complete change lifecycle,
including persistence, error feedback, and any required restart; the generic
settings flow must not save or rebuild after it returns.

## Touch Coordinate and Gesture Layers

Touch controllers keep their sampling, power, and panel-mount transforms in
the FreeInk SDK. `HalGPIO` exposes the normalized contact, and
`MappedInputManager` maps it through the renderer's live orientation before
classifying direction and edge geometry with FreeInkUI. Activities assign the
meaning: left-edge right swipe is Back, top-edge down swipe is Menu, and
bottom-edge up swipe is Home on every touch device. A hardware Home key remains
an additional input path and does not change those screen gestures.

## Long-Press Pattern

Start timing on `wasPressed()`. While `isPressed()` remains true, fire the
long action once after its threshold and mark the gesture handled. On
`wasReleased()`, run the short action only if the long action did not fire,
then reset the gesture state.

## Input Verification Matrix

Verify the invariant: **one physical gesture, one input owner, one semantic
action**.

| Scenario | Expected result |
|---|---|
| Press opens a child or popup | The child ignores the inherited hold and its release |
| Release opens a child or popup | The child stays open and waits for a new gesture |
| Child closes on press | The resumed parent consumes that gesture's release |
| Long press fires | The threshold action runs once; release does not run the short action |
| Logical buttons are remapped | Behavior is unchanged because no raw GPIO is used |
| Touch activates the same UI | It is not blocked unless the physical button is actually held |

## UITheme (The GUI Macro)
* Rule: All UI rendering must go through the GUI macro (UITheme).
* Do not hardcode fonts, colors, or positioning. This ensures orientation-aware layout consistency.
* Paginated custom grids should call `GUI.drawSideScrollBar()` with their item
  count, page start, and page capacity so the active theme controls the bar
  dimensions and placement.
* INX top-level tabs are owned by `ActivityManager`; only Activities with a
  non-`None` `MainTab` participate. Left/Right and tab touches are consumed
  before the page sees them, while reader and feature subpages remain outside
  the top-level loop.
* `GUI.drawProgressBar()` returns the first free Y coordinate after the bar and
  optional percentage line. Callers place following text from that value rather
  than reproducing the theme's font or spacing calculations.
* Functional subpages derive their body from `SubpageLayout::contentRect()` so
  headers, optional subheaders, button hints, and footers have one authoritative
  boundary. Related text keeps at least 4 px of separation, independent blocks
  keep at least 12 px, and custom drawing is clipped to that body.
* `ConfirmationActivity` uses one standard presentation: a 12 pt bold heading
  and a page body wrapped to at most two 10 pt lines. It computes those lines in
  `onEnter()`, never in `render()`, so button-driven redraws stay allocation-free.
* Center a progress state with `GUI.measureProgressBarHeight()`, then use the Y
  returned by `GUI.drawProgressBar()` for everything that follows. This keeps
  percentage text and subsequent details correct when a theme changes fonts.
* Cover views that must fill a fixed frame call
  `GfxRenderer::drawBitmapCropToFill()`. It scales and center-crops through two
  bounded row buffers and returns `false` on invalid input, read failure, or OOM
  so the caller can draw its fallback cover.
* INX front-button hints use black text above a 50% gray bottom line. Empty
  actions draw neither label nor line; directional actions use `<` and `>`.
* On touch hardware, INX list and app-grid navigation focus follows the last
  input modality: touch hides it, while a physical button restores it. The
  logical selection and viewport remain intact, and FreeInkUI's active touch
  feedback plus semantic values such as checks and switches remain visible.

## Retained Framebuffer Updates

The firmware has one framebuffer, and its contents remain available after
`displayBuffer()`. A hot UI path may redraw only the changed items, but only
when it can identify the exact frame already in that buffer:

- Snapshot cross-task UI state once at the start of `render()`; never read a
  mutable selection again inside an item loop.
- Track the framebuffer's selection/page in render-task-owned state. Invalidate
  that state before shelf/list content changes, an asset finishes loading, or
  the layout changes.
- Clear and redraw the full screen when the retained frame is invalid or the
  target is on another page. Otherwise restore the old item and draw the new
  item; do not allocate a second buffer.
- After drawing, record what the framebuffer now contains, then re-read the
  cross-task state. If it changed, request an immediate render and return before
  `displayBuffer()` so the stale frame never reaches the panel. Keep the recorded
  framebuffer state even for that skipped panel update, because the next render
  builds from it.

Incremental drawing does not imply a different panel waveform or windowed
refresh. Those are display-driver decisions and require separate hardware
measurement.

### Lyra Carousel home

Lyra Carousel displays one centered recent-book cover in a `380x540` frame.
The complete image is scaled proportionally to fit and centered with white
letterboxing when its aspect ratio differs; it is never cropped or stretched.
The frame is drawn only while the cover row has focus. Its Apps menu icon uses
a theme-local `28x28` four-cell drawing inside the standard `32x32` icon slot;
shared icon assets and other themes remain unchanged.

> User-facing text must use the `tr()` macro — see
> [hardware-constraints.md](hardware-constraints.md) → Resource Protocol rule 5,
> and the i18n workflow in [generated-files.md](generated-files.md).
