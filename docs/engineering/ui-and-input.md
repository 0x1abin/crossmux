# UI, Orientation & Input

> Deep reference for [CLAUDE.md](../../CLAUDE.md). All rendering goes through the
> `GUI`/UITheme macro; all input goes through logical buttons. Hardcoded screen
> dimensions or raw hardware button indices are bugs.

## Orientation-Aware Logic
* No Hardcoding: Never assume 800 or 480. Use renderer.getScreenWidth() and renderer.getScreenHeight().
* Viewable Area: Use renderer.getOrientedViewableTRBL() to stay within physical bezel margins.

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

> User-facing text must use the `tr()` macro — see
> [hardware-constraints.md](hardware-constraints.md) → Resource Protocol rule 5,
> and the i18n workflow in [generated-files.md](generated-files.md).
