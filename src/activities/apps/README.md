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
├── airpage/                    # cloud image display app
├── sudoku/                    # one subdirectory per app, files keep the app-name prefix
├── gomoku/
├── chinese-chess/             # conditional — gated by ENABLE_CHINESE_VERSION (see "Conditional apps" below)
├── minesweeper/
├── woodfish/                  # electronic woodfish with lazy SD checkpointing
└── avatar/
```

**Why the `Game*` prefix for `GameUi` and `GameSaveDebouncer`** — these helpers carry save-state and game-board semantics. They are used by Sudoku, Gomoku, and Minesweeper, not by Ugly Avatar (which is a single-shot generator). The name reflects what they actually do; do not rename them to `App*`.

**Why nested rather than flat** — apps share the "Apps" launcher concept and should be groupable. Reader and Settings are top-level features, so they sit flat at `activities/<feature>/`. This is a deliberate structural difference, not an inconsistency.

**File naming inside an app directory** — keep the app-name prefix (`SudokuBoard.cpp`, not `Board.cpp`). The redundancy is intentional: grep, IDE search, and crash log lines stay unambiguous. No `namespace` wrapping for the same reason — class names are globally unique.

---

## Adding a new app

The dispatcher is table-driven. A new app needs the app files, a navigation method, and one catalog entry:

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
  replaceActivityWith<MyAppActivity>();
}
```

`replaceActivityWith<T>(...)` and `startActivityForResultWith<T>(handler, ...)`
prepend `renderer` and `mappedInput` to the constructor arguments, allocate with
`makeUniqueNoThrow`, log OOM, and leave the current Activity in place on
failure. Do not construct Activities with `std::make_unique`.

### 3. Assign an ID and append one row to `kAppEntries`

In `apps/AppsMenuActivity.cpp`:

```cpp
enum class AppId : uint8_t {
  Sudoku = 2,
  Gomoku = 3,
  Minesweeper = 5,
  UglyAvatar = 7,
  MyApp = 9,  // new, never-reused bit ID
  Count = 10,
};

constexpr AppEntry kAppEntries[] = {
    {AppId::Sudoku,      StrId::STR_SUDOKU_TITLE,      UIIcon::Sudoku,      &ActivityManager::goToSudoku},
    {AppId::Gomoku,      StrId::STR_GOMOKU_TITLE,      UIIcon::Gomoku,      &ActivityManager::goToGomoku},
    {AppId::Minesweeper, StrId::STR_MINESWEEPER_TITLE, UIIcon::Minesweeper, &ActivityManager::goToMinesweeper},
    {AppId::UglyAvatar,  StrId::STR_UGLY_AVATAR,       UIIcon::Avatar,      &ActivityManager::goToUglyAvatar},
    {AppId::MyApp,       StrId::STR_MYAPP_TITLE,       UIIcon::MyApp,       &ActivityManager::goToMyApp},
};
```

The ID is the persisted bit position in `hiddenAppsMask`: allocate the next unused value, never reuse or change existing
values, and keep conditional-app IDs outside their `#ifdef`. New bits default to visible. Fresh settings hide Chinese
Chess, Minesweeper, 2048, Standby, and Buddy; existing masks are never overwritten. The menu, launcher, and App Visibility
settings all read this same table; no `switch` or `buildItems()` is needed.
The visibility mask is 32-bit. `Calculator = 15` and `Woodfish = 16` are stable;
IDs 17 through 31 remain available.

### 4. Add the i18n key and icon

- **i18n**: add `STR_MYAPP_TITLE: "My App"` to `lib/I18n/translations/english.yaml`. Other languages fall back to English; translate selectively. Then run `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/` once locally (regenerated at build time too).
- **Icon**: add `MyApp` to the `UIIcon` enum in `src/components/themes/BaseTheme.h` and add both 32×32 1-bit variants: the pre-rotated Lyra bitmap selected by `LyraTheme::iconForName(size==32)`, and the canonical-orientation bitmap selected by `InxAppIcons::get()`. No 24px bitmap is needed.

