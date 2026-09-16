#include "BookStyleStore.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {

// Conservative upper bound for the reader font point size. The font system
// snaps a requested size to the nearest size available for the family, so any
// value outside the physically useful range is rejected here.
constexpr uint8_t kMaxFontPointSize = 64;
// Extra paragraph spacing is the 0..5 selectable multiplier (0=off).
constexpr uint8_t kMaxExtraParagraphSpacing = 5;
// Longest book path accepted from the JSON file. Book paths come from the
// reader's file browser and stay well under this; a pathological record cannot
// inflate the store's resident memory.
constexpr size_t kMaxBookPathLength = 256;

// Reads an integer key from the JSON object. Type-compatible but out-of-range
// values (e.g. fontPointSize:0 or lineSpacing:255) fall back to `fallback` so
// a hand-edited or forward-versioned file cannot push the reader into an
// invalid state.
template <typename T>
T boundedInteger(JsonObjectConst obj, const char* key, T fallback, int64_t minValue, int64_t maxValue) {
  const int64_t v = obj[key] | static_cast<int64_t>(fallback);
  if (v < minValue || v > maxValue) {
    return fallback;
  }
  return static_cast<T>(v);
}

void styleToJson(JsonObject obj, const BookStyle& style) {
  obj["fontFamily"] = style.fontFamily;
  obj["sdFontFamilyName"] = style.sdFontFamilyName;
  obj["fontPointSize"] = style.fontPointSize;
  obj["lineSpacing"] = style.lineSpacing;
  obj["paragraphAlignment"] = style.paragraphAlignment;
  obj["extraParagraphSpacing"] = style.extraParagraphSpacing;
  obj["fakeBold"] = style.fakeBold;
  obj["textAntiAliasing"] = style.textAntiAliasing;
  obj["readingGuideLineEnabled"] = style.readingGuideLineEnabled;
  obj["readingGuideLineStyle"] = style.readingGuideLineStyle;
  obj["readingGuideLineOffset"] = style.readingGuideLineOffset;
}

// Reads a BookStyle from a JSON object. Unknown or out-of-range values fall
// back to the defaults so a hand-edited or forward-versioned file cannot push
// the reader into an invalid state.
bool styleFromJson(JsonObjectConst obj, BookStyle& style) {
  style.fontFamily = boundedInteger<uint8_t>(obj, "fontFamily", CrossPointSettings::NOTOSANS, 0,
                                             CrossPointSettings::FONT_FAMILY_COUNT - 1);
  const char* sdFamily = obj["sdFontFamilyName"] | "";
  if (sdFamily && *sdFamily != '\0') {
    strncpy(style.sdFontFamilyName, sdFamily, sizeof(style.sdFontFamilyName) - 1);
    style.sdFontFamilyName[sizeof(style.sdFontFamilyName) - 1] = '\0';
  } else {
    style.sdFontFamilyName[0] = '\0';
  }
  style.fontPointSize =
      boundedInteger<uint8_t>(obj, "fontPointSize", CrossPointSettings::DEFAULT_FONT_POINT_SIZE, 1, kMaxFontPointSize);
  style.lineSpacing = boundedInteger<uint8_t>(obj, "lineSpacing", CrossPointSettings::NORMAL, 0,
                                              CrossPointSettings::LINE_COMPRESSION_COUNT - 1);
  style.paragraphAlignment = boundedInteger<uint8_t>(obj, "paragraphAlignment", CrossPointSettings::JUSTIFIED, 0,
                                                     CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT - 1);
  style.extraParagraphSpacing = boundedInteger<uint8_t>(obj, "extraParagraphSpacing", 0, 0, kMaxExtraParagraphSpacing);
  style.fakeBold = boundedInteger<uint8_t>(obj, "fakeBold", CrossPointSettings::SYNTHETIC_BOLD_STANDARD, 0,
                                           CrossPointSettings::SYNTHETIC_BOLD_COUNT - 1);
  style.textAntiAliasing = boundedInteger<uint8_t>(obj, "textAntiAliasing", 1, 0, 1);
  style.readingGuideLineEnabled = boundedInteger<uint8_t>(obj, "readingGuideLineEnabled", 0, 0, 1);
  style.readingGuideLineStyle =
      boundedInteger<uint8_t>(obj, "readingGuideLineStyle", static_cast<uint8_t>(readingGuideLine::Style::ShortDash), 0,
                              static_cast<uint8_t>(readingGuideLine::Style::Count) - 1);
  style.readingGuideLineOffset = boundedInteger<int8_t>(
      obj, "readingGuideLineOffset", CrossPointSettings::READING_GUIDE_LINE_OFFSET_DEFAULT,
      CrossPointSettings::READING_GUIDE_LINE_OFFSET_MIN, CrossPointSettings::READING_GUIDE_LINE_OFFSET_MAX);
  return true;
}

}  // namespace

void BookStyleStore::toJson(JsonDocument& doc) const {
  doc["formatVersion"] = kFormatVersion;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& entry : styles) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = entry.path;
    styleToJson(obj, entry.style);
  }
}

bool BookStyleStore::fromJson(JsonVariantConst doc) {
  const int version = doc["formatVersion"] | kFormatVersion;
  if (version != kFormatVersion) {
    LOG_ERR("BST", "Unsupported book-style format version %d (expected %d); ignoring file", version, kFormatVersion);
    return false;
  }

  styles.clear();
  styles.reserve(MAX_STYLED_BOOKS);

  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (styles.size() >= MAX_STYLED_BOOKS) break;
    const char* path = obj["path"] | "";
    if (!path || *path == '\0' || strnlen(path, kMaxBookPathLength + 1) > kMaxBookPathLength) continue;
    BookStyle style;
    if (!styleFromJson(obj, style)) continue;
    styles.push_back({path, style});
  }

  LOG_DBG("BST", "Book styles loaded (%d entries)", static_cast<int>(styles.size()));
  return true;
}

bool BookStyleStore::findStyle(const std::string& bookPath, BookStyle& out) const {
  const auto it =
      std::find_if(styles.begin(), styles.end(), [&](const BookStyleEntry& entry) { return entry.path == bookPath; });
  if (it == styles.end()) return false;
  out = it->style;
  return true;
}

void BookStyleStore::updateStyle(const std::string& bookPath, const BookStyle& style) {
  const bool entryKnown =
      std::any_of(styles.begin(), styles.end(), [&](const BookStyleEntry& entry) { return entry.path == bookPath; });
  if (entryKnown) {
    for (auto& entry : styles) {
      if (entry.path == bookPath) {
        entry.style = style;
        break;
      }
    }
  } else {
    if (styles.size() >= MAX_STYLED_BOOKS) {
      // Oldest first; the list is append-ordered so the front is the oldest.
      styles.erase(styles.begin());
    }
    styles.push_back({bookPath, style});
  }

  saveToFile();
}

void BookStyleStore::clear() {
  styles.clear();
  saveToFile();
}
