#include "AirPageImageStore.h"

#include <Bitmap.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Epub/converters/ImageDimsProbe.h"

namespace airpage {

namespace {

constexpr char kBmpImagePath[] = "/.crosspoint/airpage/latest.bmp";
constexpr char kJpegImagePath[] = "/.crosspoint/airpage/latest.jpg";
constexpr char kBmpBackupPath[] = "/.crosspoint/airpage/latest.bmp.bak";
constexpr char kJpegBackupPath[] = "/.crosspoint/airpage/latest.jpg.bak";
constexpr char kPixelCachePath[] = "/.crosspoint/airpage/latest.pxc";
constexpr char kPixelCacheBackupPath[] = "/.crosspoint/airpage/latest.pxc.bak";
constexpr size_t kIoBufferSize = 128;

}  // namespace

AirPageImageStore::InitializationResult AirPageImageStore::initialize(const uint64_t archiveDateKey) {
  historyCount_ = 0;
  historyInitialized_ = false;
  currentImage_ = {};
  pendingDisplayValidation_ = false;

  const bool hadCanonicalImage = Storage.exists(kBmpImagePath) || Storage.exists(kJpegImagePath);
  const bool recovered = recoverCachedImage(archiveDateKey);
  scanHistory();
  if (recovered) return InitializationResult::Ready;
  return hadCanonicalImage ? InitializationResult::Invalid : InitializationResult::Empty;
}

bool AirPageImageStore::ensureDirectories() const {
  return Storage.ensureDirectoryExists(kCacheDir) && Storage.ensureDirectoryExists(kHistoryDir);
}

const char* AirPageImageStore::imagePathForFormat(const ImageFormat format) const {
  switch (format) {
    case ImageFormat::None:
      return nullptr;
    case ImageFormat::Bmp:
      return kBmpImagePath;
    case ImageFormat::Jpeg:
      return kJpegImagePath;
  }
  return nullptr;
}

const char* AirPageImageStore::backupPathForFormat(const ImageFormat format) const {
  switch (format) {
    case ImageFormat::None:
      return nullptr;
    case ImageFormat::Bmp:
      return kBmpBackupPath;
    case ImageFormat::Jpeg:
      return kJpegBackupPath;
  }
  return nullptr;
}

const char* AirPageImageStore::currentImagePath() const { return imagePathForFormat(currentImage_.format); }

bool AirPageImageStore::inspectImage(const char* path, ImageInfo& info) {
  info = {};
  if (!path || !Storage.exists(path)) return false;

  HalFile file;
  if (!Storage.openFileForRead("AIRP", path, file)) return false;

  uint8_t signature[2];
  if (file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature))) return false;

  if (signature[0] == 'B' && signature[1] == 'M') {
    Bitmap bitmap(file, /*dithering=*/false);
    const BmpReaderError error = bitmap.parseHeaders();
    if (error != BmpReaderError::Ok) {
      LOG_ERR("AIRP", "Invalid BMP %s: %s", path, Bitmap::errorToString(error));
      return false;
    }

    const int width = bitmap.getWidth();
    const int height = bitmap.getHeight();
    if (width <= 0 || height <= 0 || width > INT16_MAX || height > INT16_MAX) {
      LOG_ERR("AIRP", "Invalid BMP dimensions %s: %dx%d", path, width, height);
      return false;
    }

    const uint64_t pixelBytes = static_cast<uint64_t>(bitmap.getRowBytes()) * static_cast<uint64_t>(height);
    const uint64_t expectedSize = static_cast<uint64_t>(file.position()) + pixelBytes;
    if (expectedSize > file.fileSize64()) {
      LOG_ERR("AIRP", "Truncated BMP %s: need=%llu", path, static_cast<unsigned long long>(expectedSize));
      return false;
    }

    info.format = ImageFormat::Bmp;
    info.width = static_cast<int16_t>(width);
    info.height = static_cast<int16_t>(height);
    info.hasGrayscale = bitmap.hasGreyscale();
    return true;
  }

  if (signature[0] != 0xFF || signature[1] != 0xD8 || !file.seek(0)) {
    LOG_ERR("AIRP", "Unsupported image format: %s", path);
    return false;
  }

  ImageDimsProbe probe;
  uint8_t buffer[kIoBufferSize];
  while (file.available() > 0) {
    const size_t want = std::min<size_t>(sizeof(buffer), static_cast<size_t>(file.available()));
    const int bytesRead = file.read(buffer, want);
    if (bytesRead <= 0) break;
    if (probe.write(buffer, static_cast<size_t>(bytesRead)) < static_cast<size_t>(bytesRead)) break;
  }

  ImageDimensions dimensions{};
  if (!probe.getDimensions(dimensions)) {
    LOG_ERR("AIRP", "Invalid JPEG header: %s", path);
    return false;
  }

  info.format = ImageFormat::Jpeg;
  info.width = dimensions.width;
  info.height = dimensions.height;
  info.hasGrayscale = true;
  return true;
}

