#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "StreamingJsonParser.h"

class ReleaseJsonParser {
 public:
  static constexpr size_t RELEASE_NOTE_COUNT_MAX = 8;
  static constexpr size_t RELEASE_NOTE_SIZE = 97;
  using ReleaseNote = std::array<char, RELEASE_NOTE_SIZE>;

  explicit ReleaseJsonParser(std::span<ReleaseNote> releaseNotes = {});

  ReleaseJsonParser(const ReleaseJsonParser&) = delete;
  ReleaseJsonParser& operator=(const ReleaseJsonParser&) = delete;

  void reset();
  void feed(const char* data, size_t len);

  bool foundTag() const;
  bool foundFirmware() const;
  bool foundUnsupportedChannel() const;
  bool foundReleaseNotes() const;
  const char* getTagName() const;
  const char* getFirmwareUrl() const;
  size_t getReleaseNoteCount() const;
  size_t getFirmwareSize() const;

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_RELEASE_NOTES_ARRAY,
    IN_ASSETS_ARRAY,
    IN_ASSET_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    TAG_NAME,
    OTA_STATUS,
    RELEASE_NOTES,
    ASSETS,
    ASSET_NAME,
    ASSET_URL,
    ASSET_SIZE,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitAsset();
  void commitReleaseNote(const char* value, size_t len);
  void clearReleaseNotes();

  StreamingJsonParser parser;

  Position position;
  LastKey lastKey;
  uint8_t depth;
  uint8_t assetDepth;
  uint8_t releaseNotesDepth;

  char tagName[32];
  char firmwareUrl[512];
  size_t firmwareSize;
  bool tagFound;
  bool firmwareFound;
  bool unsupportedChannelFound;
  bool releaseNotesFound;
  bool releaseNotesInvalid;
  size_t releaseNoteCount;
  std::span<ReleaseNote> releaseNotes;

  char currentAssetName[32];
  char currentAssetUrl[512];
  size_t currentAssetSize;
};
