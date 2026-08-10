#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class OtaUpdater {
 public:
  enum class Channel : uint8_t { Stable, Nightly };
  static constexpr size_t SUMMARY_LINE_COUNT = 2;

 private:
  static constexpr size_t SUMMARY_LINE_SIZE = 64;

  bool updateAvailable = false;
  Channel channel = Channel::Stable;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;
  char summaryLines[SUMMARY_LINE_COUNT][SUMMARY_LINE_SIZE] = {};

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    WRONG_DEVICE_ERROR,
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  const char* getSummaryLine(size_t index) const { return index < SUMMARY_LINE_COUNT ? summaryLines[index] : ""; }
  OtaUpdaterError checkForUpdate(Channel requestedChannel);
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);
};