bool AirPageImageStore::isValidPixelCache(const char* path) const {
  HalFile file;
  if (!path || !Storage.openFileForRead("AIRP", path, file)) return false;

  uint16_t width = 0;
  uint16_t height = 0;
  if (file.read(&width, sizeof(width)) != static_cast<int>(sizeof(width)) ||
      file.read(&height, sizeof(height)) != static_cast<int>(sizeof(height)) || width == 0 || height == 0) {
    return false;
  }

  const uint64_t bytesPerRow = (static_cast<uint64_t>(width) + 3u) / 4u;
  return file.fileSize64() >= 4u + bytesPerRow * height;
}

bool AirPageImageStore::filesEqual(const char* lhsPath, const char* rhsPath) const {
  HalFile lhs;
  HalFile rhs;
  if (!Storage.openFileForRead("AIRP", lhsPath, lhs) || !Storage.openFileForRead("AIRP", rhsPath, rhs) ||
      lhs.fileSize64() != rhs.fileSize64()) {
    return false;
  }

  uint8_t lhsBuffer[kIoBufferSize];
  uint8_t rhsBuffer[kIoBufferSize];
  while (lhs.available() > 0) {
    const size_t want = std::min<size_t>(sizeof(lhsBuffer), static_cast<size_t>(lhs.available()));
    const int lhsRead = lhs.read(lhsBuffer, want);
    const int rhsRead = rhs.read(rhsBuffer, want);
    if (lhsRead != static_cast<int>(want) || rhsRead != lhsRead ||
        memcmp(lhsBuffer, rhsBuffer, static_cast<size_t>(lhsRead)) != 0) {
      return false;
    }
  }
  return rhs.available() == 0;
}

void AirPageImageStore::discardPendingBackups() {
  Storage.remove(kBmpBackupPath);
  Storage.remove(kJpegBackupPath);
  Storage.remove(kPixelCacheBackupPath);
}

bool AirPageImageStore::rollbackPendingImage() {
  ImageInfo backupInfo;
  ImageFormat backupFormat = ImageFormat::None;
  if (inspectImage(kBmpBackupPath, backupInfo)) {
    backupFormat = ImageFormat::Bmp;
  } else if (inspectImage(kJpegBackupPath, backupInfo)) {
    backupFormat = ImageFormat::Jpeg;
  } else {
    return false;
  }

  if (Storage.exists(kBmpImagePath) && !Storage.remove(kBmpImagePath)) return false;
  if (Storage.exists(kJpegImagePath) && !Storage.remove(kJpegImagePath)) return false;
  Storage.remove(kPixelCachePath);

  const char* backupPath = backupPathForFormat(backupFormat);
  const char* imagePath = imagePathForFormat(backupFormat);
  if (!Storage.rename(backupPath, imagePath)) {
    LOG_ERR("AIRP", "Failed to restore image backup");
    return false;
  }

  currentImage_ = backupInfo;
  pendingDisplayValidation_ = false;
  if (backupFormat == ImageFormat::Jpeg && Storage.exists(kPixelCacheBackupPath)) {
    Storage.rename(kPixelCacheBackupPath, kPixelCachePath);
  } else {
    Storage.remove(kPixelCacheBackupPath);
  }
  Storage.remove(backupFormat == ImageFormat::Bmp ? kJpegBackupPath : kBmpBackupPath);
  setCurrentHistoryEntry();
  return true;
}

