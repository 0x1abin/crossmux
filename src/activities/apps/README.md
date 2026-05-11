# Apps

The `apps/` directory holds all non-reader sub-applications shipped on CrossPoint Reader. They share a single entry point on the home screen (the "Apps" tile), a single dispatcher activity (`AppsMenuActivity`), and a small set of conventions documented below.

Reader, file browser, settings, OPDS, etc. are **not** apps — they are core e-reader functions and live as top-level `activities/<feature>/` directories. The `apps/` umbrella is for everything else: games, generators, toys.

---

## Directory layout

```
apps/
├── AppsMenuActivity.{h,cpp}   # dispatcher — see "Adding a new app" below
├── GameUi.{h,cpp}             # shared helpers, game-only (centering math, elapsed-time format)
├── GameSaveDebouncer.h        # 1.5s save debounce, used by sudoku/gomoku
├── sudoku/                    # one subdirectory per app, files keep the app-name prefix
├── gomoku/
└── avatar/
```

**Why the `Game*` prefix for `GameUi` and `GameSaveDebouncer`** — these helpers carry save-state and game-board semantics. They are used only by Sudoku/Gomoku, not by Ugly Avatar (which is a single-shot generator). The name reflects what they actually do; do not rename them to `App*`.

**Why nested rather than flat** — apps share the "Apps" launcher concept and should be groupable. Reader and Settings are top-level features, so they sit flat at `activities/<feature>/`. This is a deliberate structural difference, not an inconsistency.

**File naming inside an app directory** — keep the app-name prefix (`SudokuBoard.cpp`, not `Board.cpp`). The redundancy is intentional: grep, IDE search, and crash log lines stay unambiguous. No `namespace` wrapping for the same reason — class names are globally unique.

---

## Adding a new app

The dispatcher is table-driven. A new app needs **two** edits plus the app files themselves:

### 1. Create the app

```
apps/<myapp>/
  MyAppActivity.{h,cpp}   # required — extends Activity
  ...                     # game-specific board / store / generator as needed
```

The activity's `loop()` must handle Back by returning to the Apps menu, not the home screen:

```cpp
if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
  activityManager.goToApps();
}
```

### 2. Add a navigation method to `ActivityManager`

In `src/activities/ActivityManager.{h,cpp}`:

```cpp
// .h
void goToMyApp();

// .cpp
#include "apps/myapp/MyAppActivity.h"
void ActivityManager::goToMyApp() {
  replaceActivity(std::make_unique<MyAppActivity>(renderer, mappedInput));
}
```

### 3. Append one row to `kAppEntries`

In `apps/AppsMenuActivity.cpp`:

```cpp
constexpr AppEntry kAppEntries[] = {
    {StrId::STR_SUDOKU_TITLE, UIIcon::Sudoku,    &ActivityManager::goToSudoku},
    {StrId::STR_GOMOKU_TITLE, UIIcon::Gomoku,    &ActivityManager::goToGomoku},
    {StrId::STR_UGLY_AVATAR,  UIIcon::Avatar,    &ActivityManager::goToUglyAvatar},
    {StrId::STR_MYAPP_TITLE,  UIIcon::MyApp,     &ActivityManager::goToMyApp},  // new row
};
```

That's it — no enum, no `switch` cases, no `buildItems()`. The lambdas in `render()` and the dispatch in `loop()` both read the same table.

### 4. Add the i18n key and icon

- **i18n**: add `STR_MYAPP_TITLE: "My App"` to `lib/I18n/translations/english.yaml`. Other languages fall back to English; translate selectively. Then run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/` once locally (regenerated at build time too).
- **Icon**: add `MyApp` to the `UIIcon` enum in `src/components/themes/BaseTheme.h`, add a 32×32 1-bit bitmap header in `src/components/icons/`, and a `case UIIcon::MyApp:` branch in `src/components/themes/lyra/LyraTheme.cpp`'s `iconForName(size==32)` switch. The Apps menu uses `drawButtonMenu` which only reads the 32px variant — no 24px bitmap needed.

---

## UI conventions

- **Renderer**: the Apps menu uses `GUI.drawButtonMenu`, not `GUI.drawList`. That gives 32px icons, UI_12 font, 64px rows, 8px gap between rows, vertically centered text — matching the home screen tile style.
- **Header**: each app draws its own header via `GUI.drawHeader(... tr(STR_<APP>_TITLE))`.
- **Back button labels**: the four button hints follow the project standard — `STR_BACK / STR_SELECT / STR_DIR_UP / STR_DIR_DOWN` for menu rows; app-specific actions for in-game screens.

## Navigation flow

```
Home  ──Confirm "Apps"──▶  AppsMenu  ──Confirm row──▶  <App>
  ▲                            │
  └──────Back──────────────────┘    ◀──Back──  <App>  (returns to AppsMenu, not Home)
```

Every sub-app's Back button must call `activityManager.goToApps()`. This mirrors how Sudoku and Gomoku already behave, and how Ugly Avatar was wired up when it moved under `apps/`.

## Resource budget

Apps run on the same 380KB RAM ceiling as the reader. Specifically:

- **Heap**: allocate at `onEnter()`, free at `onExit()` (Activities are heap-allocated and `delete`d on exit). Don't hold buffers across navigation.
- **Stack**: keep local function variables under 256 bytes; large buffers go on heap or `static`.
- **Flash strings**: large constant tables must be `static constexpr` to stay in flash, not in DRAM.
- **SPIFFS writes**: never save on every user interaction. Debounce save-on-activity-exit, or use `GameSaveDebouncer` (1.5s window). SPIFFS erase cycles are finite.
- **Single-buffer framebuffer**: 48KB framebuffer is shared. If an app needs to overlay (modal save UI etc.), use `renderer.storeBwBuffer()` / `restoreBwBuffer()` — see `UglyAvatarActivity::onSave()` for a worked example.

See the top-level `CLAUDE.md` for the full resource protocol; apps are not exempt.
