"""Per-book typography memory: the save/restore flow both menus share.

Compiles the production ReaderActivity::persistBookStyleEdits() /
captureStyleDiff() / saveSettingsKeepingBookStyle() and the production
BookStyleStore::updateStyle()/removeByPath() against host stubs, so the
assertions run the real code instead of a re-implementation.

Covers what the review asked for:
  * both text-settings entry points (reader menu + reader toolbar) end up in the
    same persistBookStyleEdits() flow,
  * turning the switch off mid-session,
  * exit / restart (a fresh load of the store file),
  * a failing SD write, including the retry.
The previous font-exit test stubbed bookStyleMemory as absent, which is why
none of the per-book paths were ever exercised: here memory defaults to on.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
reader_cpp = (ROOT / "src/activities/reader/ReaderActivity.cpp").read_text()
store_cpp = (ROOT / "src/BookStyleStore.cpp").read_text()
store_h = (ROOT / "src/BookStyleStore.h").read_text()


def method(text, signature):
    """Extract a function body by its `Class::name(` signature."""
    start = text.index(signature)
    start = text.rfind("\n", 0, start) + 1
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def anonymous_helpers():
    """The cap constants BookStyleStore.cpp keeps in its anonymous namespace.

    styleToJson/styleFromJson belong to toJson()/fromJson(), which this flow
    does not exercise, and boundedInteger() is only used by styleFromJson().
    """
    start = store_cpp.index("constexpr uint8_t kMaxFontPointSize")
    end = store_cpp.index("// Reads an integer key from the JSON object.", start)
    return store_cpp[start:end]


# The BookStyle struct + applyStyleToSettings/applyStyleOverrides come straight
# from the real header, minus the store class (stubbed below) and the
# CrossPointSettings include.
header = store_h[store_h.index("struct BookStyle {"):store_h.index("// Persisted at")]
header = header.replace("#include", "// #include")
# The memory-toggle helper lives after the store class in the real header (it
# calls BOOK_STYLES), so it is appended after the stubbed store below.
toggle_h = store_h[store_h.index("// Applies a BookStyleMemory switch flip"):]

harness = (
    r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// --- CrossPointSettings stub: only the fields the style code touches ---------
struct {
  uint8_t fontFamily = 0;
  char sdFontFamilyName[32] = "";
  uint8_t fontPointSize = 12;
  uint8_t lineSpacing = 1;
  uint8_t paragraphAlignment = 1;
  uint8_t extraParagraphSpacing = 0;
  uint8_t firstLineIndent = 0;
  uint8_t paragraphIndentSpaces = 3;
  uint8_t fakeBold = 1;
  uint8_t textAntiAliasing = 1;
  uint8_t wordSpacing = 100;
  uint8_t characterSpacing = 2;
  uint8_t screenMargin = 5;
  uint8_t embeddedStyle = 1;
  uint8_t focusReadingEnabled = 0;
  uint8_t hyphenationEnabled = 0;
  uint8_t readingGuideLineEnabled = 0;
  uint8_t readingGuideLineStyle = 1;
  int8_t readingGuideLineOffset = 0;
  uint8_t bookStyleMemory = 1;  // the review's point: defaults ON
  uint8_t screenInverted = 0;   // in-place edit from the reader menu
  uint8_t frontlightOn = 0;     // in-place edit from the reader menu
  int saves = 0;
  bool saveOk = true;
  int persistedFontPointSize = -1;  // what the last write put in the file
  bool saveToFile() {
    ++saves;
    if (!saveOk) return false;
    persistedFontPointSize = fontPointSize;
    return true;
  }
} SETTINGS;

struct CrossPointSettings {
  static constexpr uint8_t NOTOSANS = 0;
  static constexpr uint8_t FONT_FAMILY_COUNT = 8;
  static constexpr uint8_t DEFAULT_FONT_POINT_SIZE = 12;
  static constexpr uint8_t NORMAL = 1;
  static constexpr uint8_t LINE_COMPRESSION_COUNT = 4;
  static constexpr uint8_t JUSTIFIED = 1;
  static constexpr uint8_t PARAGRAPH_ALIGNMENT_COUNT = 3;
  static constexpr uint8_t SYNTHETIC_BOLD_STANDARD = 1;
  static constexpr uint8_t SYNTHETIC_BOLD_COUNT = 3;
  static constexpr uint8_t WORD_SPACING_MIN = 80;
  static constexpr uint8_t WORD_SPACING_MAX = 120;
  static constexpr uint8_t CHARACTER_SPACING_OFFSET = 2;
  static constexpr uint8_t SCREEN_MARGIN_MIN = 5;
  static constexpr uint8_t SCREEN_MARGIN_MAX = 30;
  static constexpr int8_t READING_GUIDE_LINE_OFFSET_DEFAULT = 0;
  static constexpr int8_t READING_GUIDE_LINE_OFFSET_MIN = -10;
  static constexpr int8_t READING_GUIDE_LINE_OFFSET_MAX = 10;
};
namespace FirstLineIndent {
constexpr uint8_t Auto = 0;
constexpr uint8_t NoIndent = 2;
}
namespace readingGuideLine {
enum class Style : uint8_t { ShortDash = 0, Count = 3 };
}

// --- logging ----------------------------------------------------------------
int loggedErrors = 0;
#define LOG_ERR(tag, fmt, ...) (++loggedErrors)
#define LOG_DBG(tag, fmt, ...) ((void)0)

#define SETTINGS_ALIAS 1
#define CONFIG_IDF_TARGET_ESP32C3 0

// --- ArduinoJson stub: just the object/array navigation the store uses -------
struct JsonObject;
struct JsonArray;
struct JsonVariant {
  std::string s;
  operator std::string() const { return s; }
  operator const char*() const { return s.c_str(); }
  template <typename T>
  operator T() const { return T(); }
};
struct JsonObjectConst {
  template <typename T>
  JsonVariant operator[](const char*) const { return JsonVariant{}; }
};
struct JsonObject {
  template <typename T>
  void operator[](const char*) {}
};
struct JsonArrayConst {
  const JsonObjectConst* begin() const { return nullptr; }
  const JsonObjectConst* end() const { return nullptr; }
};
struct JsonArray {
  JsonObject add() { return JsonObject{}; }
};
struct JsonVariantConst {
  JsonVariantConst operator[](const char*) const { return *this; }
  template <typename T>
  T as() const { return T(); }
  template <typename T>
  JsonVariantConst operator|(const T&) const { return *this; }
};
struct JsonDocument : JsonVariantConst {
  JsonArray to() { return JsonArray{}; }
};
'''
    + header
    + r'''
// --- the store, with the file layer replaced by a failure-injecting stub -----
struct BookStyleStore {
  struct BookStyleEntry {
    std::string path;
    BookStyle style;
  };
  std::vector<BookStyleEntry> styles;
  bool needsRemovalWrite_ = false;
  std::string pendingRemoval_;
  static constexpr size_t MAX_STYLED_BOOKS = 128;
  static constexpr int kFormatVersion = 2;
  bool diskOk = true;      // the injected SD write result
  int writes = 0;
  std::string diskContents;  // last successfully written "path\n" list

  static BookStyleStore& getInstance() { static BookStyleStore s; return s; }
  bool findStyle(const std::string& bookPath, BookStyle& out) const {
    for (const auto& e : styles) {
      if (e.path == bookPath) { out = e.style; return true; }
    }
    return false;
  }
  void reset() { styles.clear(); needsRemovalWrite_ = false; pendingRemoval_.clear(); diskOk = true; writes = 0; diskContents.clear(); }
  bool saveToFile();
  bool updateStyle(const std::string& bookPath, const BookStyle& style);
  bool removeByPath(const std::string& bookPath);
};
#define BOOK_STYLES BookStyleStore::getInstance()
'''
    + toggle_h
    + r'''
// A fresh "boot": load the store from what the last successful write left.
struct RebootedStore {
  std::vector<std::string> paths;
  explicit RebootedStore(const std::string& disk) {
    std::string line;
    for (const char c : disk) {
      if (c == '\n') { if (!line.empty()) paths.push_back(line); line.clear(); }
      else line += c;
    }
    if (!line.empty()) paths.push_back(line);
  }
  bool has(const std::string& p) const {
    for (const auto& x : paths) if (x == p) return true;
    return false;
  }
};

constexpr const char* kBook = "/books/one.epub";

// --- production code under test ---------------------------------------------
struct ReaderActivity {
  std::string bookPath = kBook;
  bool globalSettingsSnapshotted_ = false;
  BookStyle globalSettingsSnapshot_;
  void applyBookStyle();
  void saveBookStyle();
  BookStyle captureStyleDiff(const BookStyle&) const;
  bool persistBookStyleEdits();
  void saveSettingsKeepingBookStyle();
};
'''
    + method(reader_cpp, "void ReaderActivity::applyBookStyle")
    + "\n"
    + method(reader_cpp, "void ReaderActivity::saveBookStyle")
    + "\n"
    + method(reader_cpp, "BookStyle ReaderActivity::captureStyleDiff")
    + "\n"
    + method(reader_cpp, "bool ReaderActivity::persistBookStyleEdits")
    + "\n"
    + method(reader_cpp, "void ReaderActivity::saveSettingsKeepingBookStyle")
    + "\n"
    + anonymous_helpers()
    + r'''
bool BookStyleStore::saveToFile() {
  ++writes;
  if (!diskOk) return false;
  diskContents.clear();
  for (const auto& e : styles) diskContents += e.path + "\n";
  return true;
}
'''
    + method(store_cpp, "bool BookStyleStore::updateStyle")
    + "\n"
    + method(store_cpp, "bool BookStyleStore::removeByPath")
    + r'''
// ORDER(5): entry -> snapshot -> persist -> switch-off -> exit/restart -> failure
int main() {
  BookStyleStore& store = BOOK_STYLES;

  // 1. Opening a book with memory on takes the global snapshot.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    assert(reader.globalSettingsSnapshotted_);          // default ON snapshot
    assert(SETTINGS.fontPointSize == 12);               // no entry -> global
  }

  // 2. Edit + persist: the edit becomes the book's override and the global
  //    snapshot is what reaches the settings file.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;                        // the user picks 22pt
    assert(reader.persistBookStyleEdits());
    assert(SETTINGS.fontPointSize == 22);               // still laid out at 22pt
    assert(store.styles.size() == 1);                   // recorded for this book
    assert(store.styles[0].path == kBook);
    assert(store.styles[0].style.overrideMask == BookStyle::kFontPointSize);
  }

  // 3. Any other SETTINGS save in the reader path (night mode, frontlight,
  //    orientation, image scaling, font preload) must write the GLOBAL value
  //    while the session keeps rendering with the book's value.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    reader.persistBookStyleEdits();
    reader.saveSettingsKeepingBookStyle();
    assert(SETTINGS.persistedFontPointSize == 12);      // file got the global
    assert(SETTINGS.fontPointSize == 22);               // session keeps the book's
  }
  //    ... and is a plain save with memory off or without a snapshot.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.saveSettingsKeepingBookStyle();
    assert(SETTINGS.persistedFontPointSize == 12);
    assert(SETTINGS.fontPointSize == 12);
  }

  // 4. Switch off mid-session stops capturing; the edit stays as the global.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    SETTINGS.bookStyleMemory = 1;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    reader.persistBookStyleEdits();
    SETTINGS.bookStyleMemory = 0;                       // toggled off
    assert(!reader.persistBookStyleEdits());            // no capture from now on
    assert(SETTINGS.fontPointSize == 22);
    SETTINGS.fontPointSize = 26;                        // later edits are global
    reader.saveSettingsKeepingBookStyle();
    assert(SETTINGS.persistedFontPointSize == 26);      // and they reach the file
    assert(SETTINGS.fontPointSize == 26);
    SETTINGS.bookStyleMemory = 1;
  }

  // 5. Exit / restart: the override survives a fresh store load, and the global
  //    settings file only ever held the snapshot.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    reader.persistBookStyleEdits();
    RebootedStore rebooted(store.diskContents);
    assert(rebooted.has(kBook));
    // A restart re-reads the global settings file, i.e. the snapshot value.
    SETTINGS.fontPointSize = 12;
    assert(SETTINGS.fontPointSize == 12);
  }

  // 6. Reverting every field drops the entry again.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    reader.persistBookStyleEdits();
    assert(store.styles.size() == 1);
    SETTINGS.fontPointSize = 12;                        // user puts it back
    reader.persistBookStyleEdits();
    assert(store.styles.empty());
  }

  // 7. A failing write is reported, and the removal retry actually retries.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    store.diskOk = false;
    reader.persistBookStyleEdits();
    assert(store.styles.size() == 1);                   // kept in memory
    RebootedStore rebooted(store.diskContents);
    assert(!rebooted.has(kBook));                       // never reached the SD
    const int errors = loggedErrors;

    store.diskOk = true;                                // SD is back
    assert(store.removeByPath(kBook));                 // removal retried
    assert(store.styles.empty());
    RebootedStore after(store.diskContents);
    assert(!after.has(kBook));
    assert(loggedErrors >= errors);
  }

  // 8. updateStyle() clears a stale pending removal for the same path.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    store.diskOk = false;
    reader.persistBookStyleEdits();
    assert(store.styles.size() == 1);
    // Removal fails first, leaving a pending removal for this path.
    assert(!store.removeByPath(kBook));
    assert(store.needsRemovalWrite_);
    store.diskOk = true;
    SETTINGS.fontPointSize = 24;
    reader.persistBookStyleEdits();                     // re-adds the same path
    assert(!store.needsRemovalWrite_);
    assert(store.pendingRemoval_.empty());
    RebootedStore rebooted(store.diskContents);
    assert(rebooted.has(kBook));
  }
  // 9. Exit: with memory on, exit restores the snapshot into the file and
  //    leaves the session's global baseline in memory. With memory off (no
  //    snapshot) it still persists, so an in-place edit made in a child screen
  //    survives a sleep straight out of that screen.
  {
    store.reset();
    SETTINGS.fontPointSize = 12;
    SETTINGS.bookStyleMemory = 1;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    reader.persistBookStyleEdits();
    SETTINGS.screenInverted = 1;                        // night mode, in place
    reader.saveBookStyle();
    assert(SETTINGS.persistedFontPointSize == 12);      // file kept the global
    assert(SETTINGS.fontPointSize == 12);               // and memory rolled back
    assert(!reader.globalSettingsSnapshotted_);
    assert(!store.styles.empty());                      // override still stored
  }
  {
    store.reset();
    SETTINGS.bookStyleMemory = 0;                       // memory off throughout
    ReaderActivity reader;
    reader.applyBookStyle();
    assert(!reader.globalSettingsSnapshotted_);
    SETTINGS.fontPointSize = 20;                        // edited as a global
    reader.saveBookStyle();
    assert(SETTINGS.persistedFontPointSize == 20);      // persisted, no rollback
    assert(SETTINGS.fontPointSize == 20);
    SETTINGS.bookStyleMemory = 1;
  }
  // 10. Review repro: on -> off -> on in one session keeps the remembered
  //     override and puts it back in front of the user.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();                            // baseline 12, no entry
    SETTINGS.fontPointSize = 22;                        // picks 22pt for the book
    assert(reader.persistBookStyleEdits());             // stored as an override
    assert(store.styles.size() == 1);

    SETTINGS.bookStyleMemory = 0;                       // switch off
    applyBookStyleMemoryToggle(kBook, reader.globalSettingsSnapshot_, /*wasOn=*/true);
    reader.saveSettingsKeepingBookStyle();
    assert(SETTINGS.fontPointSize == 12);               // live follows global
    assert(SETTINGS.persistedFontPointSize == 12);
    assert(store.styles.size() == 1);                   // entry kept while off

    SETTINGS.bookStyleMemory = 1;                       // switch back on
    applyBookStyleMemoryToggle(kBook, reader.globalSettingsSnapshot_, /*wasOn=*/false);
    reader.saveSettingsKeepingBookStyle();
    assert(SETTINGS.fontPointSize == 22);               // remembered 22pt is back
    assert(SETTINGS.persistedFontPointSize == 12);      // file still global
    assert(reader.persistBookStyleEdits());             // exit capture
    assert(store.styles.size() == 1);                   // NOT deleted by the capture
    assert(store.styles[0].style.overrideMask == BookStyle::kFontPointSize);
    assert(store.styles[0].style.fontPointSize == 22);
  }

  // 11. Review repro: a global edit made while memory was off must not be
  //     rolled back when memory comes back on and the book is left.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();                            // baseline 12
    SETTINGS.fontPointSize = 22;
    reader.persistBookStyleEdits();                     // book override 22

    SETTINGS.bookStyleMemory = 0;                       // switch off
    applyBookStyleMemoryToggle(kBook, reader.globalSettingsSnapshot_, /*wasOn=*/true);
    reader.saveSettingsKeepingBookStyle();
    SETTINGS.fontPointSize = 26;                        // deliberate global edit
    reader.saveSettingsKeepingBookStyle();
    assert(SETTINGS.persistedFontPointSize == 26);

    SETTINGS.bookStyleMemory = 1;                       // switch back on
    applyBookStyleMemoryToggle(kBook, reader.globalSettingsSnapshot_, /*wasOn=*/false);
    reader.saveSettingsKeepingBookStyle();
    assert(SETTINGS.fontPointSize == 22);               // book override re-applied
    reader.persistBookStyleEdits();                     // leaving the reader
    reader.saveBookStyle();
    assert(SETTINGS.persistedFontPointSize == 26);      // global edit survives
    assert(store.styles.size() == 1);
    assert(store.styles[0].style.fontPointSize == 22);  // override still 22
    SETTINGS.fontPointSize = 26;
  }

  // 12. The memory toggle with no edits made: switch-off must not delete an
  //     existing entry, and switch-on with nothing stored applies nothing.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    reader.persistBookStyleEdits();
    SETTINGS.fontPointSize = 12;                        // user reverts in-session
    SETTINGS.bookStyleMemory = 0;
    applyBookStyleMemoryToggle(kBook, reader.globalSettingsSnapshot_, /*wasOn=*/true);
    assert(store.styles.empty());                       // reverted -> no entry
    SETTINGS.bookStyleMemory = 1;
    applyBookStyleMemoryToggle(kBook, reader.globalSettingsSnapshot_, /*wasOn=*/false);
    assert(SETTINGS.fontPointSize == 12);               // nothing to re-apply
  }
  std::puts("book style memory flow passed");
}
'''
)

with tempfile.TemporaryDirectory(prefix="book-style-memory-") as tmp:
    cpp = Path(tmp) / "flow.cpp"
    exe = Path(tmp) / "flow"
    cpp.write_text(harness)
    subprocess.run(
        shlex.split(os.environ.get("CXX", "c++"))
        + ["-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)

# Guard for the review finding this test missed the first time: a child screen
# that edits SETTINGS while a book is open must not write the file itself.
# ReaderActivity keeps its own two writes (the exit restore and the no-snapshot
# save); both put the global values in SETTINGS first by construction.
for rel in (
    "src/activities/settings/TextSettingsActivity.cpp",
    "src/activities/reader/EpubReaderActivity.cpp",
    "src/activities/reader/EpubReaderMenuActivity.cpp",
    "src/activities/reader/XtcReaderActivity.cpp",
):
    text = (ROOT / rel).read_text()
    assert "SETTINGS.saveToFile()" not in text, f"{rel} bypasses the book-style save path"
print("Book style memory flow passed")