bool AirPageImageStore::installDownloadedImage(const ImageInfo& downloaded, const uint64_t archiveDateKey) {
  if (downloaded.format == ImageFormat::None) return false;

  ImageInfo pendingBackup;
  const bool hasValidPendingBackup =
      inspectImage(kBmpBackupPath, pendingBackup) || inspectImage(kJpegBackupPath, pendingBackup);
  if (hasValidPendingBackup) {
    const bool currentReady = currentImage_.format == ImageFormat::Bmp ||
                              (currentImage_.format == ImageFormat::Jpeg && isValidPixelCache(kPixelCachePath));
    if (currentReady) {
      HistoryEntry archived;
      if (!archivePendingBackup(archiveDateKey, &archived)) return false;
      if (archived.image.format != ImageFormat::None) insertHistoryEntry(archived);
    } else if (!rollbackPendingImage()) {
      return false;
    }
  }

  discardPendingBackups();

  const ImageFormat oldFormat = currentImage_.format;
  const char* oldPath = imagePathForFormat(oldFormat);
  const char* oldBackupPath = backupPathForFormat(oldFormat);
  const bool hadCurrent = oldPath && Storage.exists(oldPath);
  if (hadCurrent && !Storage.rename(oldPath, oldBackupPath)) {
    LOG_ERR("AIRP", "Failed to preserve cached image");
    return false;
  }

  if (oldFormat == ImageFormat::Jpeg && Storage.exists(kPixelCachePath)) {
    if (!Storage.rename(kPixelCachePath, kPixelCacheBackupPath)) Storage.remove(kPixelCachePath);
  } else {
    Storage.remove(kPixelCachePath);
  }

  const char* targetPath = imagePathForFormat(downloaded.format);
  if (Storage.exists(targetPath) && !Storage.remove(targetPath)) {
    if (hadCurrent) {
      Storage.rename(oldBackupPath, oldPath);
      if (oldFormat == ImageFormat::Jpeg && Storage.exists(kPixelCacheBackupPath)) {
        Storage.rename(kPixelCacheBackupPath, kPixelCachePath);
      }
    }
    return false;
  }

  if (!Storage.rename(kDownloadPartPath, targetPath)) {
    LOG_ERR("AIRP", "Failed to install downloaded image");
    if (hadCurrent) {
      Storage.rename(oldBackupPath, oldPath);
      if (oldFormat == ImageFormat::Jpeg && Storage.exists(kPixelCacheBackupPath)) {
        Storage.rename(kPixelCacheBackupPath, kPixelCachePath);
      }
    }
    return false;
  }

  const char* inactivePath = downloaded.format == ImageFormat::Bmp ? kJpegImagePath : kBmpImagePath;
  if (Storage.exists(inactivePath)) Storage.remove(inactivePath);
  currentImage_ = downloaded;
  pendingDisplayValidation_ = true;
  return true;
}

AirPageImageStore::StageResult AirPageImageStore::stageDownloadedImage(const uint64_t archiveDateKey) {
  ImageInfo downloaded;
  if (!inspectImage(kDownloadPartPath, downloaded)) {
    Storage.remove(kDownloadPartPath);
    return StageResult::Failed;
  }

  const char* existingPath = currentImagePath();
  if (existingPath && Storage.exists(existingPath) && filesEqual(kDownloadPartPath, existingPath)) {
    Storage.remove(kDownloadPartPath);
    return StageResult::Unchanged;
  }

  return installDownloadedImage(downloaded, archiveDateKey) ? StageResult::PendingDisplay : StageResult::Failed;
}

