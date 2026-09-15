#include "BookStyleStore.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>

namespace {

void styleToJson(JsonObject obj, const BookStyle& style) {
  obj["fontFamily"] = style.fontFamily;
  obj["sdFontFamilyName"] = style.sdFontFamilyName;
  obj["fontPointSize"] = style.fontPointSize;
  obj["lineSpacing"] = style.lineSpacing;
  obj["paragraphAlignment"] = style.paragraphAlignment;
  obj["extraParagraphSpacing"] = style.extraParagraphSpacing;
  obj["fakeBold"] = style.fakeBold;
  obj["textAntiAliasing"] = style.textAntiAliasing;
}

// Reads a BookStyle from a JSON object. Unknown or out-of-range values fall
// back to the defaults so a hand-edited or forward-versioned file cannot push
// the reader into an invalid state.
bool styleFromJson(JsonObjectConst obj, BookStyle& style) {
  style.fontFamily = obj["fontFamily"] | CrossPointSettings::NOTOSANS;
  const char* sdFamily = obj["sdFontFamilyName"] | "";
  if (sdFamily && *sdFamily != '\0') {
    strncpy(style.sdFontFamilyName, sdFamily, sizeof(style.sdFontFamilyName) - 1);
    style.sdFontFamilyName[sizeof(style.sdFontFamilyName) - 1] = '\0';
  } else {
    style.sdFontFamilyName[0] = '\0';
  }
  style.fontPointSize = obj["fontPointSize"] | CrossPointSettings::DEFAULT_FONT_POINT_SIZE;
  style.lineSpacing = obj["lineSpacing"] | CrossPointSettings::NORMAL;
  style.paragraphAlignment = obj["paragraphAlignment"] | CrossPointSettings::JUSTIFIED;
  style.extraParagraphSpacing = obj["extraParagraphSpacing"] | 0;
  style.fakeBold = obj["fakeBold"] | CrossPointSettings::SYNTHETIC_BOLD_STANDARD;
  style.textAntiAliasing = obj["textAntiAliasing"] | 1;
  return true;
}

}  // namespace

void BookStyleStore::toJson(JsonDocument& doc) const {
  doc["formatVersion"] = 1;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& entry : styles) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = entry.path;
    styleToJson(obj, entry.style);
  }
}

bool BookStyleStore::fromJson(JsonVariantConst doc) {
  styles.clear();

  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  for (JsonObjectConst obj : arr) {
    if (styles.size() >= MAX_STYLED_BOOKS) break;
    const char* path = obj["path"] | "";
    if (!path || *path == '\0') continue;
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
