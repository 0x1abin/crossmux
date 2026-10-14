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

// Persisted at /.crosspoint/book_styles.json. One entry per book path (path is
// the same identity the reader activities use everywhere else).
class BookStyleStore : public PersistableStore<BookStyleStore> {
 private:
  struct BookStyleEntry {
    std::string path;
    BookStyle style;
  };

  std::vector<BookStyleEntry> styles;

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