bool AirPageImageStore::recoverCachedImage(const uint64_t archiveDateKey) {
  ImageInfo jpegInfo;
  ImageInfo bmpInfo;
  bool haveJpeg = inspectImage(kJpegImagePath, jpegInfo);
  bool haveBmp = inspectImage(kBmpImagePath, bmpInfo);
  bool jpegWasInstalled = isValidPixelCache(kPixelCachePath) || Storage.exists(kBmpBackupPath) ||
                          Storage.exists(kJpegBackupPath) || !haveBmp;

  ImageInfo partInfo;
  if (inspectImage(kDownloadPartPath, partInfo)) {
    if (haveJpeg && jpegWasInstalled) {
      currentImage_ = jpegInfo;
    } else if (haveBmp) {
      currentImage_ = bmpInfo;
    } else {
      ImageInfo backupInfo;
      const bool haveBackup = inspectImage(kBmpBackupPath, backupInfo) || inspectImage(kJpegBackupPath, backupInfo);
      if (haveBackup && !rollbackPendingImage()) return false;
      if (!haveBackup) currentImage_ = {};
    }

    const char* existingPath = currentImagePath();
    if (existingPath && Storage.exists(existingPath) && filesEqual(kDownloadPartPath, existingPath)) {
      Storage.remove(kDownloadPartPath);
      return true;
    }
    if (installDownloadedImage(partInfo, archiveDateKey)) return true;
    haveJpeg = inspectImage(kJpegImagePath, jpegInfo);
    haveBmp = inspectImage(kBmpImagePath, bmpInfo);
    jpegWasInstalled = isValidPixelCache(kPixelCachePath) || Storage.exists(kBmpBackupPath) ||
                       Storage.exists(kJpegBackupPath) || !haveBmp;
  }

  if (haveJpeg && jpegWasInstalled) {
    currentImage_ = jpegInfo;
    Storage.remove(kDownloadPartPath);
    pendingDisplayValidation_ = Storage.exists(kBmpBackupPath) || Storage.exists(kJpegBackupPath);
    if (pendingDisplayValidation_ && isValidPixelCache(kPixelCachePath)) {
      HistoryEntry archived;
      if (archivePendingBackup(archiveDateKey, &archived)) {
        pendingDisplayValidation_ = false;
      }
    }
    return true;
  }

  if (haveBmp) {
    currentImage_ = bmpInfo;
    Storage.remove(kDownloadPartPath);
    pendingDisplayValidation_ = Storage.exists(kBmpBackupPath) || Storage.exists(kJpegBackupPath);
    commitDisplayedDownload(archiveDateKey);
    return true;
  }

  ImageInfo backupInfo;
  if (inspectImage(kBmpBackupPath, backupInfo)) {
    if (Storage.exists(kBmpImagePath) && !Storage.remove(kBmpImagePath)) return false;
    if (Storage.exists(kJpegImagePath) && !Storage.remove(kJpegImagePath)) return false;
    if (!Storage.rename(kBmpBackupPath, kBmpImagePath)) return false;
    currentImage_ = backupInfo;
    Storage.remove(kJpegBackupPath);
    Storage.remove(kDownloadPartPath);
    Storage.remove(kPixelCachePath);
    Storage.remove(kPixelCacheBackupPath);
    return true;
  }

  if (inspectImage(kJpegBackupPath, backupInfo)) {
    if (Storage.exists(kBmpImagePath) && !Storage.remove(kBmpImagePath)) return false;
    if (Storage.exists(kJpegImagePath) && !Storage.remove(kJpegImagePath)) return false;
    if (!Storage.rename(kJpegBackupPath, kJpegImagePath)) return false;
    currentImage_ = backupInfo;
    Storage.remove(kBmpBackupPath);
    Storage.remove(kDownloadPartPath);
    Storage.remove(kPixelCachePath);
    if (Storage.exists(kPixelCacheBackupPath)) Storage.rename(kPixelCacheBackupPath, kPixelCachePath);
    return true;
  }

  Storage.remove(kDownloadPartPath);
  discardPendingBackups();
  Storage.remove(kPixelCachePath);
  currentImage_ = {};
  return false;
}

bool AirPageImageStore::formatHistoryPath(const uint64_t archiveId, const ImageFormat format, char* path,
                                          const size_t pathSize) const {
  const char* extension = nullptr;
  switch (format) {
    case ImageFormat::None:
      return false;
    case ImageFormat::Bmp:
      extension = "bmp";
      break;
    case ImageFormat::Jpeg:
      extension = "jpg";
      break;
  }
  int written = 0;
  if (archiveId <= 99999999u) {
    written =
        snprintf(path, pathSize, "%s/%08llu.%s", kHistoryDir, static_cast<unsigned long long>(archiveId), extension);
  } else {
    const unsigned collision = static_cast<unsigned>(archiveId % 100u);
    uint64_t dateKey = archiveId / 100u;
    const unsigned second = static_cast<unsigned>(dateKey % 100u);
    dateKey /= 100u;
    const unsigned minute = static_cast<unsigned>(dateKey % 100u);
    dateKey /= 100u;
    const unsigned hour = static_cast<unsigned>(dateKey % 100u);
    dateKey /= 100u;
    const unsigned day = static_cast<unsigned>(dateKey % 100u);
    dateKey /= 100u;
    const unsigned month = static_cast<unsigned>(dateKey % 100u);
    const unsigned year = static_cast<unsigned>(dateKey / 100u);
    if (collision == 0) {
      written = snprintf(path, pathSize, "%s/%04u%02u%02u_%02u%02u%02u.%s", kHistoryDir, year, month, day, hour, minute,
                         second, extension);
    } else {
      written = snprintf(path, pathSize, "%s/%04u%02u%02u_%02u%02u%02u-%02u.%s", kHistoryDir, year, month, day, hour,
                         minute, second, collision, extension);
    }
  }
  return written > 0 && static_cast<size_t>(written) < pathSize;
}

