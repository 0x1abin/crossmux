#include "ReleaseJsonParser.h"

#include <cstdlib>
#include <cstring>

namespace {

constexpr char OTA_NOTES_FORBIDDEN[] = "`*_#<>[]{}\\";

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool validReleaseNote(const char* value, const size_t len) {
  if (len == 0 || len >= ReleaseJsonParser::RELEASE_NOTE_SIZE || value[0] == ' ' || value[len - 1] == ' ') {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const auto c = static_cast<unsigned char>(value[i]);
    if (c < 0x20 || c == 0x7f || strchr(OTA_NOTES_FORBIDDEN, c) != nullptr) return false;
  }
  return true;
}

}  // namespace

ReleaseJsonParser::ReleaseJsonParser(const std::span<ReleaseNote> releaseNotes)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd, nullptr}),
      releaseNotes(releaseNotes) {
  reset();
}

void ReleaseJsonParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  assetDepth = 0;
  releaseNotesDepth = 0;
  tagName[0] = '\0';
  firmwareUrl[0] = '\0';
  firmwareSize = 0;
  tagFound = false;
  firmwareFound = false;
  unsupportedChannelFound = false;
  releaseNotesInvalid = false;
  clearReleaseNotes();
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

void ReleaseJsonParser::feed(const char* data, size_t len) { parser.feed(data, len); }

bool ReleaseJsonParser::foundTag() const { return tagFound; }
bool ReleaseJsonParser::foundFirmware() const { return firmwareFound; }
bool ReleaseJsonParser::foundUnsupportedChannel() const { return unsupportedChannelFound; }
bool ReleaseJsonParser::foundReleaseNotes() const { return releaseNotesFound; }
const char* ReleaseJsonParser::getTagName() const { return tagName; }
const char* ReleaseJsonParser::getFirmwareUrl() const { return firmwareUrl; }
size_t ReleaseJsonParser::getReleaseNoteCount() const { return releaseNotesFound ? releaseNoteCount : 0; }
size_t ReleaseJsonParser::getFirmwareSize() const { return firmwareSize; }

void ReleaseJsonParser::commitAsset() {
  if (strcmp(currentAssetName, "firmware.bin") == 0) {
    memcpy(firmwareUrl, currentAssetUrl, sizeof(firmwareUrl));
    firmwareSize = currentAssetSize;
    firmwareFound = true;
  }
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSize = 0;
}

void ReleaseJsonParser::clearReleaseNotes() {
  releaseNotesFound = false;
  releaseNoteCount = 0;
  for (auto& note : releaseNotes) note[0] = '\0';
}

void ReleaseJsonParser::commitReleaseNote(const char* value, const size_t len) {
  if (releaseNotesInvalid) return;
  if (!validReleaseNote(value, len) || releaseNoteCount >= releaseNotes.size()) {
    releaseNotesInvalid = true;
    clearReleaseNotes();
    return;
  }
  for (size_t i = 0; i < releaseNoteCount; ++i) {
    if (strlen(releaseNotes[i].data()) == len && memcmp(releaseNotes[i].data(), value, len) == 0) {
      releaseNotesInvalid = true;
      clearReleaseNotes();
      return;
    }
  }
  safeCopy(releaseNotes[releaseNoteCount].data(), releaseNotes[releaseNoteCount].size(), value, len);
  ++releaseNoteCount;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void ReleaseJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 1) {
        if (len == 8 && memcmp(key, "tag_name", 8) == 0)
          self->lastKey = LastKey::TAG_NAME;
        else if (len == 10 && memcmp(key, "ota_status", 10) == 0)
          self->lastKey = LastKey::OTA_STATUS;
        else if (len == 13 && memcmp(key, "release_notes", 13) == 0)
          self->lastKey = LastKey::RELEASE_NOTES;
        else if (len == 6 && memcmp(key, "assets", 6) == 0)
          self->lastKey = LastKey::ASSETS;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_RELEASE_NOTES_ARRAY:
      self->releaseNotesInvalid = true;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      if (self->assetDepth == 1) {
        if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::ASSET_NAME;
        else if (len == 20 && memcmp(key, "browser_download_url", 20) == 0)
          self->lastKey = LastKey::ASSET_URL;
        else if (len == 4 && memcmp(key, "size", 4) == 0)
          self->lastKey = LastKey::ASSET_SIZE;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_ASSETS_ARRAY:
      break;
  }
}

void ReleaseJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  if (self->position == Position::IN_RELEASE_NOTES_ARRAY) {
    if (self->releaseNotesDepth == 0)
      self->commitReleaseNote(value, len);
    else
      self->releaseNotesInvalid = true;
    self->lastKey = LastKey::NONE;
    return;
  }

  switch (self->lastKey) {
    case LastKey::TAG_NAME:
      if (self->position == Position::TOP_LEVEL && self->depth == 1) {
        safeCopy(self->tagName, sizeof(self->tagName), value, len);
        self->tagFound = true;
      }
      break;
    case LastKey::OTA_STATUS:
      if (self->position == Position::TOP_LEVEL && self->depth == 1 && len == 19 &&
          memcmp(value, "unsupported_channel", 19) == 0) {
        self->unsupportedChannelFound = true;
      }
      break;
    case LastKey::RELEASE_NOTES:
      self->clearReleaseNotes();
      break;
    case LastKey::ASSET_NAME:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetName, sizeof(self->currentAssetName), value, len);
      break;
    case LastKey::ASSET_URL:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetUrl, sizeof(self->currentAssetUrl), value, len);
      break;
    case LastKey::NONE:
    case LastKey::ASSETS:
    case LastKey::ASSET_SIZE:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  if (self->position == Position::IN_RELEASE_NOTES_ARRAY) self->releaseNotesInvalid = true;

  if (self->lastKey == LastKey::ASSET_SIZE && self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
    self->currentAssetSize = static_cast<size_t>(strtoul(value, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnBool(void* ctx, bool /*value*/) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);
  if (self->position == Position::IN_RELEASE_NOTES_ARRAY) self->releaseNotesInvalid = true;
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNull(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);
  if (self->position == Position::IN_RELEASE_NOTES_ARRAY) self->releaseNotesInvalid = true;
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::IN_ASSET_OBJECT;
      self->assetDepth = 1;
      self->currentAssetName[0] = '\0';
      self->currentAssetUrl[0] = '\0';
      self->currentAssetSize = 0;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_RELEASE_NOTES_ARRAY:
      self->releaseNotesInvalid = true;
      self->releaseNotesDepth++;
      self->lastKey = LastKey::NONE;
      break;
  }
}

void ReleaseJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      if (self->assetDepth == 0) {
        self->commitAsset();
        self->position = Position::IN_ASSETS_ARRAY;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_RELEASE_NOTES_ARRAY:
      if (self->releaseNotesDepth > 0) self->releaseNotesDepth--;
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::RELEASE_NOTES && self->depth == 1) {
        const bool duplicate = self->releaseNotesFound;
        self->clearReleaseNotes();
        self->releaseNotesInvalid = duplicate || self->releaseNotes.empty();
        self->releaseNotesDepth = 0;
        self->position = Position::IN_RELEASE_NOTES_ARRAY;
      } else if (self->lastKey == LastKey::ASSETS && self->depth == 1) {
        self->position = Position::IN_ASSETS_ARRAY;
      } else {
        self->depth++;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_RELEASE_NOTES_ARRAY:
      self->releaseNotesInvalid = true;
      self->releaseNotesDepth++;
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_RELEASE_NOTES_ARRAY:
      if (self->releaseNotesDepth > 0) {
        self->releaseNotesDepth--;
      } else {
        if (self->releaseNotesInvalid || self->releaseNoteCount < 2) {
          self->clearReleaseNotes();
        } else {
          self->releaseNotesFound = true;
        }
        self->position = Position::TOP_LEVEL;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      self->lastKey = LastKey::NONE;
      break;
  }
}
