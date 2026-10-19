#pragma once

#include <ArduinoJson.h>
#include <Epub/FirstLineIndent.h>
#include <PersistableStore.h>

#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"

// Per-book reader typography overrides. Unlike a full snapshot, only the
// fields whose bit is set in overrideMask are applied when the book opens;
// cleared fields keep the global settings from the settings screen. A book
// without its own entry (or with an empty mask) reads exactly like the stock
// firmware. The text-settings entry (opened from the reader) edits these
// fields per book; clearing a field's bit (or removing the entry) restores
// the global value.
struct BookStyle {
  // Bit i set = the matching field below is an explicit override for this
  // book; cleared = the field follows the global setting. kSdFontFamily
  // tracks sdFontFamilyName (non-empty value); when it is cleared the string
  // is ignored.
  enum Bit : uint32_t {
    kFontFamily = 1u << 0,
    kSdFontFamily = 1u << 1,
    kFontPointSize = 1u << 2,
    kLineSpacing = 1u << 3,
    kParagraphAlignment = 1u << 4,
    kExtraParagraphSpacing = 1u << 5,
    kFirstLineIndent = 1u << 6,
    kParagraphIndentSpaces = 1u << 7,
    kFakeBold = 1u << 8,
    kTextAntiAliasing = 1u << 9,
    kWordSpacing = 1u << 10,
    kCharacterSpacing = 1u << 11,
    kScreenMargin = 1u << 12,
    kEmbeddedStyle = 1u << 13,
    kFocusReading = 1u << 14,
    kHyphenation = 1u << 15,
    kGuideLine = 1u << 16,
    kGuideLineStyle = 1u << 17,
    kGuideLineOffset = 1u << 18,
  };
  uint32_t overrideMask = 0;
  uint8_t fontFamily = CrossPointSettings::NOTOSANS;
  char sdFontFamilyName[32] = "";
  uint8_t fontPointSize = CrossPointSettings::DEFAULT_FONT_POINT_SIZE;
  uint8_t lineSpacing = CrossPointSettings::NORMAL;
  uint8_t paragraphAlignment = CrossPointSettings::JUSTIFIED;
  uint8_t extraParagraphSpacing = 0;
  uint8_t firstLineIndent = FirstLineIndent::Auto;
  uint8_t paragraphIndentSpaces = 3;
  uint8_t fakeBold = CrossPointSettings::SYNTHETIC_BOLD_STANDARD;
  uint8_t textAntiAliasing = 1;
  uint8_t wordSpacing = 100;
  uint8_t characterSpacing = CrossPointSettings::CHARACTER_SPACING_OFFSET;
  uint8_t screenMargin = CrossPointSettings::SCREEN_MARGIN_MIN;
  uint8_t embeddedStyle = 1;
  uint8_t focusReadingEnabled = 0;
  uint8_t hyphenationEnabled = 0;
  uint8_t readingGuideLineEnabled = 0;
  uint8_t readingGuideLineStyle = static_cast<uint8_t>(readingGuideLine::Style::ShortDash);
  int8_t readingGuideLineOffset = CrossPointSettings::READING_GUIDE_LINE_OFFSET_DEFAULT;
};

// Copies every field of `style` into the in-memory global settings. Shared by
// the reader's enter/exit snapshot restore and by the text-settings book
// override entry (clear-overrides restores the entered global snapshot the same
// way). Callers persist SETTINGS themselves when the change must reach disk.
inline void applyStyleToSettings(const BookStyle& style) {
  SETTINGS.fontFamily = style.fontFamily;
  memcpy(SETTINGS.sdFontFamilyName, style.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName));
  SETTINGS.fontPointSize = style.fontPointSize;
  SETTINGS.lineSpacing = style.lineSpacing;
  SETTINGS.paragraphAlignment = style.paragraphAlignment;
  SETTINGS.extraParagraphSpacing = style.extraParagraphSpacing;
  SETTINGS.firstLineIndent = style.firstLineIndent;
  SETTINGS.paragraphIndentSpaces = style.paragraphIndentSpaces;
  SETTINGS.fakeBold = style.fakeBold;
  SETTINGS.textAntiAliasing = style.textAntiAliasing;
  SETTINGS.wordSpacing = style.wordSpacing;
  SETTINGS.characterSpacing = style.characterSpacing;
  SETTINGS.screenMargin = style.screenMargin;
  SETTINGS.embeddedStyle = style.embeddedStyle;
  SETTINGS.focusReadingEnabled = style.focusReadingEnabled;
  SETTINGS.hyphenationEnabled = style.hyphenationEnabled;
  SETTINGS.readingGuideLineEnabled = style.readingGuideLineEnabled;
  SETTINGS.readingGuideLineStyle = style.readingGuideLineStyle;
  SETTINGS.readingGuideLineOffset = style.readingGuideLineOffset;
}

