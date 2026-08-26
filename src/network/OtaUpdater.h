#pragma once

#include <ReleaseJsonParser.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

class OtaUpdater {
 public:
  enum class Channel : uint8_t { Stable, Nightly };
  using ReleaseNote = ReleaseJsonParser::ReleaseNote;

 private:
  bool updateAvailable = false;
  Channel channel = Channel::Stable;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;
  std::array<ReleaseNote, ReleaseJsonParser::RELEASE_NOTE_COUNT_MAX> releaseNotes{};
  size_t releaseNoteCount = 0;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    UNSUPPORTED_CHANNEL,
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
  std::span<const ReleaseNote> getReleaseNotes() const {
    return std::span<const ReleaseNote>(releaseNotes.data(), releaseNoteCount);
  }
#ifdef SIMULATOR
  void loadSimulatorReleaseNotes() {
    static constexpr const char* MOCK_NOTES[] = {
        "OTA更新页采用连续文档布局并支持完整日志翻页",    "更新日志按条目整页排版并保持单条内容完整显示",
        "默认字体下载新增20号与22号字号并补充伪粗体说明", "优化中文TXT分页扫描与缓存重建提升首次打开速度",
        "提升AirPage图片格式兼容性并保留旧BMP接口回退",   "修复微信读书浏览页退出卡死以及详情菜单行高异常",
        "改善多款游戏宽屏居中显示并减少象棋AI循环走子",   "提升夜间版发布与双固件下载流程的稳定性和可恢复性",
    };
    static_assert(std::size(MOCK_NOTES) <= ReleaseJsonParser::RELEASE_NOTE_COUNT_MAX);
    releaseNoteCount = std::size(MOCK_NOTES);
    for (size_t i = 0; i < releaseNoteCount; ++i) {
      strncpy(releaseNotes[i].data(), MOCK_NOTES[i], releaseNotes[i].size() - 1);
      releaseNotes[i].back() = '\0';
    }
  }
#endif
  OtaUpdaterError checkForUpdate(Channel requestedChannel);
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);
};
