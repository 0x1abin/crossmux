#pragma once

#include <ArduinoJson.h>
#include <Epub/FirstLineIndent.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"

// Per-book reader typography snapshot. Persisted per book path so a book
// reopens with exactly the style (font family/size, line spacing, alignment,
// reading guide line, extra paragraph spacing, first-line indent, synthetic
// bold, anti-aliasing) it had when it was last closed. Books without their own
// entry keep the global settings from the settings screen untouched.
struct BookStyle {
  uint8_t fontFamily = CrossPointSettings::NOTOSANS;
  char sdFontFamilyName[32] = "";
  uint8_t fontPointSize = CrossPointSettings::DEFAULT_FONT_POINT_SIZE;
  uint8_t lineSpacing = CrossPointSettings::NORMAL;
  uint8_t paragraphAlignment = CrossPointSettings::JUSTIFIED;
  uint8_t extraParagraphSpacing = 0;
  uint8_t firstLineIndent = FirstLineIndent::Auto;
  uint8_t fakeBold = CrossPointSettings::SYNTHETIC_BOLD_STANDARD;
  uint8_t textAntiAliasing = 1;
  uint8_t readingGuideLineEnabled = 0;
  uint8_t readingGuideLineStyle = static_cast<uint8_t>(readingGuideLine::Style::ShortDash);
  int8_t readingGuideLineOffset = CrossPointSettings::READING_GUIDE_LINE_OFFSET_DEFAULT;
};

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
  static constexpr size_t MAX_STYLED_BOOKS = 64;
  static constexpr int kFormatVersion = 1;

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

  // Forget every remembered book style. Persists immediately and returns
  // whether the file write succeeded (the in-memory store is cleared either
  // way).
  bool clear();
};

#define BOOK_STYLES BookStyleStore::getInstance()
