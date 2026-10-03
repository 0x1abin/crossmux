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
  uint64_t archiveId = 0;
  ImageInfo image;

  bool isCurrent() const { return archiveId == 0 && image.format != ImageFormat::None; }
};

struct SelectedImage {
  static constexpr size_t kPathCapacity = 96;

  char path[kPathCapacity]{};
  ImageInfo image;
  bool current = false;
};

class AirPageImageStore final {
 public:
  static constexpr size_t kHistoryPageSize = 20;
  static constexpr char kCacheDir[] = "/AirPage";
  static constexpr char kHistoryDir[] = "/AirPage";
  static constexpr char kDownloadPartPath[] = "/AirPage/latest.bmp.part";

  enum class InitializationResult : uint8_t { Empty, Ready, Invalid };
  enum class StageResult : uint8_t { Failed, Unchanged, PendingDisplay };
  enum class RejectResult : uint8_t { CurrentRestored, CurrentInvalid, HistoryInvalid };

  InitializationResult initialize(uint64_t archiveDateKey = 0);
  bool ensureDirectories() const;
  StageResult stageDownloadedImage(uint64_t archiveDateKey = 0);

  bool selectCurrent(SelectedImage& selected) const;
  bool selectHistory(size_t index, SelectedImage& selected);
  RejectResult rejectDisplayedImage(const SelectedImage& selected);
  void commitDisplayedDownload(uint64_t archiveDateKey = 0);

  bool hasImage() const { return currentImage_.format != ImageFormat::None; }
  bool hasPendingDownload() const { return pendingDisplayValidation_; }
  const ImageInfo& currentImage() const { return currentImage_; }
  void firstHistoryPage();
  bool nextHistoryPage();
  bool previousHistoryPage();
  bool hasPreviousHistoryPage() const { return hasPreviousPage_; }
  bool hasNextHistoryPage() const { return hasNextPage_; }
  // Count and entries refer only to the loaded page. Files are never capped.
  size_t historyCount() const { return historyCount_; }
  const HistoryEntry& historyEntry(size_t index) const { return history_[index]; }

  static bool inspectImage(const char* path, ImageInfo& info);
  static bool formatPixelCachePath(const char* imagePath, char* path, size_t pathSize);

 private:
  static constexpr size_t kPathBufferSize = SelectedImage::kPathCapacity;

  static const char* imagePathForFormat(ImageFormat format);
  static const char* backupPathForFormat(ImageFormat format);
  const char* currentImagePath() const;
  static bool isValidPixelCache(const char* path);
  bool filesEqual(const char* lhsPath, const char* rhsPath) const;
  bool installDownloadedImage(const ImageInfo& downloaded, uint64_t archiveDateKey);
  bool recoverCachedImage(uint64_t archiveDateKey);
  bool rollbackPendingImage();
  static void discardPendingBackups();

  enum class HistoryPage : uint8_t { First, Next, Previous };
  void scanHistory(HistoryPage page = HistoryPage::First, const HistoryEntry& anchor = {});
  static bool newerHistoryEntry(const HistoryEntry& lhs, const HistoryEntry& rhs);
  void setCurrentHistoryEntry();
  void removeCurrentHistoryEntry();
  void insertHistoryEntry(const HistoryEntry& entry);
  void removeHistoryEntry(uint64_t archiveId, ImageFormat format);
  static bool parseHistoryId(const char* name, uint64_t& archiveId);
  static bool parseHistoryName(const char* name, HistoryEntry& entry);
  static bool formatHistoryPath(uint64_t archiveId, ImageFormat format, char* path, size_t pathSize);
  static uint64_t nextHistoryId(uint64_t archiveDateKey);
  static bool archivePendingBackup(uint64_t archiveDateKey, HistoryEntry* archived);

  ImageInfo currentImage_;
  std::array<HistoryEntry, kHistoryPageSize> history_{};
  size_t historyCount_ = 0;
  bool historyInitialized_ = false;
  bool hasPreviousPage_ = false;
  bool hasNextPage_ = false;
  bool pendingDisplayValidation_ = false;
};

}  // namespace airpage
