#!/usr/bin/env python3
"""Compile and run the production tap/page-turn settings path with host stubs.

Review feedback on the touch-zone work asked for a real-execution regression
covering the stored values, not a re-reading of the switch statements:

  * the settings row "Disabled" must store PAGE_TURN_GESTURE_DISABLED (4), not
    the row index 3, which is the legacy INVERTED_TAP and still turns pages;
  * a stored value must survive save + reload through the same JSON path the
    device uses, and come back with the same behaviour;
  * a stored INVERTED_TAP (a save written before the row was folded away) must
    be migrated on load instead of being displayed as "Tap Only" forever;
  * the reader's tap classifier must keep the page-turn direction semantics the
    stock outer-thirds zones had: mirrored reader or right-to-left book swaps
    the sides, the two together cancel, and each direction's own gesture still
    decides whether taps are accepted;
  * the row has to hold end to end: a stored 4 must come back out of the load
    path and then make the real classifier's tap branch quiet, while a stored 3
    run through the same path still turns -- one direction alone would also
    pass for a value that silently folded back to Tap Only.

Nothing here re-implements the production logic: SettingsList.h, the
CrossPointSettings load/save pair and ReaderUtils::detectTouchPageTurn() are
compiled from the real sources and only their collaborators are stubbed.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def read(path):
    return (ROOT / path).read_text()

def brace_block(text, start):
    """Return text[start:end] spanning one balanced {...} block."""
    assert text[start] == '{', text[start : start + 40]
    depth = 0
    for index in range(start, len(text)):
        depth += (text[index] == '{') - (text[index] == '}')
        if depth == 0:
            return text[start : index + 1]
    raise ValueError('unbalanced braces')

def prefixed_source(text, declarations, keep):
    """Keep only the top-level blocks whose opening line matches keep()."""
    out = []
    index = 0
    while index < len(text):
        for prefix in declarations:
            if text.startswith(prefix, index):
                block_start = text.index('{', index)
                block = brace_block(text, block_start)
                head = text[index:block_start]
                if keep(head):
                    out.append(head + block)
                index = block_start + len(block)
                break
        else:
            index += 1
    return '\n'.join(out)

def settings_list_source():
    text = read('src/SettingsList.h')
    wanted = (
        'pageTurnGestureValues',
        'pageTurnGestureDisplayValue',
        'setPageTurnGestureFromDisplayValue',
        'shortPowerButtonDisplayValue',
        'setShortPowerButtonFromDisplayValue',
        'getBaseSettingsList',
    )
    blocks = prefixed_source(
        text,
        tuple('inline ' for _ in wanted) + ('static ',),
        lambda head: any(name in head for name in wanted),
    )
    # The base list is trimmed to the two page-turn rows: this regression is
    # about their stored values, and every other row would drag in unrelated
    # enums and collaborators.
    start = blocks.index('baseList = [] {')
    end = blocks.index('return v;', start) + len('return v;')
    body = blocks[start:end]
    # Keep the two page-turn rows verbatim. Each entry is one
    # `SettingInfo::DynamicEnum( ... ),` call; find it by its label and copy up
    # to the balanced closing paren, which the inner calls make unsafe to
    # search for by text.
    kept = []
    for entry_start in re.finditer(r'SettingInfo::', body):
        cursor = body.index('(', entry_start.end() - 1)
        depth = 0
        for index in range(cursor, len(body)):
            depth += (body[index] == '(') - (body[index] == ')')
            if depth == 0:
                entry = body[entry_start.start() : index + 2]  # include the trailing comma
                break
        if 'STR_NEXT_PAGE_GESTURE' in entry or 'STR_PREV_PAGE_GESTURE' in entry:
            kept.append(entry.rstrip().rstrip(','))
    assert len(kept) == 2, kept
    trimmed = (
        'baseList = [] {\n    std::vector<SettingInfo> v;\n'
        '    v.insert(v.end(), {\n        ' + ',\n        '.join(kept) + ',\n    });\n    return v;'
    )
    blocks = blocks[:start] + trimmed + blocks[end:]
    assert 'STR_NEXT_PAGE_GESTURE' in blocks and 'STR_PREV_PAGE_GESTURE' in blocks
    return blocks

def gesture_enums():
    header = read('src/CrossPointSettings.h')
    start = header.index('enum PAGE_TURN_GESTURE')
    return header[start : header.index('};', start) + 2]

def persistence_loader_source():
    """The generic persistence loop from fromJson(), verbatim.

    Loading is where the stored byte decides behaviour, and the loop is the part
    that clamps an enum to the number of labels: a value that the row count
    forgot (Disabled, 4) is rewritten to the struct default before the migration
    ever sees it. Compiling the real loop is the only way to catch that.
    """
    text = read('src/CrossPointSettings.cpp')
    # toJson() has a near-identical loop further up; anchor on the load-side
    # comment so the save path is never compiled in here.
    start = text.index('  for (const auto& info : getBaseSettingsList()) {', text.index('// Legacy migration'))
    block = brace_block(text, text.index('{', start))
    # The loop is a loader; the only harness-side change is that the default it
    # reads back for a missing key is the struct member itself, which the stub
    # JsonDocument cannot hand out by reference.
    # Two name-only adaptions, both spelled out with asserts so a rename in the
    # production source breaks this regression loudly instead of silently
    # testing nothing:
    #  * the loop lives outside the class and writes through `s`, so its
    #    "needs a resave" flag has nowhere to go here;
    #  * SettingType is qualified with SettingInfo because the harness keeps it
    #    nested (production has it at namespace scope).
    for legacy, replacement in (
        ('const char* raw = doc[info.key].is<const char*>() ? doc[info.key].as<const char*>() : nullptr;',
         'const char* raw = nullptr;  // no string-valued keys in the list under test'),
        ('obfuscation::deobfuscateFromBase64(doc[obfKey] | "", info.stringMaxLen - 1, &ok, &tooLong)',
         'obfuscation::deobfuscateFromBase64("", info.stringMaxLen - 1, &ok, &tooLong)'),
        ('needsResave = true;', '(void)0;'),
        ('if (info.type == SettingType::ENUM) {', 'if (info.type == SettingInfo::Type::ENUM) {'),
        ('} else if (info.type == SettingType::TOGGLE) {', '} else if (info.type == SettingInfo::Type::TOGGLE) {'),
        ('} else if (info.type == SettingType::VALUE) {', '} else if (info.type == SettingInfo::Type::VALUE) {'),
    ):
        assert legacy in block, legacy
        block = block.replace(legacy, replacement)
    # The uiTheme special case only widens a theme list; the rows under test are
    # plain enums. Drop just the theme arm of the nested conditional and keep the
    # generic one (enumValueCount, then the row count) so the assert below still
    # proves the value range is honoured.
    count_start = block.index('const uint8_t count =')
    count_end = block.index(';', block.index('enumLabels().size()', count_start)) + 1
    count_expr = block[count_start:count_end]
    theme_arm = re.search(r'\? static_cast<uint8_t>\(COVER_GRID \+ 1\)\s*:\s*', count_expr)
    assert theme_arm, count_expr
    assert 'info.enumValueCount != 0' in count_expr, count_expr
    count_expr = count_expr[: theme_arm.start()] + count_expr[theme_arm.end() :]
    count_expr = count_expr.replace('const uint8_t count = info.valuePtr == &CrossPointSettings::uiTheme',
                                    'const uint8_t count =')
    block = block[:count_start] + count_expr + block[count_end:]
    assert 'SettingType' not in block and 'uiTheme' not in block and 'COVER_GRID' not in block
    return 'for (const auto& info : getBaseSettingsList()) ' + block

def touch_page_turn_source():
    utils = read('src/activities/reader/ReaderUtils.h')
    keep = (
        'struct TouchPageTurn',
        'gestureAllowsSwipe',
        'gestureAllowsTap',
        'miniFallbackZone',
        'zoneRawShortAction',
        'zoneShortAction',
        'zoneLongAction',
        'tapZoneAt',
        'tapZoneAction',
        'detectTouchPageTurn',
    )
    blocks = prefixed_source(utils, ('inline ', 'struct ', 'static '),
                             lambda head: 'TouchPageTurn' in head or any(k in head for k in keep))
    # prefixed_source() copies the braces only; a struct needs its terminator.
    blocks = re.sub(r'(struct TouchPageTurn \{[^{}]*\})(?!;)', r'\1;', blocks)
    for name in keep:
        assert name in blocks, f'{name} missing from the extracted touch source'
    grid_start = utils.index('struct TapZoneGrid {')
    # brace_block() returns "{ ... }"; the struct keyword and the terminator are
    # added here so the body itself stays verbatim.
    grid = 'struct TapZoneGrid ' + brace_block(utils, utils.index('{', grid_start)) + ';'
    # Production stores zone actions in byte arrays, so the extracted helpers
    # only meet an enum in that one conditional; give it the same type there.
    blocks = blocks.replace('? SETTINGS.tapZones[fallback] : CrossPointSettings::TAP_ZONE_NONE',
                            '? SETTINGS.tapZones[fallback] : static_cast<uint8_t>(CrossPointSettings::TAP_ZONE_NONE)')
    return grid + '\n\n' + blocks


def touch_gesture_loader_source():
    """The production loader, verbatim (see src/CrossPointSettings.cpp)."""
    text = read('src/CrossPointSettings.cpp')
    start = text.index('bool CrossPointSettings::loadTouchGestureSettings(')
    brace = text.index('{', start)
    depth = 0
    for index in range(brace, len(text)):
        depth += (text[index] == '{') - (text[index] == '}')
        if depth == 0:
            return text[start : index + 1]
    raise ValueError('unbalanced braces in loadTouchGestureSettings')

def touch_gesture_constants():
    """The legacy touch-mode constants the loader reads, from their real homes."""
    header = read('src/CrossPointSettings.h')
    source = read('src/CrossPointSettings.cpp')
    out = []
    # enum TOUCH_READER_CONTROLS { OFF, ON, COUNT } - keep the OFF/ON names.
    begin = header.index('enum TOUCH_READER_CONTROLS')
    out.append('enum ' + header[begin + len('enum ') : header.index('}', begin) + 1] + ';')
    for line in source.splitlines():
        stripped = line.strip()
        if stripped.startswith(('constexpr uint8_t TOUCH_CONTROLS_VERSION', 'constexpr uint8_t LEGACY_TOUCH_')):
            out.append(stripped)
    joined = '\n'.join(out)
    for name in ('TOUCH_READER_OFF', 'TOUCH_READER_ON', 'TOUCH_CONTROLS_VERSION', 'LEGACY_TOUCH_SWIPE',
                 'LEGACY_TOUCH_INVERTED_TAP'):
        assert name in joined, f'{name} not found for the loader test'
    return joined

HARNESS = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// ---- collaborators the compiled production code expects ----------------
#define LOG_ERR(...) ((void)0)

namespace freeink { namespace MemoryManager {
struct Instance {
  size_t ensureFree_calls = 0;
  size_t ensureFree(size_t) { return ++ensureFree_calls; }
};
inline Instance& instance() {
  static Instance stub;
  return stub;
}
} }  // namespace freeink

enum class StrId : uint16_t {
  NONE, STR_TAP_AND_SWIPE, STR_TAP_ONLY, STR_SWIPE_ONLY, STR_DISABLED,
  STR_NEXT_PAGE_GESTURE, STR_PREV_PAGE_GESTURE, STR_CAT_CONTROLS, STR_COUNT
};
inline const char* i18nName(StrId id) {
  switch (id) {
    case StrId::STR_TAP_AND_SWIPE: return "Tap & Swipe";
    case StrId::STR_TAP_ONLY: return "Tap Only";
    case StrId::STR_SWIPE_ONLY: return "Swipe Only";
    case StrId::STR_DISABLED: return "Disabled";
    default: return "?";
  }
}
struct I18n {
  const char* get(StrId id) const { return i18nName(id); }
};
inline I18n& i18nInstance() {
  static I18n i;
  return i;
}

// Legacy touch-mode constants live in the .cpp in production; the extracted
// loader body reads them, and so does this harness.
''' + touch_gesture_constants() + r'''

// A saved device settings file, as the loader sees it: only the keys a test
// writes exist, every other key reads as null (which is what an older save
// without them looks like).
struct JsonDocument {
  std::vector<std::pair<std::string, uint8_t>> keys;
  const JsonDocument* parent = nullptr;  // subscription: doc["legacy"] -> child
  bool present = false;
  uint8_t value = 0;

  void set(const char* key, uint8_t v) { keys.emplace_back(key, v); }

  // ArduinoJson subscriptions: the index is evaluated against the root.
  const JsonDocument& operator[](const char* key) const {
    if (parent) return (*parent)[key];
    for (const auto& entry : keys) {
      if (entry.first == key) {
        static thread_local JsonDocument hit;
        hit.parent = nullptr;
        hit.present = true;
        hit.value = entry.second;
        hit.keys.clear();
        return hit;
      }
    }
    static const JsonDocument absent;
    return absent;
  }

  bool isNull() const { return !present; }
  template <typename T>
  bool is() const {
    return present;
  }
  template <typename T>
  T as() const {
    return static_cast<T>(value);
  }
};

using JsonVariantConst = JsonDocument;
using JsonObjectConst = JsonDocument;

// ArduinoJson's `variant | default` reads a missing key as the default.
inline uint8_t operator|(const JsonDocument& v, uint8_t fallback) { return v.present ? v.value : fallback; }
// `doc["touchReaderControls"] | TOUCH_READER_ON` with an enumerator operand is
// ambiguous against the byte overload above; the production code spells that
// one with an explicit static_cast<uint8_t>.
inline uint8_t operator|(const JsonDocument& v, const int& enumerator) {
  return v.present ? v.value : static_cast<uint8_t>(enumerator);
}
inline const char* operator|(const JsonDocument& v, const char* fallback) {
  return v.present ? "" : fallback;
}

// Helpers the persistence loop calls. Obfuscated string fields exist in the
// real settings but not in the two rows under test, so the decoder is a stub.
constexpr size_t OBF_KEY_BUF = 64;
namespace obfuscation {
inline std::string deobfuscateFromBase64(const char*, size_t, bool* ok, bool* tooLong) {
  *ok = false;
  *tooLong = false;
  return {};
}
}  // namespace obfuscation
inline uint8_t clamp(const uint8_t value, const uint8_t count, const uint8_t fallback) {
  return value < count ? value : fallback;
}
inline void copyToField(char* dest, const char* src, size_t maxLen) {
  snprintf(dest, maxLen, "%s", src);
}

struct CrossPointSettings {
''' + gesture_enums() + r'''
  // Action tables the zones and the settings list address by name.
  enum TAP_ZONE_ACTION {
    TAP_ZONE_NONE = 0,
    TAP_ZONE_PREV = 1,
    TAP_ZONE_NEXT = 2,
    TAP_ZONE_MENU = 3,
    TAP_ZONE_BOOKMARK = 4,
    TAP_ZONE_DICTIONARY = 5,
    TAP_ZONE_CHAPTER = 13,
    TAP_ZONE_FOOTNOTE = 14,
    TAP_ZONE_ACTION_COUNT
  };
  enum TAP_ZONE_LONG_ACTION {
    TAP_ZONE_LONG_NONE = 0,
    TAP_ZONE_LONG_BOOKMARK = 1,
    TAP_ZONE_LONG_DICTIONARY = 2,
    TAP_ZONE_LONG_ACTION_COUNT
  };
  enum SHOW_READER_MENU { READER_MENU_OFF = 0, READER_MENU_TAP = 1, READER_MENU_SWIPE_UP = 2 };
  // Short power-button action table (the base list builds its row).
  enum SHORT_PWRBTN { IGNORE = 0, PAGE_TURN = 1, FOOTNOTES = 2, CREATE_CLIPPING = 3, PWR_CONFIRM = 4 };
  enum HOME_BUTTON_ACTION { NONE_ACTION = 0 };

  // Page/touch fields the compiled settings list and classifier read.
  uint8_t pageTurnGesture = SWIPE_ONLY;
  uint8_t previousPageGesture = SWIPE_ONLY;
  uint8_t touchReaderControls = 1;
  uint8_t pageTurnDirection = 0;
  uint8_t showReaderMenu = 0;
  uint8_t shortPwrBtn = 0;
  uint8_t tapZones[9] = {static_cast<uint8_t>(TAP_ZONE_PREV), static_cast<uint8_t>(TAP_ZONE_NONE),
                         static_cast<uint8_t>(TAP_ZONE_NEXT), static_cast<uint8_t>(TAP_ZONE_PREV),
                         static_cast<uint8_t>(TAP_ZONE_MENU), static_cast<uint8_t>(TAP_ZONE_NEXT),
                         static_cast<uint8_t>(TAP_ZONE_PREV), static_cast<uint8_t>(TAP_ZONE_NONE),
                         static_cast<uint8_t>(TAP_ZONE_NEXT)};
  uint8_t miniZones[6] = {static_cast<uint8_t>(TAP_ZONE_NONE), static_cast<uint8_t>(TAP_ZONE_NONE),
                          static_cast<uint8_t>(TAP_ZONE_NONE), static_cast<uint8_t>(TAP_ZONE_NONE),
                          static_cast<uint8_t>(TAP_ZONE_NONE), static_cast<uint8_t>(TAP_ZONE_NONE)};
  uint8_t tapZonesLong[9] = {TAP_ZONE_LONG_NONE, TAP_ZONE_LONG_NONE, TAP_ZONE_LONG_NONE,
                             TAP_ZONE_LONG_NONE, TAP_ZONE_LONG_NONE, TAP_ZONE_LONG_NONE,
                             TAP_ZONE_LONG_NONE, TAP_ZONE_LONG_NONE, TAP_ZONE_LONG_NONE};
  int saveCalls = 0;
  void saveToFile() { ++saveCalls; }

  // Declared here, defined from the production .cpp below -- the same split the
  // real header/source pair uses.
  bool loadTouchGestureSettings(JsonVariantConst doc);
};
static CrossPointSettings SETTINGS;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
''' + touch_gesture_loader_source() + r'''
#pragma GCC diagnostic pop



struct SettingInfo {
  enum class Type { TOGGLE, ENUM, VALUE, STRING, ACTION };
  StrId nameId = StrId::NONE;
  Type type = Type::ENUM;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  int16_t CrossPointSettings::* signedValuePtr = nullptr;
  size_t stringOffset = 0;
  uint8_t stringMaxLen = 0;
  bool obfuscated = false;
  // Number of storable values when it exceeds the row count (0 = row count).
  uint8_t enumValueCount = 0;
  struct {
    int min = 0;
    int max = 255;
  } valueRange;
  std::vector<StrId> enumValues;
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  const char* key = nullptr;
  StrId category = StrId::NONE;
  StrId action = StrId::NONE;
  bool managedEnumPicker = false;

  const std::vector<StrId>& enumLabels() const { return enumValues; }

  template <typename S, typename F>
  static SettingInfo bind(S, StrId nameId, F ptr, const char* key, StrId category) {
    SettingInfo s;
    s.nameId = nameId;
    s.valuePtr = ptr;
    s.key = key;
    s.category = category;
    return s;
  }
  template <typename V>
  static SettingInfo Enum(StrId nameId, uint8_t CrossPointSettings::* ptr, V values, const char* key,
                          StrId category) {
    SettingInfo s = bind(0, nameId, ptr, key, category);
    s.enumValues.assign(values.begin(), values.end());
    return s;
  }
  static SettingInfo Toggle(StrId nameId, uint8_t CrossPointSettings::* ptr, const char* key, StrId category) {
    SettingInfo s = bind(0, nameId, ptr, key, category);
    s.type = Type::TOGGLE;
    return s;
  }
  static SettingInfo Action(StrId nameId, StrId action, StrId category) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = Type::ACTION;
    s.action = action;
    s.category = category;
    return s;
  }
  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, std::function<uint8_t()> getter,
                                 std::function<void(uint8_t)> setter, const char* key,
                                 StrId category, uint8_t CrossPointSettings::* field,
                                 uint8_t valueCount = 0) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = Type::ENUM;
    s.valuePtr = field;
    s.enumValues = std::move(values);
    s.enumValueCount = valueCount;
    s.valueGetter = std::move(getter);
    s.valueSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }
};

namespace BoardConfig {
inline bool hasTouch() { return true; }
inline bool hasHomeKey() { return true; }
inline bool isX4Pro() { return true; }
inline bool hasPwmFrontlight() { return true; }
}  // namespace BoardConfig
struct TiltSensor {
  bool isAvailable() const { return false; }
};

#include "SettingsListProduction.h"

// ---- the real load path -------------------------------------------------
// The persistence loop, compiled from CrossPointSettings.cpp, walks the very
// same getBaseSettingsList() the device walks: an entry missing its field
// binding, or an enum stored beyond its row count, fails here exactly as it
// would on a reboot. Runs in the same order as fromJson(): generic loop first,
// then the touch-gesture migration.
static bool isSettingAvailableForPersistence(const SettingInfo&) { return true; }

static void loadSettingsDocument(const JsonDocument& doc) {
  CrossPointSettings& s = SETTINGS;
  ''' + persistence_loader_source() + r'''
  s.loadTouchGestureSettings(doc);
}

// ---- reader side --------------------------------------------------------
struct Rect { int16_t x, y, width, height; };
struct GfxRenderer {
  int w = 480, h = 800;
  int getScreenWidth() const { return w; }
  int getScreenHeight() const { return h; }
  void tapToLogical(float, float, int& lx, int& ly) const {
    lx = lx_in;
    ly = ly_in;
  }
  mutable int lx_in = 0, ly_in = 0;
};
struct MappedInputManager {
  enum class SwipeDir { None, Left, Right, Up, Down };
  bool touch = true;
  bool tapped = false;
  int tapX = 0, tapY = 0;
  SwipeDir swipeDir = SwipeDir::None;
  bool hasTouch() const { return touch; }
  SwipeDir wasSwipe() const { return swipeDir; }
  bool wasScreenTapped(int& x, int& y) const {
    if (!tapped) return false;
    x = tapX;
    y = tapY;
    return true;
  }
};
struct GpioStub {
  bool candidate = false;
  unsigned long held = 0;
  bool isTouchTapCandidate(float&, float&, unsigned long& ms) const {
    ms = held;
    return candidate;
  }
  void suppressTouchContact() {}
  unsigned long lastTouchHeldMs() const { return held; }
};
static GpioStub gpio;
static constexpr unsigned long BOOKMARK_HOLD_MS = 400;

#include "TouchPageTurnProduction.h"

// ---- harness ------------------------------------------------------------
static void resetSettings();
static int failures = 0;
static void expect(bool ok, const char* what, long got, long want) {
  if (ok) return;
  std::printf("FAIL %s: got %ld, want %ld\n", what, got, want);
  ++failures;
}

static const SettingInfo* find(StrId id) {
  for (const auto& s : getBaseSettingsList()) {
    if (s.nameId == id) return &s;
  }
  return nullptr;
}

// Reload the way the device does after a fresh save: the real load path over a
// document that carries the stored gesture pair (everything else keeps its
// struct-initialiser default). The master toggle is part of every save -- it is
// a base setting -- so the migration sees it exactly as on the device.
static void reloadFromDocument(uint8_t storedPage, uint8_t storedPrev) {
  resetSettings();  // a reboot starts from the struct initialisers
  JsonDocument doc;
  doc.set("touchReaderControls", TOUCH_READER_ON);
  doc.set("pageTurnGesture", storedPage);
  doc.set("previousPageGesture", storedPrev);
  loadSettingsDocument(doc);
}

// A device save that predates the per-direction gesture pair: it still carries
// the old combined selector, optionally with its own tap zones (0 = not set).
static void reloadLegacySave(uint8_t legacyMode, uint8_t customZone) {
  resetSettings();
  JsonDocument doc;
  doc.set("touchReaderControls", legacyMode);
  if (customZone) {
    SETTINGS.tapZones[0] = customZone;
    // The loader only asks whether the key exists (it is a JSON array on the
    // device); the stub cannot model the array, and the loader never reads it
    // here.
    doc.set("tapZones", customZone);
  }
  loadSettingsDocument(doc);
}

// One contact at (x, y), classified by the production function.
struct Turn {
  bool prev, next;
};
static Turn tapAt(GfxRenderer& renderer, int x, int y) {
  renderer.lx_in = x;
  renderer.ly_in = y;
  MappedInputManager input;
  input.tapped = true;
  input.tapX = x;
  input.tapY = y;
  gpio.candidate = false;
  gpio.held = 0;
  const TouchPageTurn r = detectTouchPageTurn(renderer, input);
  return {r.prev, r.next};
}
[[maybe_unused]] static Turn swipe(MappedInputManager::SwipeDir dir) {
  MappedInputManager input;
  input.swipeDir = dir;
  gpio.candidate = false;
  GfxRenderer renderer;
  const TouchPageTurn r = detectTouchPageTurn(renderer, input);
  return {r.prev, r.next};
}

static void resetSettings() {
  // A fresh boot with no page-turn keys in the file: every field back to its
  // struct initialiser, exactly what the device starts from before fromJson()
  // runs. Loading is explicit (reloadFromDocument), as on the device.
  SETTINGS.pageTurnGesture = CrossPointSettings::SWIPE_ONLY;
  SETTINGS.previousPageGesture = CrossPointSettings::SWIPE_ONLY;
  SETTINGS.pageTurnDirection = 0;
  SETTINGS.touchReaderControls = 1;
  SETTINGS.showReaderMenu = 0;
  const uint8_t zones[9] = {CrossPointSettings::TAP_ZONE_PREV, CrossPointSettings::TAP_ZONE_NONE,
                            CrossPointSettings::TAP_ZONE_NEXT, CrossPointSettings::TAP_ZONE_PREV,
                            CrossPointSettings::TAP_ZONE_MENU, CrossPointSettings::TAP_ZONE_NEXT,
                            CrossPointSettings::TAP_ZONE_PREV, CrossPointSettings::TAP_ZONE_NONE,
                            CrossPointSettings::TAP_ZONE_NEXT};
  for (int i = 0; i < 9; ++i) SETTINGS.tapZones[i] = zones[i];
}

int main() {
  resetSettings();
  const SettingInfo* next = find(StrId::STR_NEXT_PAGE_GESTURE);
  const SettingInfo* prev = find(StrId::STR_PREV_PAGE_GESTURE);
  if (!next || !prev) {
    std::printf("FAIL the gesture settings are missing from the base list\n");
    return 1;
  }
  // The entries must persist: a DynamicEnum without the field binding is
  // skipped by CrossPointSettings::toJson() and never reaches the file.
  expect(next->valuePtr == &CrossPointSettings::pageTurnGesture, "next entry persists", 0,
         next->valuePtr == nullptr);
  expect(prev->valuePtr == &CrossPointSettings::previousPageGesture, "prev entry persists", 0,
         prev->valuePtr == nullptr);

  // 1. Every row, as the settings UI walks it, stores the enum the label says.
  //    Row 3 is the regression: it used to store 3 == INVERTED_TAP.
  const struct {
    const char* label;
    uint8_t stored;
    bool turnsPages;
  } rows[] = {
      {"Tap & Swipe", CrossPointSettings::TAP_AND_SWIPE, true},
      {"Tap Only", CrossPointSettings::TAP_ONLY, true},
      {"Swipe Only", CrossPointSettings::SWIPE_ONLY, false},
      {"Disabled", CrossPointSettings::PAGE_TURN_GESTURE_DISABLED, false},
  };
  expect(next->enumValues.size() == 4, "four rows", static_cast<long>(next->enumValues.size()), 4);
  for (int row = 0; row < 4; ++row) {
    // What the user sees for that row.
    expect(i18nName(next->enumValues[row]) == std::string(rows[row].label), "row label", row, row);
    // Selecting the row writes through the production setter.
    next->valueSetter(static_cast<uint8_t>(row));
    prev->valueSetter(static_cast<uint8_t>(row));
    expect(SETTINGS.pageTurnGesture == rows[row].stored, "stored enum for the row", SETTINGS.pageTurnGesture,
           rows[row].stored);
    expect(SETTINGS.previousPageGesture == rows[row].stored, "stored enum for the row (previous)",
           SETTINGS.previousPageGesture, rows[row].stored);
    // And reads back as the same row.
    expect(next->valueGetter() == row, "row round-trips", next->valueGetter(), row);
    // Tap classification follows the stored value, not the row index.
    GfxRenderer renderer;
    const Turn right = tapAt(renderer, 460, 400);  // right column = NEXT zone
    const Turn left = tapAt(renderer, 20, 400);    // left column = PREV zone
    expect(right.next == rows[row].turnsPages, "row turns pages on the next zone", right.next, rows[row].turnsPages);
    expect(left.prev == rows[row].turnsPages, "row turns pages on the prev zone", left.prev, rows[row].turnsPages);
  }

  // 2. Save / reload keeps the selected row (this is the path the old
  //    "Disabled" broke: it stored 3 and a reload showed Tap Only).
  next->valueSetter(3);
  prev->valueSetter(3);
  expect(SETTINGS.pageTurnGesture == CrossPointSettings::PAGE_TURN_GESTURE_DISABLED, "disabled stored",
         SETTINGS.pageTurnGesture, CrossPointSettings::PAGE_TURN_GESTURE_DISABLED);
  resetSettings();
  reloadFromDocument(CrossPointSettings::PAGE_TURN_GESTURE_DISABLED, CrossPointSettings::PAGE_TURN_GESTURE_DISABLED);
  expect(SETTINGS.pageTurnGesture == CrossPointSettings::PAGE_TURN_GESTURE_DISABLED, "disabled survives reload",
         SETTINGS.pageTurnGesture, CrossPointSettings::PAGE_TURN_GESTURE_DISABLED);
  expect(find(StrId::STR_NEXT_PAGE_GESTURE)->valueGetter() == 3, "disabled shows as Disabled",
         find(StrId::STR_NEXT_PAGE_GESTURE)->valueGetter(), 3);

  // 3. A save written while Inverted Tap still existed holds 3. It must keep
  //    behaving as the tap-only mode it always was, and must not be displayed
  //    as "Disabled" -- 3 is the neighbouring row of the new value.
  resetSettings();
  reloadFromDocument(CrossPointSettings::INVERTED_TAP, CrossPointSettings::INVERTED_TAP);
  expect(SETTINGS.pageTurnGesture == CrossPointSettings::INVERTED_TAP, "legacy inverted tap kept verbatim",
         SETTINGS.pageTurnGesture, CrossPointSettings::INVERTED_TAP);
  expect(find(StrId::STR_NEXT_PAGE_GESTURE)->valueGetter() == 1, "legacy value shows Tap Only",
         find(StrId::STR_NEXT_PAGE_GESTURE)->valueGetter(), 1);
  {
    GfxRenderer renderer;
    const Turn right = tapAt(renderer, 460, 400);
    // Inverted Tap meant "the right edge goes back". The reader no longer folds
    // the gesture itself, so the migration moves that swap into the stock zones:
    // the save keeps its stored 3 and still goes back on the right edge.
    expect(right.prev, "legacy inverted tap keeps its swapped side", right.prev, 1);
    expect(!right.next, "legacy inverted tap does not turn next on the right", right.next, 0);
    expect(SETTINGS.tapZones[0] == CrossPointSettings::TAP_ZONE_NEXT, "legacy inverted tap swapped the stock zones",
           SETTINGS.tapZones[0], CrossPointSettings::TAP_ZONE_NEXT);
  }

  // 3b. The old combined selector: 1 = tap, 2 = swipe, 3 = inverted tap. The
  //     loader splits it into the master toggle plus the pair, and Inverted Tap
  //     additionally swaps the stock zones unless the save had its own.
  {
    const struct {
      uint8_t legacy;
      uint8_t page;
      uint8_t prev;
      bool swappedZones;
    } legacyModes[] = {
        {1, CrossPointSettings::TAP_ONLY, CrossPointSettings::TAP_ONLY, false},
        {2, CrossPointSettings::SWIPE_ONLY, CrossPointSettings::SWIPE_ONLY, false},
        {3, CrossPointSettings::INVERTED_TAP, CrossPointSettings::INVERTED_TAP, true},
    };
    for (const auto& mode : legacyModes) {
      reloadLegacySave(mode.legacy, false);
      expect(SETTINGS.touchReaderControls == TOUCH_READER_ON, "legacy mode enables touch controls",
             SETTINGS.touchReaderControls, TOUCH_READER_ON);
      expect(SETTINGS.pageTurnGesture == mode.page, "legacy mode -> next gesture", SETTINGS.pageTurnGesture, mode.page);
      expect(SETTINGS.previousPageGesture == mode.prev, "legacy mode -> previous gesture", SETTINGS.previousPageGesture,
             mode.prev);
      const bool swapped = SETTINGS.tapZones[0] == CrossPointSettings::TAP_ZONE_NEXT;
      expect(swapped == mode.swappedZones, "legacy mode swaps the stock zones", swapped, mode.swappedZones);
      // A save that already has its own zones keeps them: the loader swaps the
      // stock grid only when the save has no tapZones at all.
      reloadLegacySave(mode.legacy, CrossPointSettings::TAP_ZONE_BOOKMARK);
      expect(SETTINGS.tapZones[0] == CrossPointSettings::TAP_ZONE_BOOKMARK, "legacy mode keeps a custom zone",
             SETTINGS.tapZones[0], CrossPointSettings::TAP_ZONE_BOOKMARK);
    }
  }

  // 4. Every stored value stays inside the four rows the UI can show.
  for (int stored = 0; stored <= 4; ++stored) {
    const uint8_t row = pageTurnGestureDisplayValue(static_cast<uint8_t>(stored));
    expect(row < 4, "display row in range", row, 4);
  }

  // 4b. Loading is idempotent: the second load of the same save (a reboot after
  //     a resave, which keeps the same keys) must not swap the zones again or
  //     change the gestures. A migration that re-runs is what the reviewer
  //     asked to rule out.
  for (uint8_t legacy : {uint8_t{1}, uint8_t{2}, uint8_t{3}}) {
    resetSettings();
    JsonDocument doc;
    doc.set("touchReaderControls", legacy);
    loadSettingsDocument(doc);
    const uint8_t pageOnce = SETTINGS.pageTurnGesture;
    const uint8_t prevOnce = SETTINGS.previousPageGesture;
    const uint8_t zoneOnce = SETTINGS.tapZones[0];
    // The device resaves after a migration, so the second load already carries
    // the gesture keys and must leave everything alone.
    JsonDocument afterResave = doc;
    afterResave.set("pageTurnGesture", pageOnce);
    afterResave.set("previousPageGesture", prevOnce);
    // The resave carries the version key (toJson() writes it), which is what
    // stops the side swap from running again.
    afterResave.set("touchControlsVersion", TOUCH_CONTROLS_VERSION);
    loadSettingsDocument(afterResave);
    expect(SETTINGS.pageTurnGesture == pageOnce, "second load keeps the next gesture", SETTINGS.pageTurnGesture,
           pageOnce);
    expect(SETTINGS.previousPageGesture == prevOnce, "second load keeps the previous gesture",
           SETTINGS.previousPageGesture, prevOnce);
    expect(SETTINGS.tapZones[0] == zoneOnce, "second load does not swap the zones again", SETTINGS.tapZones[0],
           zoneOnce);
  }

  // 5. Direction semantics: the classic outer-thirds record (left = prev,
  //    right = next) must behave like the stock zones for every combination of
  //    mirrored reader and right-to-left book, on taps and on swipes.
  const struct {
    const char* what;
    uint8_t direction;
    bool rtl;
    bool expectRightNext;
  } combos[] = {
      {"normal ltr", 0, false, true},
      {"mirrored ltr", 1, false, false},
      {"normal rtl", 0, true, false},
      {"mirrored rtl", 1, true, true},
  };
  for (const auto& c : combos) {
    resetSettings();
    SETTINGS.pageTurnGesture = CrossPointSettings::TAP_AND_SWIPE;
    SETTINGS.previousPageGesture = CrossPointSettings::TAP_AND_SWIPE;
    SETTINGS.pageTurnDirection = c.direction;
    GfxRenderer renderer;
    MappedInputManager input;
    input.tapped = true;
    input.tapX = 460;
    input.tapY = 400;
    gpio.candidate = false;
    const TouchPageTurn right = detectTouchPageTurn(renderer, input, c.rtl);
    input.tapX = 20;
    const TouchPageTurn left = detectTouchPageTurn(renderer, input, c.rtl);
    expect(right.next == c.expectRightNext, c.what, right.next, c.expectRightNext);
    expect(right.prev == !c.expectRightNext, c.what, right.prev, !c.expectRightNext);
    expect(left.prev == c.expectRightNext, c.what, left.prev, c.expectRightNext);
    expect(left.next == !c.expectRightNext, c.what, left.next, !c.expectRightNext);
    // The same combination must hold for swipes in the matching direction.
    MappedInputManager swipeInput;
    swipeInput.swipeDir = c.expectRightNext ? MappedInputManager::SwipeDir::Left : MappedInputManager::SwipeDir::Right;
    gpio.candidate = false;
    const TouchPageTurn swipeTurn = detectTouchPageTurn(renderer, swipeInput, c.rtl);
    expect(swipeTurn.next, c.what, swipeTurn.next, 1);
    expect(!swipeTurn.prev, c.what, swipeTurn.prev, 0);
  }
  {
    // A left swipe is "next" in the normal left-to-right reader and "previous"
    // once mirrored, exactly like before the tap zones were configurable.
    resetSettings();
    SETTINGS.pageTurnGesture = CrossPointSettings::TAP_AND_SWIPE;
    SETTINGS.previousPageGesture = CrossPointSettings::TAP_AND_SWIPE;
    SETTINGS.pageTurnDirection = 0;
    GfxRenderer renderer;
    MappedInputManager input;
    input.swipeDir = MappedInputManager::SwipeDir::Left;
    gpio.candidate = false;
    expect(detectTouchPageTurn(renderer, input, false).next, "left swipe turns next (ltr)", 1, 1);
    SETTINGS.pageTurnDirection = 1;
    expect(detectTouchPageTurn(renderer, input, false).prev, "left swipe turns prev when mirrored", 1, 1);
    SETTINGS.pageTurnDirection = 0;
    expect(detectTouchPageTurn(renderer, input, true).prev, "left swipe turns prev in an rtl book", 1, 1);
  }

  // 6. Each direction keeps its own gesture gate, including after a swap:
  //    next = Disabled must silence the next zone without touching prev, both
  //    in the normal and in the mirrored layout.
  for (int mirrored : {0, 1}) {
    resetSettings();
    SETTINGS.pageTurnDirection = static_cast<uint8_t>(mirrored);
    SETTINGS.pageTurnGesture = CrossPointSettings::PAGE_TURN_GESTURE_DISABLED;
    SETTINGS.previousPageGesture = CrossPointSettings::TAP_ONLY;
    GfxRenderer renderer;
    const Turn right = tapAt(renderer, 460, 400);
    const Turn left = tapAt(renderer, 20, 400);
    // Whichever physical side carries "next" after the fold, the disabled slot
    // must stay silent and the tap slot must fire.
    const Turn nextSide = mirrored ? left : right;
    const Turn prevSide = mirrored ? right : left;
    expect(!nextSide.next, "disabled next never taps", nextSide.next, 0);
    expect(prevSide.prev, "the other direction keeps its tap", prevSide.prev, 1);
    expect(!right.next || !right.prev, "a tap fires at most one direction", right.next + right.prev, 1);
  }

  // 7. A user-assigned zone follows the same fold as the stock grid: its
  //    stored action is a reading-direction slot, not a fixed physical side,
  //    so mirroring still means "the other hand's next".
  resetSettings();
  SETTINGS.pageTurnGesture = CrossPointSettings::TAP_AND_SWIPE;
  SETTINGS.previousPageGesture = CrossPointSettings::TAP_AND_SWIPE;
  SETTINGS.pageTurnDirection = 1;
  SETTINGS.tapZones[2] = CrossPointSettings::TAP_ZONE_PREV;  // top-right goes back
  {
    GfxRenderer renderer;
    const Turn topRight = tapAt(renderer, 460, 100);
    expect(topRight.next, "mirrored: the top-right zone turns forward", topRight.next, 1);
    expect(!topRight.prev, "mirrored: the top-right zone does not go back", topRight.prev, 0);
  }

  // 8. The end-to-end case behind the "Disabled" row: a stored
  //    PAGE_TURN_GESTURE_DISABLED (4) must survive the real load path -- it is
  //    only kept by enumValueCount = PAGE_TURN_GESTURE_COUNT(5), the row count
  //    would clamp it to 3 -- and the tap branch of the real classifier must
  //    then stay silent. The neighbouring stored 3 (INVERTED_TAP) walks the
  //    same load + classify path and must still turn; without that counterpart
  //    this assertion would hold for every stored value, including one the
  //    loader had folded back to Tap Only.
  const struct {
    uint8_t stored;
    bool turnsOnTap;
    const char* what;
  } storedGestures[] = {
      {CrossPointSettings::PAGE_TURN_GESTURE_DISABLED, false, "a stored 4 stops the tap turn"},
      {CrossPointSettings::INVERTED_TAP, true, "a stored 3 still turns on tap"},
  };
  for (const auto& g : storedGestures) {
    reloadFromDocument(g.stored, g.stored);
    expect(SETTINGS.pageTurnGesture == g.stored, "the stored value survives the load", SETTINGS.pageTurnGesture,
           g.stored);
    GfxRenderer renderer;
    const Turn right = tapAt(renderer, 460, 400);
    const Turn left = tapAt(renderer, 20, 400);
    // Loading a stored 3 is the legacy Inverted Tap, whose migration swaps the
    // stock sides, so the physical side that means "forward" is read back from
    // the loaded zones rather than hard-coded.
    const bool rightIsNext = zoneShortAction(tapZoneAt(renderer, 460, 400)) == CrossPointSettings::TAP_ZONE_NEXT;
    const bool leftIsNext = zoneShortAction(tapZoneAt(renderer, 20, 400)) == CrossPointSettings::TAP_ZONE_NEXT;
    expect(rightIsNext || leftIsNext, "one side holds the forward slot", rightIsNext || leftIsNext, 1);
    const Turn& forward = rightIsNext ? right : left;
    const Turn& backward = rightIsNext ? left : right;
    std::printf("stored %d: forward tap next=%d, backward tap prev=%d (want %d)\n", static_cast<int>(g.stored),
                forward.next, backward.prev, g.turnsOnTap);
    expect(forward.next == g.turnsOnTap, g.what, forward.next, g.turnsOnTap);
    expect(backward.prev == g.turnsOnTap, g.what, backward.prev, g.turnsOnTap);
  }

  if (failures == 0) std::printf("touch page-turn settings path passed\n");
  return failures == 0 ? 0 : 1;
}
'''

def build(directory):
    harness = HARNESS
    (directory / 'SettingsListProduction.h').write_text(settings_list_source())
    (directory / 'TouchPageTurnProduction.h').write_text(touch_page_turn_source())
    (directory / 'check.cpp').write_text(harness)
    compiler = shlex.split(os.environ.get('CXX', 'c++'))
    subprocess.run(
        compiler
        + ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(directory), str(directory / 'check.cpp'),
           '-o', str(directory / 'check')],
        check=True,
    )
    subprocess.run([str(directory / 'check')], check=True)

def main():
    with tempfile.TemporaryDirectory(prefix='touch-page-turn-') as temp:
        build(Path(temp))
    print('Touch page-turn settings path passed')

if __name__ == '__main__':
    main()
