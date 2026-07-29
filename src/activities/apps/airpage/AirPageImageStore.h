#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace airpage {

enum class ImageFormat : uint8_t { None, Bmp, Jpeg };

struct ImageInfo {
  ImageFormat format = ImageFormat::None;
  int16_t width = 0;
  int16_t height = 0;
  bool hasGrayscale = false;
};

struct HistoryEntry {
  uint32_t sequence = 0;
  ImageInfo image;
  bool current = false;
};

struct SelectedImage {
  static constexpr size_t kPathCapacity = 96;

  char path[kPathCapacity]{};
  ImageInfo image;
  bool current = false;
};

class AirPageImageStore final {
 public:
  static constexpr size_t kMaxHistoryEntries = 20;
  static constexpr char kCacheDir[] = "/.crosspoint/airpage";
  static constexpr char kHistoryDir[] = "/.crosspoint/airpage/history";
  static constexpr char kDownloadPartPath[] = "/.crosspoint/airpage/latest.bmp.part";

  enum class InitializationResult : uint8_t { Empty, Ready, Invalid };
  enum class StageResult : uint8_t { Failed, Unchanged, PendingDisplay };
  enum class RejectResult : uint8_t { CurrentRestored, CurrentInvalid, HistoryInvalid };

  InitializationResult initialize();
  bool ensureDirectories() const;
  StageResult stageDownloadedImage();

  bool selectCurrent(SelectedImage& selected) const;
  bool selectHistory(size_t index, SelectedImage& selected);
  RejectResult rejectDisplayedImage(const SelectedImage& selected);
  void commitDisplayedDownload();

  bool hasImage() const { return currentImage_.format != ImageFormat::None; }
  bool hasPendingDownload() const { return pendingDisplayValidation_; }
  const ImageInfo& currentImage() const { return currentImage_; }
  size_t historyCount() const { return historyCount_; }
  const HistoryEntry& historyEntry(size_t index) const { return history_[index]; }

  static bool inspectImage(const char* path, ImageInfo& info);
  static bool formatPixelCachePath(const char* imagePath, char* path, size_t pathSize);

 private:
  static constexpr size_t kPathBufferSize = SelectedImage::kPathCapacity;

  const char* imagePathForFormat(ImageFormat format) const;
  const char* backupPathForFormat(ImageFormat format) const;
  const char* currentImagePath() const;
  bool isValidPixelCache(const char* path) const;
  bool filesEqual(const char* lhsPath, const char* rhsPath) const;
  bool installDownloadedImage(const ImageInfo& downloaded);
  bool recoverCachedImage();
  bool rollbackPendingImage();
  void discardPendingBackups();

  void scanHistory();
  void setCurrentHistoryEntry();
  void removeCurrentHistoryEntry();
  void insertHistoryEntry(const HistoryEntry& entry);
  void removeHistoryEntry(uint32_t sequence, ImageFormat format);
  bool parseHistoryName(const char* name, HistoryEntry& entry) const;
  bool formatHistoryPath(uint32_t sequence, ImageFormat format, char* path, size_t pathSize) const;
  bool historyContains(uint32_t sequence, ImageFormat format) const;
  uint32_t nextHistorySequence() const;
  bool archivePendingBackup(HistoryEntry* archived);
  void pruneHistoryFiles();

  ImageInfo currentImage_;
  std::array<HistoryEntry, kMaxHistoryEntries> history_{};
  size_t historyCount_ = 0;
  bool historyInitialized_ = false;
  bool pendingDisplayValidation_ = false;
};

}  // namespace airpage