bool AirPageImageStore::formatPixelCachePath(const char* imagePath, char* path, const size_t pathSize) {
  if (!imagePath || !path || pathSize == 0) return false;
  const char* dot = strrchr(imagePath, '.');
  const size_t stemLength = dot ? static_cast<size_t>(dot - imagePath) : strlen(imagePath);
  if (stemLength + sizeof(".pxc") > pathSize) return false;
  memcpy(path, imagePath, stemLength);
  memcpy(path + stemLength, ".pxc", sizeof(".pxc"));
  return true;
}

bool AirPageImageStore::parseHistoryId(const char* name, uint64_t& archiveId) const {
  archiveId = 0;
  if (!name) return false;
  const char* base = strrchr(name, '/');
  base = base ? base + 1 : name;
  const char* dot = strrchr(base, '.');
  if (!dot) return false;
  const size_t stemLength = static_cast<size_t>(dot - base);

  if (stemLength == 8) {
    for (size_t i = 0; i < stemLength; ++i) {
      if (!std::isdigit(static_cast<unsigned char>(base[i]))) return false;
      archiveId = archiveId * 10u + static_cast<uint64_t>(base[i] - '0');
    }
    return archiveId != 0;
  }

  if ((stemLength != 15 && stemLength != 18) || base[8] != '_' || (stemLength == 18 && base[15] != '-')) {
    return false;
  }
  uint64_t dateKey = 0;
  for (size_t i = 0; i < 15; ++i) {
    if (i == 8) continue;
    if (!std::isdigit(static_cast<unsigned char>(base[i]))) return false;
    dateKey = dateKey * 10u + static_cast<uint64_t>(base[i] - '0');
  }
  const unsigned second = static_cast<unsigned>(dateKey % 100u);
  const unsigned minute = static_cast<unsigned>((dateKey / 100u) % 100u);
  const unsigned hour = static_cast<unsigned>((dateKey / 10000u) % 100u);
  const unsigned day = static_cast<unsigned>((dateKey / 1000000u) % 100u);
  const unsigned month = static_cast<unsigned>((dateKey / 100000000u) % 100u);
  const unsigned year = static_cast<unsigned>(dateKey / 10000000000u);
  if (year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
      second > 59) {
    return false;
  }

  unsigned collision = 0;
  if (stemLength == 18) {
    if (!std::isdigit(static_cast<unsigned char>(base[16])) || !std::isdigit(static_cast<unsigned char>(base[17]))) {
      return false;
    }
    collision = static_cast<unsigned>(base[16] - '0') * 10u + static_cast<unsigned>(base[17] - '0');
    if (collision == 0) return false;
  }
  archiveId = dateKey * 100u + collision;
  return true;
}

bool AirPageImageStore::parseHistoryName(const char* name, HistoryEntry& entry) const {
  entry = {};
  uint64_t archiveId = 0;
  if (!parseHistoryId(name, archiveId)) return false;
  const char* base = strrchr(name, '/');
  base = base ? base + 1 : name;
  const char* extension = strrchr(base, '.');
  if (!extension) return false;

  ImageFormat format = ImageFormat::None;
  if (strcmp(extension + 1, "bmp") == 0) {
    format = ImageFormat::Bmp;
  } else if (strcmp(extension + 1, "jpg") == 0) {
    format = ImageFormat::Jpeg;
  } else {
    return false;
  }

  char path[kPathBufferSize];
  if (!formatHistoryPath(archiveId, format, path, sizeof(path))) return false;
  ImageInfo image;
  if (!inspectImage(path, image) || image.format != format) return false;
  entry.archiveId = archiveId;
  entry.image = image;
  return true;
}

