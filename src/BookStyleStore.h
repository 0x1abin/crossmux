#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

#include "CrossPointSettings.h"

// Per-book reader typography snapshot. Persisted per book path so a book
// reopens with exactly the style (font family/size, line spacing, alignment,
// extra paragraph spacing, synthetic bold, anti-aliasing) it had when it was
// last closed. Books without their own entry keep the global settings from the
// settings screen untouched.
struct BookStyle {
  uint8_t fontFamily = CrossPointSettings::NOTOSANS;
  char sdFontFamilyName[32] = "";
  uint8_t fontPointSize = CrossPointSettings::DEFAULT_FONT_POINT_SIZE;
  uint8_t lineSpacing = CrossPointSettings::NORMAL;
  uint8_t paragraphAlignment = CrossPointSettings::JUSTIFIED;
  uint8_t extraParagraphSpacing = 0;
  uint8_t fakeBold = CrossPointSettings::SYNTHETIC_BOLD_STANDARD;
  uint8_t textAntiAliasing = 1;
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

  static const char* getFilePath() { return "/.crosspoint/book_styles.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Style currently remembered for this book path. Returns false when the book
  // has no own entry (the reader then keeps the global settings).
  bool findStyle(const std::string& bookPath, BookStyle& out) const;

  // Remember this book's style as its own entry. Persists on change.
  void updateStyle(const std::string& bookPath, const BookStyle& style);
};

#define BOOK_STYLES BookStyleStore::getInstance()