---

### 5. (Optional) Stateless toy apps

Some apps have no save state at all. Ugly Avatar is a single-screen generator that creates a new avatar on entry and exits cleanly. It skips both `GameSaveDebouncer` and any `*Store.{h,cpp}` layer, and its `Activity` is launched directly (no `*MenuActivity`). Use this pattern when the app has no "in-progress game" worth resuming; it cuts a few hundred lines and avoids persistent writes.

### Reusable implementation patterns

Choose the smallest pattern that fits:

| App shape | Reference | Reuse |
|---|---|---|
| Stateless, single screen | `avatar/UglyAvatarActivity` | `Activity`, `GameUi` action geometry; no Store or menu Activity |
| Stateful, single screen | `2048/Game2048Activity`, `woodfish/WoodfishActivity` | Board/state object plus `GameSaveDebouncer` or an app-specific idle checkpoint |
| Board game with launcher | `sudoku/` | Separate Board, Store, MenuActivity, GameActivity; `OptionPopup` for in-game menus |

Use `OptionPopup` for in-game action and difficulty menus. It owns touch hit
testing, logical-button navigation, and the inherited-input barrier. Build its
labels when the popup opens, not during rendering. Keep rules, win detection,
AI, and persistence in the game: they are not framework concerns.

---

### 6. (Optional) Conditional / compile-flag-gated apps

Regional apps such as Chinese Chess and WeRead are compiled into the unified
firmware and hidden unless the locked content profile is China.

A conditional app uses a **two-layer guard**: ifdef at every reference site, plus a `build_src_filter` exclusion in `platformio.ini` so the app's `.cpp` files aren't compiled at all when the flag is off. The ifdef alone is not enough — without the filter, the app's translation units still compile (and fail, since they freely reference each other without inner ifdefs).

When adding such an app, wrap every line in the four standard add-an-app edits with `#ifdef ENABLE_<FLAG>` and add two `platformio.ini` lines. Concretely, using `chinese-chess` as the reference:

| Touchpoint | What to wrap |
|---|---|
| `BaseTheme.h` | The new `UIIcon::<App>` enum variant |
| `themes/lyra/LyraTheme.cpp` | `#include "components/icons/<app>.h"` and the `case UIIcon::<App>:` branch in `iconForName` |
| `ActivityManager.{h,cpp}` | The `goTo<App>()` declaration, the `#include "apps/<app>/<App>MenuActivity.h"`, and the `goTo<App>()` definition |
| `AppsMenuActivity.cpp` | The unconditional stable `AppId` plus the guarded `kAppEntries[]` row (`kAppCount` auto-adjusts) |
| `main.cpp` | App-specific font objects + `renderer.insertFont(...)` calls, if the app needs a custom font |
| `lib/EpdFont/builtinFonts/all.h` | `#include` of the app's font header |
| `platformio.ini` (base) | Add `-<activities/apps/<app>/>` to the default `build_src_filter` |
| `platformio.ini` (the gated env) | Add `-D<FLAG>` to `build_flags` and `+<activities/apps/<app>/>` to `build_src_filter` |
| `platformio.ini` (simulator) | Add `-D<FLAG>=1` to `env:simulator` so X4/X3 host builds cover the app |

**Do not** add inner `#ifdef <FLAG>` guards inside the app's own `*.cpp` / `*.h` files — `build_src_filter` already excludes the whole directory, and inner guards would just clutter the source. The app source code stays plain.

i18n keys (`STR_<APP>_*` in `english.yaml`) are **not** ifdef-guarded: the i18n generator has no conditional mechanism, and the few hundred bytes of unused string data in non-gated builds is acceptable.

---

## UI conventions