void AirPageImageStore::insertHistoryEntry(const HistoryEntry& entry) {
  size_t position = 0;
  while (position < historyCount_) {
    const HistoryEntry& existing = history_[position];
    if (entry.isCurrent() || (!existing.isCurrent() && entry.archiveId > existing.archiveId)) break;
    ++position;
  }
  if (position >= kMaxHistoryEntries) return;

  const size_t newCount = std::min(kMaxHistoryEntries, historyCount_ + 1);
  for (size_t i = newCount - 1; i > position; --i) history_[i] = history_[i - 1];
  history_[position] = entry;
  historyCount_ = newCount;
}

void AirPageImageStore::removeCurrentHistoryEntry() {
  for (size_t i = 0; i < historyCount_; ++i) {
    if (!history_[i].isCurrent()) continue;
    for (size_t j = i + 1; j < historyCount_; ++j) history_[j - 1] = history_[j];
    --historyCount_;
    return;
  }
}

void AirPageImageStore::setCurrentHistoryEntry() {
  removeCurrentHistoryEntry();
  if (hasImage()) insertHistoryEntry(HistoryEntry{0, currentImage_});
}

void AirPageImageStore::removeHistoryEntry(const uint64_t archiveId, const ImageFormat format) {
  for (size_t i = 0; i < historyCount_; ++i) {
    if (history_[i].isCurrent() || history_[i].archiveId != archiveId || history_[i].image.format != format) continue;
    for (size_t j = i + 1; j < historyCount_; ++j) history_[j - 1] = history_[j];
    --historyCount_;
    return;
  }
}

bool AirPageImageStore::historyContains(const uint64_t archiveId, const ImageFormat format) const {
  for (size_t i = 0; i < historyCount_; ++i) {
    if (!history_[i].isCurrent() && history_[i].archiveId == archiveId && history_[i].image.format == format) {
      return true;
    }
  }
  return false;
}

void AirPageImageStore::scanHistory() {
  historyCount_ = 0;
  setCurrentHistoryEntry();

  if (Storage.ensureDirectoryExists(kHistoryDir)) {
    auto directory = Storage.open(kHistoryDir);
    if (directory && directory.isDirectory()) {
      directory.rewindDirectory();
      for (auto file = directory.openNextFile(); file; file = directory.openNextFile()) {
        if (file.isDirectory()) continue;
        char name[kPathBufferSize];
        if (!file.getName(name, sizeof(name))) continue;
        HistoryEntry entry;
        if (parseHistoryName(name, entry)) insertHistoryEntry(entry);
      }
    }
  }
  historyInitialized_ = true;
  pruneHistoryFiles();
}

uint64_t AirPageImageStore::nextHistoryId(const uint64_t archiveDateKey) const {
  constexpr uint64_t kMinimumDateKey = 20240101000000u;
  constexpr uint64_t kMaximumDateKey = 20991231235959u;
  if (archiveDateKey >= kMinimumDateKey && archiveDateKey <= kMaximumDateKey) {
    for (uint64_t collision = 0; collision <= 99; ++collision) {
      const uint64_t archiveId = archiveDateKey * 100u + collision;
      char bmpPath[kPathBufferSize];
      char jpegPath[kPathBufferSize];
      char cachePath[kPathBufferSize];
      if (!formatHistoryPath(archiveId, ImageFormat::Bmp, bmpPath, sizeof(bmpPath)) ||
          !formatHistoryPath(archiveId, ImageFormat::Jpeg, jpegPath, sizeof(jpegPath)) ||
          !formatPixelCachePath(jpegPath, cachePath, sizeof(cachePath))) {
        return 0;
      }
      if (!Storage.exists(bmpPath) && !Storage.exists(jpegPath) && !Storage.exists(cachePath)) return archiveId;
    }
    return 0;
  }

  constexpr uint64_t kMaximumSequence = 99999999u;
  uint64_t maximum = 0;
  if (historyInitialized_) {
    for (size_t i = 0; i < historyCount_; ++i) {
      if (!history_[i].isCurrent() && history_[i].archiveId <= kMaximumSequence) {
        maximum = std::max(maximum, history_[i].archiveId);
      }
    }
  } else {
    auto directory = Storage.open(kHistoryDir);
    if (directory && directory.isDirectory()) {
      directory.rewindDirectory();
      for (auto file = directory.openNextFile(); file; file = directory.openNextFile()) {
        if (file.isDirectory()) continue;
        char name[kPathBufferSize];
        if (!file.getName(name, sizeof(name))) continue;
        uint64_t archiveId = 0;
        if (parseHistoryId(name, archiveId) && archiveId <= kMaximumSequence) {
          maximum = std::max(maximum, archiveId);
        }
      }
    }
  }
  if (maximum < kMaximumSequence) return maximum + 1;

  for (uint64_t candidate = 1; candidate <= kMaximumSequence; ++candidate) {
    char bmpPath[kPathBufferSize];
    char jpegPath[kPathBufferSize];
    if (!formatHistoryPath(candidate, ImageFormat::Bmp, bmpPath, sizeof(bmpPath)) ||
        !formatHistoryPath(candidate, ImageFormat::Jpeg, jpegPath, sizeof(jpegPath))) {
      return 0;
    }
    if (!Storage.exists(bmpPath) && !Storage.exists(jpegPath)) return candidate;
  }
  return 0;
}