// Applies only the fields a book explicitly overrides; cleared mask bits keep
// the global value currently in SETTINGS. Used when a book opens and when the
// text-settings entry re-applies a book's overrides after restoring the global
// snapshot.
inline void applyStyleOverrides(const BookStyle& style) {
  const uint32_t m = style.overrideMask;
  if (m & BookStyle::kFontFamily) SETTINGS.fontFamily = style.fontFamily;
  if (m & BookStyle::kSdFontFamily) {
    memcpy(SETTINGS.sdFontFamilyName, style.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName));
  }
  if (m & BookStyle::kFontPointSize) SETTINGS.fontPointSize = style.fontPointSize;
  if (m & BookStyle::kLineSpacing) SETTINGS.lineSpacing = style.lineSpacing;
  if (m & BookStyle::kParagraphAlignment) SETTINGS.paragraphAlignment = style.paragraphAlignment;
  if (m & BookStyle::kExtraParagraphSpacing) SETTINGS.extraParagraphSpacing = style.extraParagraphSpacing;
  if (m & BookStyle::kFirstLineIndent) SETTINGS.firstLineIndent = style.firstLineIndent;
  if (m & BookStyle::kParagraphIndentSpaces) SETTINGS.paragraphIndentSpaces = style.paragraphIndentSpaces;
  if (m & BookStyle::kFakeBold) SETTINGS.fakeBold = style.fakeBold;
  if (m & BookStyle::kTextAntiAliasing) SETTINGS.textAntiAliasing = style.textAntiAliasing;
  if (m & BookStyle::kWordSpacing) SETTINGS.wordSpacing = style.wordSpacing;
  if (m & BookStyle::kCharacterSpacing) SETTINGS.characterSpacing = style.characterSpacing;
  if (m & BookStyle::kScreenMargin) SETTINGS.screenMargin = style.screenMargin;
  if (m & BookStyle::kEmbeddedStyle) SETTINGS.embeddedStyle = style.embeddedStyle;
  if (m & BookStyle::kFocusReading) SETTINGS.focusReadingEnabled = style.focusReadingEnabled;
  if (m & BookStyle::kHyphenation) SETTINGS.hyphenationEnabled = style.hyphenationEnabled;
  if (m & BookStyle::kGuideLine) SETTINGS.readingGuideLineEnabled = style.readingGuideLineEnabled;
  if (m & BookStyle::kGuideLineStyle) SETTINGS.readingGuideLineStyle = style.readingGuideLineStyle;
  if (m & BookStyle::kGuideLineOffset) SETTINGS.readingGuideLineOffset = style.readingGuideLineOffset;
}

// Copies the typography fields currently in SETTINGS into a BookStyle. Used for
// the session baseline (the global values this book opened with) and, when
// per-book memory is toggled, for swapping the live values out for the write.
inline BookStyle captureStyleFromSettings() {
  BookStyle style;
  style.fontFamily = SETTINGS.fontFamily;
  memcpy(style.sdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(style.sdFontFamilyName));
  style.fontPointSize = SETTINGS.fontPointSize;
  style.lineSpacing = SETTINGS.lineSpacing;
  style.paragraphAlignment = SETTINGS.paragraphAlignment;
  style.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  style.firstLineIndent = SETTINGS.firstLineIndent;
  style.paragraphIndentSpaces = SETTINGS.paragraphIndentSpaces;
  style.fakeBold = SETTINGS.fakeBold;
  style.textAntiAliasing = SETTINGS.textAntiAliasing;
  style.wordSpacing = SETTINGS.wordSpacing;
  style.characterSpacing = SETTINGS.characterSpacing;
  style.screenMargin = SETTINGS.screenMargin;
  style.embeddedStyle = SETTINGS.embeddedStyle;
  style.focusReadingEnabled = SETTINGS.focusReadingEnabled;
  style.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  style.readingGuideLineEnabled = SETTINGS.readingGuideLineEnabled;
  style.readingGuideLineStyle = SETTINGS.readingGuideLineStyle;
  style.readingGuideLineOffset = SETTINGS.readingGuideLineOffset;
  return style;
}

