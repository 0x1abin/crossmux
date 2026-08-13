#pragma once

#include <HalGPIO.h>

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button {
    Back,
    Confirm,
    Left,
    Right,
    Up,
    Down,
    Power,
    PageBack,
    PageForward,
    NavNext,
    NavPrevious,
    ScreenLeft,
    ScreenRight,
    ScreenUp,
    ScreenDown
  };
  // Number of values in Button (Back..ScreenDown). Used to size the BLE overlay and
  // to clamp persisted BLE mappings. Keep in sync with the enum above.
  static constexpr uint8_t kButtonCount = 15;
  enum class SwipeDir { None, Left, Right, Up, Down };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(renderer) {}

  void update() const { gpio.update(); }
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool hasTouch() const;
  bool wasScreenTapped(int& x, int& y) const;
  bool wasScreenTouchDown(int& x, int& y) const;
  bool isScreenTouchHeld(int& x, int& y) const;
  bool wasTapInRect(int x, int y, int width, int height) const;
  bool wasListItemTapped(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  bool wasListItemTouchedDown(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                              bool hasSubtitle) const;

  // Combined touch interaction for a band of equal rows with caller-supplied
  // geometry — the shared hit-test for lists the theme helpers above do not
  // cover (custom row heights, option prompts, menus). Down = a held
  // tap-candidate is on a row (update the selection highlight); Tap = a tap
  // released on one (activate). rowHeight limits the hit to the top rowHeight
  // px of each step (0 = the full step, no gap band).
  enum class RowTouch : uint8_t { None, Down, Tap };
  RowTouch rowTouch(int& row, int top, int rowStep, int rowCount, int xStart = 0, int xEnd = INT32_MAX,
                    int rowHeight = 0) const;
  // Horizontal variant for side-by-side button pairs (confirmation prompts).
  RowTouch colTouch(int& col, int left, int colStep, int colCount, int yStart, int yEnd, int colWidth = 0) const;

  SwipeDir wasSwipe() const;
  bool wasHomeGesture() const;
  bool wasMenuGesture() const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  bool isHeld(const Button button) const;
  unsigned long getHeldTime() const;
  const GfxRenderer& getRenderer() const { return renderer; }
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Maps four screen-direction labels onto the two physical front-button roles
  // using the same live-orientation transform as ScreenLeft/Right/Up/Down.
  Labels mapDirectionalLabels(const char* back, const char* confirm, const char* left, const char* right,
                              const char* up, const char* down) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // --- BLE page-turner overlay -------------------------------------------------
  // Drain decoded key events from the FreeInk BLE HID host and translate the ones
  // bound in SETTINGS.bleKeyMap into per-frame logical-button edges that OR into
  // wasPressed()/isPressed()/wasReleased(). Call once per main-loop iteration,
  // right after gpio.update() and BleHid.poll(). No-ops when BLE is compiled out.
  void pollBle();
  // True when a mapped BLE key produced an edge this frame — keeps the inactivity
  // / auto-sleep timer alive while a remote is the only input device in use.
  bool bleHadActivityThisFrame() const { return bleActivityThisFrame; }
  // Capture mode: while on, pollBle() stops mapping events and instead stashes the
  // raw decoded key identity so the button-mapping UI can read it without racing the
  // live mapping over the single popKey() queue.
  void setBleCaptureMode(bool on);
  // Pop a captured (kind, value) key identity grabbed while in capture mode.
  // Returns false when nothing has been captured since the last call.
  bool takeCapturedBleKey(uint8_t& kind, uint8_t& value);

  // True when the control axis is flipped relative to the physical buttons: the user opted into
  // orientation-following front buttons AND the screen is *currently rendered* rotated (INVERTED /
  // LANDSCAPE_CCW). Keyed on the live renderer orientation rather than the persisted reader setting,
  // so portrait UI (home, settings) never swaps while the reader and its menus do.
  [[nodiscard]] bool isNavDirectionSwapped() const;

 private:
  HalGPIO& gpio;
  // Logical-to-physical button mapping depends on what the user is actually looking at: when the
  // screen is rendered rotated, the directional buttons must flip to match. The renderer is the only
  // authority on the *live* orientation (the reader rotates it and restores portrait on exit), so we
  // read it here instead of CrossPointSettings.orientation, which is just the persisted reader
  // preference and stays "rotated" even while portrait UI like home/settings is on screen.
  const GfxRenderer& renderer;

  Button mapScreenDirection(Button button) const;
  Labels mapFrontLabels(const char* back, const char* confirm, const char* left, const char* right) const;
  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  // OR-in the BLE overlay for a logical button, mirroring mapButton()'s composite
  // handling of NavNext/NavPrevious so a remote key bound to Up/Down/Left/Right also
  // drives list navigation.
  bool bleEdge(const bool* arr, Button button) const;

  // Per-frame BLE overlay, indexed by (uint8_t)Button.
  bool blePressEdge[kButtonCount] = {};    // press edge this frame   -> wasPressed / isPressed
  bool bleReleaseEdge[kButtonCount] = {};  // release edge this frame  -> wasReleased
  bool bleActivityThisFrame = false;
  bool bleCaptureMode = false;
  bool bleHasCaptured = false;
  uint8_t bleCapturedKind = 0xFF;
  uint8_t bleCapturedValue = 0;
  bool wasBackGesture() const;
  // Fetch the pending swipe (if any) and map both endpoints to logical screen coords
  bool decodeSwipe(int& sx, int& sy, int& ex, int& ey) const;
  bool listItemFromPoint(int x, int y, int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  void rememberTouchHeldTime() const;

  mutable bool touchHeldOverrideValid = false;
  mutable unsigned long touchHeldOverrideMs = 0;
  mutable unsigned long touchHeldOverrideAt = 0;
};