bool AirPageImageStore::archivePendingBackup(const uint64_t archiveDateKey, HistoryEntry* archived) {
  if (archived) *archived = {};
  ImageInfo backupInfo;
  ImageFormat format = ImageFormat::None;
  const char* backupPath = nullptr;
  if (inspectImage(kBmpBackupPath, backupInfo)) {
    format = ImageFormat::Bmp;
    backupPath = kBmpBackupPath;
  } else if (inspectImage(kJpegBackupPath, backupInfo)) {
    format = ImageFormat::Jpeg;
    backupPath = kJpegBackupPath;
  } else {
    discardPendingBackups();
    return true;
  }

  if (!Storage.ensureDirectoryExists(kHistoryDir)) return false;
  const uint64_t archiveId = nextHistoryId(archiveDateKey);
  char historyPath[kPathBufferSize];
  if (archiveId == 0 || !formatHistoryPath(archiveId, format, historyPath, sizeof(historyPath)) ||
      !Storage.rename(backupPath, historyPath)) {
    return false;
  }

  if (format == ImageFormat::Jpeg && Storage.exists(kPixelCacheBackupPath)) {
    char cachePath[kPathBufferSize];
    if (!formatPixelCachePath(historyPath, cachePath, sizeof(cachePath)) ||
        !Storage.rename(kPixelCacheBackupPath, cachePath)) {
      Storage.remove(kPixelCacheBackupPath);
    }
  } else {
    Storage.remove(kPixelCacheBackupPath);
  }
  Storage.remove(format == ImageFormat::Bmp ? kJpegBackupPath : kBmpBackupPath);
  if (archived) *archived = HistoryEntry{archiveId, backupInfo};
  return true;
}

void AirPageImageStore::pruneHistoryFiles() {
  while (true) {
    char removePath[kPathBufferSize]{};
    bool removeRelatedCache = false;
    {
      auto directory = Storage.open(kHistoryDir);
      if (!directory || !directory.isDirectory()) return;
      directory.rewindDirectory();
      for (auto file = directory.openNextFile(); file; file = directory.openNextFile()) {
        if (file.isDirectory()) continue;
        char name[kPathBufferSize];
        if (!file.getName(name, sizeof(name))) continue;
        const char* base = strrchr(name, '/');
        base = base ? base + 1 : name;

        bool keep = false;
        HistoryEntry entry;
        if (parseHistoryName(base, entry)) {
          keep = historyContains(entry.archiveId, entry.image.format);
          if (!keep) {
            formatHistoryPath(entry.archiveId, entry.image.format, removePath, sizeof(removePath));
            removeRelatedCache = true;
          }
        } else if (const char* extension = strrchr(base, '.'); extension && strcmp(extension + 1, "pxc") == 0) {
          uint64_t archiveId = 0;
          keep = parseHistoryId(base, archiveId) && historyContains(archiveId, ImageFormat::Jpeg);
          if (!keep) {
            const size_t prefixLength = strlen(kHistoryDir);
            const size_t nameLength = strlen(base);
            if (prefixLength + 1 + nameLength < sizeof(removePath)) {
              memcpy(removePath, kHistoryDir, prefixLength);
              removePath[prefixLength] = '/';
              memcpy(removePath + prefixLength + 1, base, nameLength + 1);
            }
          }
        } else {
          const size_t prefixLength = strlen(kHistoryDir);
          const size_t nameLength = strlen(base);
          if (prefixLength + 1 + nameLength < sizeof(removePath)) {
            memcpy(removePath, kHistoryDir, prefixLength);
            removePath[prefixLength] = '/';
            memcpy(removePath + prefixLength + 1, base, nameLength + 1);
          }
        }
        if (!keep && removePath[0] != '\0') break;
      }
    }
    if (removePath[0] == '\0') return;
    if (!Storage.remove(removePath)) {
      LOG_ERR("AIRP", "Could not prune history file: %s", removePath);
      return;
    }
    if (removeRelatedCache) {
      char cachePath[kPathBufferSize];
      if (formatPixelCachePath(removePath, cachePath, sizeof(cachePath))) Storage.remove(cachePath);
    }
  }
}