- **Renderer**: the Apps menu uses `GUI.drawButtonMenu`, not `GUI.drawList`. That gives 32px icons, UI_12 font, 64px rows, vertically centered text — matching the home screen tile style. The Apps menu passes a halved inter-row gap (`metrics.menuSpacing / 2`, i.e. 4px on LYRA) to tighten the list; other callers keep the theme default.
- **Pagination**: when the list overflows one screen, the Apps menu renders only the current page's slice (offsetting `drawButtonMenu`'s index callbacks). Lyra-family themes use the shared right-side scrollbar; Classic and RoundedRaff retain Standby-style page dots. Item navigation flips pages automatically; no separate page-turn key.
- **Header**: each app draws its own header via `GUI.drawHeader(... tr(STR_<APP>_TITLE))`.
- **Result layouts**: center multi-line status/result blocks from measured font heights with `gameCenteredBlockY`; reserve the title bar and button-hint area instead of relying on fixed Y offsets.
- **Back button labels**: the four button hints follow the project standard — `STR_BACK / STR_SELECT / STR_DIR_UP / STR_DIR_DOWN` for menu rows; app-specific actions for in-game screens.

## Navigation flow

```
Home  ──Confirm "Apps"──▶  AppsMenu  ──Confirm row──▶  <App>
  ▲                            │
  └──────Back──────────────────┘    ◀──Back──  <App>  (returns to AppsMenu, not Home)
```

Every sub-app's Back button must call `activityManager.goToApps()`. This mirrors how Sudoku, Gomoku, Ugly Avatar, and AirPage behave.

Electronic Woodfish accepts Confirm and all four logical directions on button
release. Touch devices add taps on the rendered wooden body; whitespace,
mallet, ripples, drags, and the system Back gesture are not knocks. Its
`uint32_t` counter saturates, has no reset action, and is checkpointed to SD
after 60 seconds idle or on exit.

AirPage always enters on its QR page and stays offline until Refresh or live
mode needs Wi-Fi. Network-dependent apps use the shared Wi-Fi picker on demand;
cancelling it leaves the app on its current screen so the user can retry.
Its mapped bottom actions are Back, Settings, Images, and Refresh; logical
previous/next also map the side buttons to Images/Refresh in every orientation.
Touch devices render those QR-page actions as a tappable footer, and tapping a
full-screen image opens the same actions plus Set Cover in a modal menu.
Connecting or reconnecting never downloads an image by itself; only Refresh or
a live MQTT push starts a download.
Settings uses the standard themed list for manual/live mode and the optional
"set downloads as sleep screen" toggle. Images opens a newest-first list of the
current image plus up to 19 archived deliveries; selecting one displays it
full-screen, where Confirm offers the standard sleep-screen confirmation and
Back returns to the QR page.

Live mode runs only while AirPage is foregrounded, backs off failed
connections, and allows normal auto-sleep again after the retry window expires.
Manual mode keeps foreground Wi-Fi available but permits idle auto-sleep.
Downloads are identified by signature and accept BMP or JPEG from the same
endpoint. Exact duplicates reuse the current entry. Unique downloads are
validated transactionally before the old image is archived; JPEG uses the EPUB
aspect-fit, dithering, streamed pixel-cache, and 4-level grayscale path without
loading the full pixel cache into RAM. A selected JPEG is converted to a
fit-without-cropping BMP before atomically replacing `/sleep.bmp`.

## Resource budget

Apps run on the same 380KB RAM ceiling as the reader. Specifically:

- **Heap**: allocate at `onEnter()`, free at `onExit()` (Activities are heap-allocated and `delete`d on exit). Don't hold buffers across navigation.
- **Stack**: keep local function variables under 256 bytes; large buffers go on heap or `static`.
- **Flash strings**: large constant tables must be `static constexpr` to stay in flash, not in DRAM.
- **Storage writes**: never save on every user interaction. Debounce save-on-activity-exit, or use `GameSaveDebouncer` (1.5s window). Electronic Woodfish checkpoints its SD-backed counter only after 60 seconds idle or on exit.
- **Single-buffer framebuffer**: 48KB framebuffer is shared. If an app needs to overlay (modal save UI etc.), use `renderer.storeBwBuffer()` / `restoreBwBuffer()` — see `UglyAvatarActivity::onSave()` for a worked example.

See the top-level `CLAUDE.md` for the full resource protocol; apps are not exempt.
