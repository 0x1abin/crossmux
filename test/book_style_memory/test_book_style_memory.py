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
font_cpp = (ROOT / "src/activities/settings/FontDownloadActivity.cpp").read_text()
ts_cpp = (ROOT / "src/activities/settings/TextSettingsActivity.cpp").read_text()
epub_cpp = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text()


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


def installer_caller(text, signature, name):
    """Rebuild a screen's installer return callback as a named method.

    Both screens hand the installer a lambda that decides whether a committed
    family has to reach the session baseline. That lambda is what the review
    found wrong, so the test runs the production one rather than a copy: take
    the function, keep the lambda's body only, and expose it as a method the
    shell can call with the result a visit produced.
    """
    body = method(text, signature)
    call = body.index("startActivityForResult(")
    lambda_start = body.index("[", call)
    lambda_end = lambda_start
    depth = 0
    # Walk the lambda body braces so a nested if/else cannot end it early.
    paren = body.index(")", lambda_start)
    brace = body.index("{", paren)
    lambda_end = brace + 1
    depth = 1
    while depth:
        depth += (body[lambda_end] == "{") - (body[lambda_end] == "}")
        lambda_end += 1
    lambda_body = body[brace + 1 : lambda_end - 1]
    owner = "ReaderActivity::" if "EpubReaderActivity" in signature else "TextSettingsActivity::"
    return "void " + owner + name + "(const ActivityResult& result) {" + lambda_body + "}"