bool AirPageImageStore::selectCurrent(SelectedImage& selected) const {
  const char* path = currentImagePath();
  if (!path || !hasImage()) {
    selected = {};
    return false;
  }
  snprintf(selected.path, sizeof(selected.path), "%s", path);
  selected.image = currentImage_;
  selected.current = true;
  return true;
}

bool AirPageImageStore::selectHistory(const size_t index, SelectedImage& selected) {
  if (index >= historyCount_) return false;
  const HistoryEntry& entry = history_[index];

  char path[kPathBufferSize];
  if (entry.isCurrent()) {
    const char* currentPath = imagePathForFormat(entry.image.format);
    if (!currentPath) return false;
    snprintf(path, sizeof(path), "%s", currentPath);
  } else if (!formatHistoryPath(entry.archiveId, entry.image.format, path, sizeof(path))) {
    return false;
  }

  ImageInfo inspected;
  if (!inspectImage(path, inspected) || inspected.format != entry.image.format) {
    if (!entry.isCurrent()) {
      char cachePath[kPathBufferSize];
      if (formatPixelCachePath(path, cachePath, sizeof(cachePath))) Storage.remove(cachePath);
      Storage.remove(path);
      removeHistoryEntry(entry.archiveId, entry.image.format);
    }
    return false;
  }
  snprintf(selected.path, sizeof(selected.path), "%s", path);
  selected.image = inspected;
  selected.current = entry.isCurrent();
  return true;
}

void AirPageImageStore::commitDisplayedDownload(const uint64_t archiveDateKey) {
  switch (currentImage_.format) {
    case ImageFormat::None:
      return;
    case ImageFormat::Bmp:
      Storage.remove(kPixelCachePath);
      Storage.remove(kJpegImagePath);
      break;
    case ImageFormat::Jpeg:
      Storage.remove(kBmpImagePath);
      break;
  }

  if (historyInitialized_) removeCurrentHistoryEntry();
  if (pendingDisplayValidation_) {
    HistoryEntry archived;
    if (archivePendingBackup(archiveDateKey, &archived)) {
      if (historyInitialized_ && archived.image.format != ImageFormat::None) insertHistoryEntry(archived);
    } else {
      LOG_ERR("AIRP", "Could not archive previous image; backup retained");
    }
    pendingDisplayValidation_ = false;
  } else {
    discardPendingBackups();
  }
  if (historyInitialized_) {
    setCurrentHistoryEntry();
    pruneHistoryFiles();
  }
}

AirPageImageStore::RejectResult AirPageImageStore::rejectDisplayedImage(const SelectedImage& selected) {
  if (selected.current) {
    if (pendingDisplayValidation_ && rollbackPendingImage()) return RejectResult::CurrentRestored;
    return RejectResult::CurrentInvalid;
  }

  char cachePath[kPathBufferSize];
  if (formatPixelCachePath(selected.path, cachePath, sizeof(cachePath))) Storage.remove(cachePath);
  Storage.remove(selected.path);
  for (size_t i = 0; i < historyCount_; ++i) {
    if (history_[i].isCurrent() || history_[i].image.format != selected.image.format) continue;
    char path[kPathBufferSize];
    if (formatHistoryPath(history_[i].archiveId, history_[i].image.format, path, sizeof(path)) &&
        strcmp(path, selected.path) == 0) {
      removeHistoryEntry(history_[i].archiveId, history_[i].image.format);
      break;
    }
  }
  return RejectResult::HistoryInvalid;
}

}  // namespace airpage