// The one save path for "SETTINGS currently carries this book's typography but
// the settings file must keep the user's global values": write the entered
// global style, then put the live values back so the session keeps rendering
// with the book's. Every screen that edits SETTINGS while a book is open must
// save through here — a raw saveToFile() would write the book's overrides into
// the global settings file. `enteredGlobal` is null when no book session stands
// behind this screen (the text settings opened from the main settings menu):
// there is no book style to protect, so a plain save is right. The
// !SETTINGS.bookStyleMemory half of the test covers the opposite case —
// entering from the reader still takes a snapshot with memory off, but the live
// values are then already global and are what has to be saved.
inline void saveGlobalSettingsKeepingBookStyle(const BookStyle* enteredGlobal) {
  if (enteredGlobal == nullptr || !SETTINGS.bookStyleMemory) {
    SETTINGS.saveToFile();
    return;
  }
  const BookStyle liveStyle = captureStyleFromSettings();
  applyStyleToSettings(*enteredGlobal);
  SETTINGS.saveToFile();
  applyStyleToSettings(liveStyle);
}

// Compares the live typography in SETTINGS with the entered global snapshot.
// A field the user changed in the text settings differs from the snapshot and
// gets its bit set; a field reverted to the global value clears its bit ("follow
// global"). Fields already overridden for this book differ from the snapshot
// too, so existing overrides are preserved by the same comparison. Shared by the
// reader's exit capture and the text-settings memory toggle.
inline BookStyle diffStyleFromSettings(const BookStyle& globalSnapshot) {
  BookStyle overrides;
  uint32_t& m = overrides.overrideMask;
  if (SETTINGS.fontFamily != globalSnapshot.fontFamily) {
    m |= BookStyle::kFontFamily;
    overrides.fontFamily = SETTINGS.fontFamily;
  }
  if (strcmp(SETTINGS.sdFontFamilyName, globalSnapshot.sdFontFamilyName) != 0) {
    m |= BookStyle::kSdFontFamily;
    memcpy(overrides.sdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(overrides.sdFontFamilyName));
  }
  if (SETTINGS.fontPointSize != globalSnapshot.fontPointSize) {
    m |= BookStyle::kFontPointSize;
    overrides.fontPointSize = SETTINGS.fontPointSize;
  }
  if (SETTINGS.lineSpacing != globalSnapshot.lineSpacing) {
    m |= BookStyle::kLineSpacing;
    overrides.lineSpacing = SETTINGS.lineSpacing;
  }
  if (SETTINGS.paragraphAlignment != globalSnapshot.paragraphAlignment) {
    m |= BookStyle::kParagraphAlignment;
    overrides.paragraphAlignment = SETTINGS.paragraphAlignment;
  }
  if (SETTINGS.extraParagraphSpacing != globalSnapshot.extraParagraphSpacing) {
    m |= BookStyle::kExtraParagraphSpacing;
    overrides.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  }
  if (SETTINGS.firstLineIndent != globalSnapshot.firstLineIndent) {
    m |= BookStyle::kFirstLineIndent;
    overrides.firstLineIndent = SETTINGS.firstLineIndent;
  }
  if (SETTINGS.paragraphIndentSpaces != globalSnapshot.paragraphIndentSpaces) {
    m |= BookStyle::kParagraphIndentSpaces;
    overrides.paragraphIndentSpaces = SETTINGS.paragraphIndentSpaces;
  }
  if (SETTINGS.fakeBold != globalSnapshot.fakeBold) {
    m |= BookStyle::kFakeBold;
    overrides.fakeBold = SETTINGS.fakeBold;
  }
  if (SETTINGS.textAntiAliasing != globalSnapshot.textAntiAliasing) {
    m |= BookStyle::kTextAntiAliasing;
    overrides.textAntiAliasing = SETTINGS.textAntiAliasing;
  }
  if (SETTINGS.wordSpacing != globalSnapshot.wordSpacing) {
    m |= BookStyle::kWordSpacing;
    overrides.wordSpacing = SETTINGS.wordSpacing;
  }
  if (SETTINGS.characterSpacing != globalSnapshot.characterSpacing) {
    m |= BookStyle::kCharacterSpacing;
    overrides.characterSpacing = SETTINGS.characterSpacing;
  }
  if (SETTINGS.screenMargin != globalSnapshot.screenMargin) {
    m |= BookStyle::kScreenMargin;
    overrides.screenMargin = SETTINGS.screenMargin;
  }
  if (SETTINGS.embeddedStyle != globalSnapshot.embeddedStyle) {
    m |= BookStyle::kEmbeddedStyle;
    overrides.embeddedStyle = SETTINGS.embeddedStyle;
  }
  if (SETTINGS.focusReadingEnabled != globalSnapshot.focusReadingEnabled) {
    m |= BookStyle::kFocusReading;
    overrides.focusReadingEnabled = SETTINGS.focusReadingEnabled;
  }
  if (SETTINGS.hyphenationEnabled != globalSnapshot.hyphenationEnabled) {
    m |= BookStyle::kHyphenation;
    overrides.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  }
  if (SETTINGS.readingGuideLineEnabled != globalSnapshot.readingGuideLineEnabled) {
    m |= BookStyle::kGuideLine;
    overrides.readingGuideLineEnabled = SETTINGS.readingGuideLineEnabled;
  }
  if (SETTINGS.readingGuideLineStyle != globalSnapshot.readingGuideLineStyle) {
    m |= BookStyle::kGuideLineStyle;
    overrides.readingGuideLineStyle = SETTINGS.readingGuideLineStyle;
  }
  if (SETTINGS.readingGuideLineOffset != globalSnapshot.readingGuideLineOffset) {
    m |= BookStyle::kGuideLineOffset;
    overrides.readingGuideLineOffset = SETTINGS.readingGuideLineOffset;
  }
  return overrides;
}