def confirm_caller(text, anchor, name):
    """Rebuild a popup's confirm callback as a named method.

    Same trick as installer_caller(): the decision lives in a lambda handed to
    optionPopup_.show(), so take that lambda's body and expose it as a method
    the shell can call with the chosen option index.
    """
    start = text.index(anchor)
    lambda_start = text.index("[this]", start)
    brace = text.index("{", lambda_start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return "void TextSettingsActivity::" + name + "(int idx) {" + text[brace + 1 : end - 1] + "}"


def case_caller(text, owner, anchor, end_anchor, name):
    """Rebuild a switch case as a named method.

    The memory row's decision is a `case` in confirmStyleRow(), so - like the
    lambdas above - its body is taken as written and exposed as a method the
    shell can call. `owner` pins which function the case belongs to: the row
    appears in several switches (the row painter and the key handler).
    """
    start = text.index(owner)
    start = text.index(anchor, start)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    assert end_anchor not in text[brace:end], "the extracted case ran past its own body"
    return "void TextSettingsActivity::" + name + "() {" + text[brace + 1 : end - 1] + "}"


def anonymous_helpers():
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
#include <functional>
#include <memory>
#include <string>
#include <variant>
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
  int persistedLineSpacing = -1;
  int persistedBookStyleMemory = -1;
  char persistedFamily[32] = "";
  bool saveToFile() {
    ++saves;
    if (!saveOk) return false;
    persistedFontPointSize = fontPointSize;
    persistedLineSpacing = lineSpacing;
    persistedBookStyleMemory = bookStyleMemory;
    memcpy(persistedFamily, sdFontFamilyName, sizeof(persistedFamily));
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

// --- font installer: the two production return paths the review found ------
// Reading stats pause/resume around a child activity; irrelevant here.
struct ReadingStatsStore {
  void resumeSession() {}
};
inline ReadingStatsStore READING_STATS;

// Both callers must refresh the session baseline only when a family was really
// committed. The flag rides on the installer's result, set at the moment it is
// saved, so a cancelled prompt or a failed download cannot reach the baseline.
struct MenuResult {
  int action = -1;
  uint8_t orientation = 0;
  uint8_t pageTurnRate = 0;
  bool settingsDirty = false;
  bool fontFamilyCommitted = false;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<std::monostate, MenuResult> data;
  ActivityResult() = default;
  ActivityResult(MenuResult menu) : data{menu} {}
};

struct FontDownloadActivity {
  const BookStyle* sessionGlobal_ = nullptr;
  const BookStyle* sessionLive_ = nullptr;
  bool fontFamilyCommitted_ = false;
  bool saveFontSelection();
};
struct ReaderActivity;

// TextSettingsActivity::maybeOfferCompleteChineseFont(): the production body is
// extracted below; this shell supplies the session context it captures.
struct TextSettingsActivity {
  ReaderActivity* reader_ = nullptr;
  const std::string* bookPath_ = nullptr;
  BookStyle* globalSnapshot_ = nullptr;
  BookStyle fontInstallLiveStyle_;
  int updates = 0;
  bool bookOverrideMode() const { return bookPath_ != nullptr && globalSnapshot_ != nullptr; }
  void requestUpdate() { ++updates; }
  template <typename T>
  void startActivityForResult(std::unique_ptr<T>&&, std::function<void(const ActivityResult&)> handler) {
    handler_ = std::move(handler);
  }
  void maybeOfferCompleteChineseFont();
  void onFontInstallerReturn(const ActivityResult& result);
  // The "clear this book's overrides" confirm callback and the screen's own
  // save path, both production bodies extracted below.
  void onClearBookStylesConfirm(int idx);
  // The "Per-book style memory" row: production body extracted below.
  void toggleBookStyleMemory();
  void saveSettings();
  std::function<void(const ActivityResult&)> handler_;
  // Simulates the installer's own exit: commit a family (or not) and deliver
  // whatever result that visit produced to this screen.
  void installThenReturn(const char* family);
  void cancelPrompt();
};

// --- production code under test ---------------------------------------------
struct ReaderActivity {
  std::string bookPath = kBook;
  bool globalSettingsSnapshotted_ = false;
  BookStyle globalSettingsSnapshot_;
  BookStyle fontInstallLiveStyle_;
  int updates = 0;
  bool autoInstallActive = false;

  void applyBookStyle();
  void saveBookStyle();
  BookStyle captureStyleDiff(const BookStyle&) const;
  bool persistBookStyleEdits();
  void saveSettingsKeepingBookStyle();
  void startActivityForResult(std::unique_ptr<FontDownloadActivity>&&,
                              std::function<void(const ActivityResult&)> handler) {
    handler_ = std::move(handler);
  }
  // The reader's own auto-install prompt returns through this handler.
  std::function<void(const ActivityResult&)> handler_;
  void openReaderAutoInstall();
  void requestUpdate() { ++updates; }
  void onReaderAutoInstallReturn(const ActivityResult& result);
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
    + "\n"
    # The installer's write is the same contract from a child screen.
    + method(font_cpp, "bool FontDownloadActivity::saveFontSelection")
    + "\n"
    # Both callers of the installer: the reader's auto-install prompt and the
    # text settings picker. Each refreshes the session baseline only for a
    # committed family, and each is extracted as written so the test fails if
    # that gate is dropped. Their lambdas are turned into named methods because
    # the surrounding activity shells are stubs.
    + installer_caller(epub_cpp, "bool EpubReaderActivity::maybeOfferCompleteChineseFont", "onReaderAutoInstallReturn")
    + "\n"
    + installer_caller(ts_cpp, "void TextSettingsActivity::maybeOfferCompleteChineseFont",
                       "onFontInstallerReturn")
    + r'''
// The shells' simulated installer visits: one that commits a family (and says
// so, exactly as the installer's result does) and one that walks away. Both
// call the production return callback the open path installs.
void TextSettingsActivity::installThenReturn(const char* family) {
  strncpy(SETTINGS.sdFontFamilyName, family, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  ActivityResult result;
  result.data = MenuResult{-1, 0, 0, false, true};
  onFontInstallerReturn(result);
}
void TextSettingsActivity::cancelPrompt() {
  ActivityResult result;
  result.isCancelled = true;
  onFontInstallerReturn(result);
}
'''
    # The ClearBookStyles row: its confirm callback decides whether the entered
    # global snapshot has to be put back, and saveSettings() is the write it
    # uses. Both are extracted as written so the test fails if the memory guard
    # is dropped from the callback.
    + method(ts_cpp, "void TextSettingsActivity::saveSettings")
    + "\n"
    + case_caller(ts_cpp, "void TextSettingsActivity::confirmStyleRow", "case StyleRow::BookStyleMemory:",
                  "case StyleRow::ClearBookStyles:", "toggleBookStyleMemory")
    + "\n"
    + confirm_caller(ts_cpp, "case StyleRow::ClearBookStyles:", "onClearBookStylesConfirm")
    + "\n"
    + r'''
// ORDER(5): entry -> snapshot -> persist -> switch-off -> exit/restart -> failure
int main() {
  // Unbuffered: the printf probes have to survive an abort() from a failed
  // assertion, which is what they are for.
  setvbuf(stdout, nullptr, _IONBF, 0);
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
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    // The baseline is always taken: enabling memory mid-session needs it.
    assert(reader.globalSettingsSnapshotted_);
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
    std::printf("re-enable: entries while off=1, after re-enable/result=%zu, live font=%u, disk font=%u\n",
                store.styles.size(), (unsigned)SETTINGS.fontPointSize, (unsigned)SETTINGS.persistedFontPointSize);
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
    std::printf("global edit while off: disk=%u (wanted 26), live=%u, override=%u\n",
                (unsigned)SETTINGS.persistedFontPointSize, (unsigned)SETTINGS.fontPointSize,
                (unsigned)store.styles[0].style.fontPointSize);
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
  // 13. Review repro (standalone Text Settings): the memory row toggled from
  //     global Settings has no book baseline, and must still persist. The
  //     toggle row saves in both contexts (TextSettingsActivity), so this
  //     asserts the contract, not the branch: a plain write when there is no
  //     baseline.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    ReaderActivity reader;                              // no book session
    SETTINGS.saves = 0;
    SETTINGS.bookStyleMemory = 0;
    saveGlobalSettingsKeepingBookStyle(nullptr);        // the toggle's save
    assert(SETTINGS.saves == 1);                        // wrote, did not skip
    SETTINGS.bookStyleMemory = 1;
    saveGlobalSettingsKeepingBookStyle(nullptr);
    assert(SETTINGS.saves == 2);
  }

  // 14. Review repro (book opened with memory OFF, enabled mid-session): the
  //     session must have a baseline, keep global 12 on disk, and remember the
  //     book's 22.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 0;
    SETTINGS.fontPointSize = 12;
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();                            // opened while off
    assert(reader.globalSettingsSnapshotted_);          // baseline exists

    SETTINGS.fontPointSize = 14;                        // global edit while off
    SETTINGS.bookStyleMemory = 1;                       // enabled from the menu
    // Exactly what the toggle row does: TextSettingsActivity flips the flag and
    // calls the shared helper with the reader session's baseline.
    applyBookStyleMemoryToggle(kBook, reader.globalSettingsSnapshot_, /*wasOn=*/false);
    assert(reader.globalSettingsSnapshot_.fontPointSize == 14);  // refreshed
    SETTINGS.fontPointSize = 22;                        // book edit
    assert(reader.persistBookStyleEdits());             // now book-scoped
    assert(store.styles.size() == 1);
    assert(store.styles[0].style.fontPointSize == 22);
    reader.saveBookStyle();
    assert(SETTINGS.persistedFontPointSize == 14);      // global edit kept
    assert(SETTINGS.fontPointSize == 14);               // session rolled back
    std::printf("book opened OFF then enabled: global=%u entries=%zu\n",
                (unsigned)SETTINGS.persistedFontPointSize, store.styles.size());
  }

  // 15. Opened with memory ON, switched OFF mid-session: the exit write must
  //     still put the baseline in the file, never the book's live values.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    reader.persistBookStyleEdits();
    SETTINGS.bookStyleMemory = 0;                       // switched off
    applyBookStyleMemoryToggle(kBook, reader.globalSettingsSnapshot_, /*wasOn=*/true);
    reader.saveBookStyle();
    assert(SETTINGS.persistedFontPointSize == 12);      // baseline, not 22
    assert(store.styles.size() == 1);                   // entry kept
    SETTINGS.bookStyleMemory = 1;
  }

  // 16. Font install (review repro): the book has its own typography, then the
  //     user installs a family while the book is open and normally exits.
  //     Installing a family is device-level: it must become global and stay
  //     global after the exit, while the book's other typography (line spacing
  //     here) stays out of the settings file.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.fontPointSize = 12;
    SETTINGS.lineSpacing = 1;
    memset(SETTINGS.sdFontFamilyName, 0, sizeof(SETTINGS.sdFontFamilyName));
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;                        // book override
    SETTINGS.lineSpacing = 2;                           // book override
    reader.persistBookStyleEdits();

    FontDownloadActivity installer;
    installer.sessionGlobal_ = &reader.globalSettingsSnapshot_;
    const BookStyle live = captureStyleFromSettings();
    installer.sessionLive_ = &live;
    strncpy(SETTINGS.sdFontFamilyName, "DownloadedFamily", sizeof(SETTINGS.sdFontFamilyName) - 1);
    assert(installer.saveFontSelection());
    assert(SETTINGS.persistedLineSpacing == 1);         // global kept
    assert(strcmp(SETTINGS.persistedFamily, "DownloadedFamily") == 0);  // family is global
    assert(SETTINGS.lineSpacing == 2);                  // session keeps the book's
    assert(SETTINGS.fontPointSize == 22);
    std::printf("font install: disk lineSpacing=%u (wanted 1), family=%s, live=%u\n",
                (unsigned)SETTINGS.persistedLineSpacing, SETTINGS.persistedFamily,
                (unsigned)SETTINGS.lineSpacing);

    // The installer's result handler then refreshes the session baseline with
    // the family it just made global; the production callback runs here, not a
    // copy. Without it the exit below restores the baseline's older family over
    // the install - the reviewer's repro.
    reader.onReaderAutoInstallReturn(ActivityResult{MenuResult{-1, 0, 0, false, true}});

    // The ordinary reader exit follows: the page-turn block above was the last
    // book edit, so run the ordinary post-edit capture first (it runs only while
    // the reader is still up, i.e. before onExit) and then exit.
    reader.persistBookStyleEdits();
    reader.saveBookStyle();
    assert(strcmp(SETTINGS.persistedFamily, "DownloadedFamily") == 0);
    assert(SETTINGS.persistedLineSpacing == 1);         // still no book line spacing
    std::printf("font install then exit: disk family=%s lineSpacing=%u\n",
                SETTINGS.persistedFamily, (unsigned)SETTINGS.persistedLineSpacing);
  }

  // 16b. Same book, same exit, but the family was chosen from the Text
  //      Settings picker: that screen opens the installer too, and its own
  //      callback is the one that has to carry the commit into the baseline.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.fontPointSize = 12;
    SETTINGS.lineSpacing = 1;
    memset(SETTINGS.sdFontFamilyName, 0, sizeof(SETTINGS.sdFontFamilyName));
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    SETTINGS.lineSpacing = 2;
    reader.persistBookStyleEdits();

    TextSettingsActivity picker;
    picker.bookPath_ = &reader.bookPath;
    picker.globalSnapshot_ = &reader.globalSettingsSnapshot_;
    assert(picker.bookOverrideMode());
    FontDownloadActivity installer;
    installer.sessionGlobal_ = picker.globalSnapshot_;
    const BookStyle live = captureStyleFromSettings();
    installer.sessionLive_ = &live;
    strncpy(SETTINGS.sdFontFamilyName, "DownloadedFamily", sizeof(SETTINGS.sdFontFamilyName) - 1);
    assert(installer.saveFontSelection());
    picker.globalSnapshot_ = &reader.globalSettingsSnapshot_;
    picker.installThenReturn("DownloadedFamily");

    reader.persistBookStyleEdits();
    reader.saveBookStyle();
    assert(strcmp(SETTINGS.persistedFamily, "DownloadedFamily") == 0);
    assert(SETTINGS.persistedLineSpacing == 1);
    std::printf("text-settings install then exit: disk family=%s lineSpacing=%u\n",
                SETTINGS.persistedFamily, (unsigned)SETTINGS.persistedLineSpacing);
  }

  // 16c. Cancelling the prompt commits nothing, so it must not touch the
  //      baseline: the book uses the built-in face (empty SD family) while the
  //      global family is GlobalFamily, and backing out of the prompt has to
  //      leave GlobalFamily global.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.fontPointSize = 12;
    SETTINGS.lineSpacing = 1;
    strncpy(SETTINGS.sdFontFamilyName, "GlobalFamily", sizeof(SETTINGS.sdFontFamilyName) - 1);
    ReaderActivity reader;
    reader.applyBookStyle();                            // baseline: GlobalFamily
    SETTINGS.fontPointSize = 22;
    memset(SETTINGS.sdFontFamilyName, 0, sizeof(SETTINGS.sdFontFamilyName));  // book: built-in
    reader.persistBookStyleEdits();

    reader.onReaderAutoInstallReturn(ActivityResult{});
    reader.persistBookStyleEdits();
    reader.saveBookStyle();
    assert(strcmp(SETTINGS.persistedFamily, "GlobalFamily") == 0);
    std::printf("cancelled prompt: disk family=%s (wanted GlobalFamily)\n", SETTINGS.persistedFamily);

    // The picker's cancelled prompt takes the same exit.
    ReaderActivity pickerReader;
    strncpy(SETTINGS.sdFontFamilyName, "GlobalFamily", sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.fontPointSize = 12;
    pickerReader.applyBookStyle();
    memset(SETTINGS.sdFontFamilyName, 0, sizeof(SETTINGS.sdFontFamilyName));
    pickerReader.persistBookStyleEdits();
    TextSettingsActivity picker;
    picker.bookPath_ = &pickerReader.bookPath;
    picker.globalSnapshot_ = &pickerReader.globalSettingsSnapshot_;
    picker.cancelPrompt();
    pickerReader.persistBookStyleEdits();
    pickerReader.saveBookStyle();
    assert(strcmp(SETTINGS.persistedFamily, "GlobalFamily") == 0);
    std::printf("cancelled picker prompt: disk family=%s (wanted GlobalFamily)\n", SETTINGS.persistedFamily);
  }

  // 17. Standalone installer (Settings, no book): a plain save.
  {
    store.reset();
    SETTINGS.saves = 0;
    FontDownloadActivity installer;
    assert(installer.saveFontSelection());
    assert(SETTINGS.saves == 1);
  }

  // 18. Clearing this book's overrides with memory OFF must not roll back a
  //     global edit made in this screen. applyBookStyle() snapshots the global
  //     settings on enter whether or not memory is on, so "a snapshot exists"
  //     cannot be the guard: restoring it here would undo the edit and write
  //     the stale value into the settings file.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 0;
    SETTINGS.fontPointSize = 12;
    SETTINGS.lineSpacing = 1;
    ReaderActivity reader;
    reader.applyBookStyle();                     // snapshot taken anyway
    assert(reader.globalSettingsSnapshotted_);   // the review's point
    TextSettingsActivity picker;
    picker.bookPath_ = &reader.bookPath;
    picker.globalSnapshot_ = &reader.globalSettingsSnapshot_;
    SETTINGS.fontPointSize = 26;                 // the user's global edit
    picker.saveSettings();
    assert(SETTINGS.persistedFontPointSize == 26);

    picker.onClearBookStylesConfirm(1);          // clear this book's overrides
    assert(SETTINGS.fontPointSize == 26);            // live value untouched
    assert(SETTINGS.persistedFontPointSize == 26);   // not rolled back to 12
    std::printf("clear overrides, memory off: disk font=%d (wanted 26)\n",
                SETTINGS.persistedFontPointSize);
  }

  // 19. Same row with memory ON: SETTINGS carries this book's overrides, so the
  //     entered global values do have to come back and reach the file.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.fontPointSize = 12;
    SETTINGS.lineSpacing = 1;
    ReaderActivity reader;
    reader.applyBookStyle();
    SETTINGS.fontPointSize = 26;
    assert(reader.persistBookStyleEdits());      // becomes the book's override
    BookStyle probe;
    assert(store.findStyle(kBook, probe));
    TextSettingsActivity picker;
    picker.bookPath_ = &reader.bookPath;
    picker.globalSnapshot_ = &reader.globalSettingsSnapshot_;
    picker.onClearBookStylesConfirm(1);
    assert(!store.findStyle(kBook, probe));      // the entry is gone
    assert(SETTINGS.fontPointSize == 12);        // back to the global value
    assert(SETTINGS.persistedFontPointSize == 12);
    std::printf("clear overrides, memory on: disk font=%d (wanted 12)\n",
                SETTINGS.persistedFontPointSize);
  }

  // 20. Review finding 1: the memory row toggled from the standalone Text
  //     Settings. There is no book session behind the screen, so
  //     bookOverrideMode() is false; the switch is still an ordinary setting and
  //     must reach the file on its own.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.persistedBookStyleMemory = 1;  // what the file says right now
    SETTINGS.saves = 0;
    TextSettingsActivity standalone;        // bookPath_/globalSnapshot_ null
    assert(!standalone.bookOverrideMode());
    standalone.toggleBookStyleMemory();     // the production row, as written
    assert(SETTINGS.bookStyleMemory == 0);
    assert(SETTINGS.saves == 1);            // wrote, did not skip
    assert(SETTINGS.persistedBookStyleMemory == 0);  // and the file changed
    standalone.toggleBookStyleMemory();     // and back on
    assert(SETTINGS.bookStyleMemory == 1);
    assert(SETTINGS.saves == 2);
    assert(SETTINGS.persistedBookStyleMemory == 1);
    std::printf("standalone toggle: writes=%d (wanted 2), disk bookStyleMemory=%d (wanted back to 1)\n", SETTINGS.saves,
                SETTINGS.persistedBookStyleMemory);
  }

  // 21. Review finding 2: a book opened with memory OFF, its font changed in
  //     the book, then memory enabled from the menu. The row is the production
  //     one, so `wasOn` and the flag flip are the real ones.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 0;
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();                          // opened while off
    assert(reader.globalSettingsSnapshotted_);        // baseline exists
    SETTINGS.fontPointSize = 22;                      // changed in the book
    reader.saveSettingsKeepingBookStyle();            // ... while memory is off
    // The edit is a global one: with memory off this is the stock behaviour, so
    // it is already in the file before the switch is even touched. That is what
    // the maintainer asked for in the earlier round ("with the feature disabled
    // throughout ... should retain 24/24"), and it is why enabling memory later
    // cannot turn this 22 into a book override: it would roll a persisted global
    // change back to 12 for every other book.
    assert(SETTINGS.persistedFontPointSize == 22);
    TextSettingsActivity picker;
    picker.bookPath_ = &reader.bookPath;
    picker.globalSnapshot_ = &reader.globalSettingsSnapshot_;
    picker.toggleBookStyleMemory();
    assert(reader.globalSettingsSnapshot_.fontPointSize == 22);  // refreshed
    const int snapshotted = reader.globalSettingsSnapshotted_ ? 1 : 0;
    // Memory is active for this session now: a later edit is book-scoped.
    SETTINGS.fontPointSize = 26;
    assert(reader.persistBookStyleEdits());
    assert(store.styles.size() == 1);
    assert(store.styles[0].style.fontPointSize == 26);
    reader.saveBookStyle();
    std::printf("book opened OFF then enabled: snapshot=%d disk=%u (wanted 22) entries=%zu value=%u\n", snapshotted,
                (unsigned)SETTINGS.persistedFontPointSize, store.styles.size(),
                (unsigned)store.styles[0].style.fontPointSize);
    assert(SETTINGS.persistedFontPointSize == 22);    // global edit survives
    SETTINGS.fontPointSize = 12;
  }
  // 21b. The same entry context, but the edit comes AFTER memory is enabled:
  //      then it is this book's, and the global file keeps 12.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 0;
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();                          // opened while off
    TextSettingsActivity picker;
    picker.bookPath_ = &reader.bookPath;
    picker.globalSnapshot_ = &reader.globalSettingsSnapshot_;
    picker.toggleBookStyleMemory();
    assert(SETTINGS.bookStyleMemory == 1);            // the row flipped it
    SETTINGS.fontPointSize = 22;                      // edit with memory on
    assert(reader.persistBookStyleEdits());
    reader.saveBookStyle();                           // close
    assert(SETTINGS.persistedFontPointSize == 12);    // global kept
    assert(store.styles.size() == 1);
    assert(store.styles[0].style.fontPointSize == 22);
    std::printf("book opened OFF, enabled, then edited: disk=%d (wanted 12) entries=%zu value=%u\n",
                (unsigned)SETTINGS.persistedFontPointSize, store.styles.size(),
                (unsigned)store.styles[0].style.fontPointSize);
  }
  // 21c. ... and with a style already remembered for this book: enabling memory
  //      has to bring that override back in front of the user.
  {
    store.reset();
    SETTINGS.bookStyleMemory = 1;
    SETTINGS.fontPointSize = 12;
    ReaderActivity remembered;
    remembered.applyBookStyle();
    SETTINGS.fontPointSize = 22;
    remembered.persistBookStyleEdits();               // book override 22
    assert(store.styles.size() == 1);
    remembered.saveBookStyle();

    SETTINGS.bookStyleMemory = 0;
    SETTINGS.fontPointSize = 12;
    ReaderActivity reader;
    reader.applyBookStyle();                          // opened while off: 12
    assert(SETTINGS.fontPointSize == 12);
    TextSettingsActivity picker;
    picker.bookPath_ = &reader.bookPath;
    picker.globalSnapshot_ = &reader.globalSettingsSnapshot_;
    picker.toggleBookStyleMemory();
    assert(SETTINGS.bookStyleMemory == 1);            // the row flipped it
    assert(SETTINGS.fontPointSize == 22);             // remembered override back
    std::printf("book opened OFF then enabled, stored style: live=%u entries=%zu, ", (unsigned)SETTINGS.fontPointSize,
                store.styles.size());
    reader.saveBookStyle();
    assert(SETTINGS.persistedFontPointSize == 12);    // global kept
    assert(store.styles.size() == 1);
    std::printf("disk=%d (wanted 12)\n", (unsigned)SETTINGS.persistedFontPointSize);
    SETTINGS.fontPointSize = 12;
  }

  std::puts("book style memory flow passed");
}
'''
)

with tempfile.TemporaryDirectory(prefix="book-style-memory-") as tmp:
    cpp = Path(tmp) / "flow.cpp"
    exe = Path(tmp) / "flow"
    cpp.write_text(harness)
    if os.environ.get("DUMP_HARNESS"):
        Path(os.environ["DUMP_HARNESS"]).write_text(harness)
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

# The font installer has to work with no book open, so it owns one plain-save
# branch, but every write in it must go through saveFontSelection(), which keeps
# the book's typography out of the file while a reader session is open.
installer = (ROOT / "src/activities/settings/FontDownloadActivity.cpp").read_text()
body = installer[installer.index("bool FontDownloadActivity::saveFontSelection"):]
body = body[:body.index("\n}\n")]
assert installer.count("SETTINGS.saveToFile()") == body.count("SETTINGS.saveToFile()"), (
    "FontDownloadActivity.cpp writes SETTINGS outside saveFontSelection()")

# The memory row has to persist in BOTH entry contexts. Opened from global
# Settings there is no book baseline, so bookOverrideMode() is false and only a
# save outside that branch keeps the toggle across a sleep or power loss; the
# helper-based case above cannot see this because it calls the helper directly.
settings_cpp = (ROOT / "src/activities/settings/TextSettingsActivity.cpp").read_text()
memory_case = settings_cpp[settings_cpp.index("case StyleRow::BookStyleMemory:") :]
memory_case = memory_case[: memory_case.index("case StyleRow::ClearBookStyles:")]
toggle_body = memory_case[memory_case.index("const bool wasOn") :]
# Strip the branch so what remains is the unconditional tail.
outside_branch = toggle_body
if "if (bookOverrideMode()) {" in outside_branch:
    head, rest = outside_branch.split("if (bookOverrideMode()) {", 1)
    depth = 1
    index = 0
    while depth:
        depth += (rest[index] == "{") - (rest[index] == "}")
        index += 1
    outside_branch = head + rest[index:]
assert "saveSettings()" in outside_branch, (
    "the memory toggle saves only inside bookOverrideMode(), so global Settings never persists it")
assert "applyBookStyleMemoryToggle" in memory_case, "the book transition is no longer applied"
print("Book style memory flow passed")
