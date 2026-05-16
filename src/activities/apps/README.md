# Apps

The `apps/` directory holds all non-reader sub-applications shipped on CrossPoint Reader. They share a single entry point on the home screen (the "Apps" tile), a single dispatcher activity (`AppsMenuActivity`), and a small set of conventions documented below.

Reader, file browser, settings, OPDS, etc. are **not** apps — they are core e-reader functions and live as top-level `activities/<feature>/` directories. The `apps/` umbrella is for everything else: games, generators, toys.

---

## Directory layout

```
apps/
├── AppsMenuActivity.{h,cpp}   # dispatcher — see "Adding a new app" below
├── GameUi.{h,cpp}             # shared helpers, game-only (centering math, elapsed-time format)
├── GameSaveDebouncer.h        # 1.5s save debounce, used by sudoku/gomoku/minesweeper
├── sudoku/                    # one subdirectory per app, files keep the app-name prefix
├── gomoku/
├── chinese-chess/             # conditional — gated by ENABLE_CHINESE_VERSION (see "Conditional apps" below)
├── minesweeper/
└── avatar/
```

**Why the `Game*` prefix for `GameUi` and `GameSaveDebouncer`** — these helpers carry save-state and game-board semantics. They are used by Sudoku, Gomoku, and Minesweeper, not by Ugly Avatar (which is a single-shot generator). The name reflects what they actually do; do not rename them to `App*`.

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
    {StrId::STR_SUDOKU_TITLE,      UIIcon::Sudoku,      &ActivityManager::goToSudoku},
    {StrId::STR_GOMOKU_TITLE,      UIIcon::Gomoku,      &ActivityManager::goToGomoku},
    {StrId::STR_MINESWEEPER_TITLE, UIIcon::Minesweeper, &ActivityManager::goToMinesweeper},
    {StrId::STR_UGLY_AVATAR,       UIIcon::Avatar,      &ActivityManager::goToUglyAvatar},
    {StrId::STR_MYAPP_TITLE,       UIIcon::MyApp,       &ActivityManager::goToMyApp},  // new row
};
```

That's it — no enum, no `switch` cases, no `buildItems()`. The lambdas in `render()` and the dispatch in `loop()` both read the same table.

### 4. Add the i18n key and icon

- **i18n**: add `STR_MYAPP_TITLE: "My App"` to `lib/I18n/translations/english.yaml`. Other languages fall back to English; translate selectively. Then run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/` once locally (regenerated at build time too).
- **Icon**: add `MyApp` to the `UIIcon` enum in `src/components/themes/BaseTheme.h`, add a 32×32 1-bit bitmap header in `src/components/icons/`, and a `case UIIcon::MyApp:` branch in `src/components/themes/lyra/LyraTheme.cpp`'s `iconForName(size==32)` switch. The Apps menu uses `drawButtonMenu` which only reads the 32px variant — no 24px bitmap needed.

---

### 5. (Optional) Conditional / compile-flag-gated apps

Some apps ship only in a subset of releases — e.g. Chinese Chess (`chinese-chess/`) ships only in the Chinese-only release (`env:gh_release_cn`) and the host simulator. The branch lives behind `#ifdef ENABLE_CHINESE_VERSION`.

A conditional app uses a **two-layer guard**: ifdef at every reference site, plus a `build_src_filter` exclusion in `platformio.ini` so the app's `.cpp` files aren't compiled at all when the flag is off. The ifdef alone is not enough — without the filter, the app's translation units still compile (and fail, since they freely reference each other without inner ifdefs).

When adding such an app, wrap every line in the four standard add-an-app edits with `#ifdef ENABLE_<FLAG>` and add two `platformio.ini` lines. Concretely, using `chinese-chess` as the reference:

| Touchpoint | What to wrap |
|---|---|
| `BaseTheme.h` | The new `UIIcon::<App>` enum variant |
| `themes/lyra/LyraTheme.cpp` | `#include "components/icons/<app>.h"` and the `case UIIcon::<App>:` branch in `iconForName` |
| `ActivityManager.{h,cpp}` | The `goTo<App>()` declaration, the `#include "apps/<app>/<App>MenuActivity.h"`, and the `goTo<App>()` definition |
| `AppsMenuActivity.cpp` | The `kAppEntries[]` row (`kAppCount` uses `sizeof/sizeof`, so it auto-adjusts) |
| `main.cpp` | App-specific font objects + `renderer.insertFont(...)` calls, if the app needs a custom font |
| `lib/EpdFont/builtinFonts/all.h` | `#include` of the app's font header |
| `platformio.ini` (base) | Add `-<activities/apps/<app>/>` to the default `build_src_filter` |
| `platformio.ini` (the gated env) | Add `-D<FLAG>` to `build_flags` and `+<activities/apps/<app>/>` to `build_src_filter` |
| `simulator/CMakeLists.txt` | Optionally add `-D<FLAG>=1` to `target_compile_definitions` so host debugging always covers the app |

**Do not** add inner `#ifdef <FLAG>` guards inside the app's own `*.cpp` / `*.h` files — `build_src_filter` already excludes the whole directory, and inner guards would just clutter the source. The app source code stays plain.

i18n keys (`STR_<APP>_*` in `english.yaml`) are **not** ifdef-guarded: the i18n generator has no conditional mechanism, and the few hundred bytes of unused string data in non-gated builds is acceptable.

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