// Persisted at /.crosspoint/book_styles.json. One entry per book path (path is
// the same identity the reader activities use everywhere else).
class BookStyleStore : public PersistableStore<BookStyleStore> {
 private:
  struct BookStyleEntry {
    std::string path;
    BookStyle style;
  };

  std::vector<BookStyleEntry> styles;
  // A removal whose write failed: the entry is gone from `styles` but the file
  // still holds it, so a retry must rewrite the file rather than report a
  // success it never had. Cleared as soon as a write succeeds.
  bool needsRemovalWrite_ = false;
  std::string pendingRemoval_;

  BookStyleStore() = default;
  ~BookStyleStore() = default;

  friend class PersistableStore<BookStyleStore>;

 public:
  // Per-target cap so the store stays lean on the one non-PSRAM build.
  // Every product target (X4/X4Pro/X4C/sticky/eego_a4/...) is an ESP32-S3 with
  // 8MB PSRAM; the C3 host build ([env:default], esp32-c3-devkitm-1) has no
  // PSRAM and a ~400KB heap, so it keeps 64 entries (the same figure the
  // upstream review asked to validate on C3). At ~250B/record on disk the S3
  // cap stays far below the SD write buffer, and the resident vector plus the
  // JSON parse peak stays within a few tens of KB on either target.
#if CONFIG_IDF_TARGET_ESP32C3
  static constexpr size_t MAX_STYLED_BOOKS = 64;
#else
  static constexpr size_t MAX_STYLED_BOOKS = 128;
#endif
  // v2: per-field override mask. The store was introduced by this PR; only
  // the current format version is loaded, a mismatch ignores the whole file.
  static constexpr int kFormatVersion = 2;

  static const char* getFilePath() { return "/.crosspoint/book_styles.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Style currently remembered for this book path. Returns false when the book
  // has no own entry (the reader then keeps the global settings).
  bool findStyle(const std::string& bookPath, BookStyle& out) const;

  // Remember this book's style as its own entry. Persists on change and
  // returns whether the file write succeeded; the in-memory record is kept in
  // either case so this session still uses it, but a false return means the
  // change will not survive a reboot (caller should log).
  bool updateStyle(const std::string& bookPath, const BookStyle& style);

  // Forget this book's entry (its overrides). No-op and returns true when the
  // book had no entry. Persists on change and returns whether the file write
  // succeeded.
  bool removeByPath(const std::string& bookPath);

  // Forget every remembered book style. Persists immediately and returns
  // whether the file write succeeded (the in-memory store is cleared either
  // way).
  bool clear();
};

#define BOOK_STYLES BookStyleStore::getInstance()

// Applies a BookStyleMemory switch flip to the live session. `enteredGlobal` is
// the session baseline and `wasOn` the state before the flip; the caller flips
// SETTINGS.bookStyleMemory and persists (saveGlobalSettingsKeepingBookStyle).
//   off: capture what the user has picked so far as the book's overrides (the
//        exit capture will not run any more), then put the global values back.
//   on:  SETTINGS now holds the user's global typography — possibly edited while
//        memory was off — so it becomes the new baseline, and the remembered
//        overrides are re-applied on top. Without the refresh a global edit made
//        while memory was off would be rolled back on exit, and without the
//        re-apply the exit capture would store "no overrides" and delete the
//        remembered entry.
inline void applyBookStyleMemoryToggle(const std::string& bookPath, BookStyle& enteredGlobal, const bool wasOn) {
  if (wasOn) {
    const BookStyle overrides = diffStyleFromSettings(enteredGlobal);
    if (overrides.overrideMask != 0) {
      BOOK_STYLES.updateStyle(bookPath, overrides);
    } else {
      BOOK_STYLES.removeByPath(bookPath);
    }
    applyStyleToSettings(enteredGlobal);
  } else {
    enteredGlobal = captureStyleFromSettings();
    BookStyle stored;
    if (BOOK_STYLES.findStyle(bookPath, stored)) applyStyleOverrides(stored);
  }
}
