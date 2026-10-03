#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "AirPageImageStore.h"

namespace {

using airpage::AirPageImageStore;
using airpage::ImageFormat;
using airpage::SelectedImage;

constexpr uint64_t kArchiveDateKey = 20260730123456u;

class AirPageImageStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<unsigned> serial{0};
    root_ = std::filesystem::temp_directory_path() / ("crossmux-airpage-store-" + std::to_string(serial.fetch_add(1)));
    std::error_code error;
    std::filesystem::remove_all(root_, error);
    ASSERT_TRUE(std::filesystem::create_directories(root_, error));
    ASSERT_FALSE(error);
    ASSERT_EQ(setenv("CROSSPOINT_SIM_SD", root_.c_str(), 1), 0);
    ASSERT_TRUE(Storage.begin());
  }

  void TearDown() override {
    unsetenv("CROSSPOINT_SIM_SD");
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  std::filesystem::path hostPath(const char* sdPath) const {
    while (*sdPath == '/') ++sdPath;
    return root_ / sdPath;
  }

  void writeBytes(const char* path, const std::vector<uint8_t>& bytes) {
    const auto target = hostPath(path);
    std::filesystem::create_directories(target.parent_path());
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
  }

  void writeBmp(const char* path, const uint8_t pixel = 0x00) {
    // 1x1, 1-bit BMP: 14-byte file header, 40-byte DIB, 8-byte palette,
    // and one four-byte-aligned pixel row.
    std::vector<uint8_t> bytes(66, 0);
    bytes[0] = 'B';
    bytes[1] = 'M';
    bytes[2] = 66;
    bytes[10] = 62;
    bytes[14] = 40;
    bytes[18] = 1;
    bytes[22] = 1;
    bytes[26] = 1;
    bytes[28] = 1;
    bytes[34] = 4;
    bytes[46] = 2;
    bytes[58] = 0xFF;
    bytes[59] = 0xFF;
    bytes[60] = 0xFF;
    bytes[62] = pixel;
    writeBytes(path, bytes);
  }

  void writeJpegHeader(const char* path, const uint16_t width = 2, const uint16_t height = 3) {
    writeBytes(path, {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x0B, 0x08, static_cast<uint8_t>(height >> 8),
                      static_cast<uint8_t>(height), static_cast<uint8_t>(width >> 8), static_cast<uint8_t>(width)});
  }

  void writePixelCache(const char* path, const uint16_t width = 2, const uint16_t height = 3) {
    std::vector<uint8_t> bytes(4 + height, 0x55);
    bytes[0] = static_cast<uint8_t>(width);
    bytes[1] = static_cast<uint8_t>(width >> 8);
    bytes[2] = static_cast<uint8_t>(height);
    bytes[3] = static_cast<uint8_t>(height >> 8);
    writeBytes(path, bytes);
  }

  std::filesystem::path root_;
};

TEST_F(AirPageImageStoreTest, DistinguishesEmptyInvalidAndValidCaches) {
  AirPageImageStore store;
  EXPECT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Empty);

  writeBytes("/AirPage/latest.bmp", {'B', 'M', 0});
  EXPECT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Invalid);

  writeBmp("/AirPage/latest.bmp");
  EXPECT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  ASSERT_EQ(store.historyCount(), 1U);
  EXPECT_TRUE(store.historyEntry(0).isCurrent());
  EXPECT_EQ(store.historyEntry(0).image.format, ImageFormat::Bmp);
}

TEST_F(AirPageImageStoreTest, RejectsTruncatedBmpAndJpegHeaders) {
  airpage::ImageInfo info;
  writeBytes("/truncated.bmp", {'B', 'M', 0});
  writeBytes("/truncated.jpg", {0xFF, 0xD8, 0xFF});
  EXPECT_FALSE(AirPageImageStore::inspectImage("/truncated.bmp", info));
  EXPECT_FALSE(AirPageImageStore::inspectImage("/truncated.jpg", info));
}

TEST_F(AirPageImageStoreTest, DeduplicatesAnIdenticalDownload) {
  writeBmp("/AirPage/latest.bmp", 0x80);
  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  writeBmp(AirPageImageStore::kDownloadPartPath, 0x80);

  EXPECT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::Unchanged);
  EXPECT_FALSE(Storage.exists(AirPageImageStore::kDownloadPartPath));
  EXPECT_FALSE(store.hasPendingDownload());
  EXPECT_EQ(store.historyCount(), 1U);
}

TEST_F(AirPageImageStoreTest, CommitsAFormatChangeAndArchivesThePreviousImage) {
  writeBmp("/AirPage/latest.bmp");
  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);

  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  ASSERT_TRUE(store.hasPendingDownload());
  SelectedImage selected;
  ASSERT_TRUE(store.selectCurrent(selected));
  EXPECT_EQ(selected.image.format, ImageFormat::Jpeg);

  store.commitDisplayedDownload(kArchiveDateKey);
  EXPECT_FALSE(store.hasPendingDownload());
  ASSERT_EQ(store.historyCount(), 2U);
  EXPECT_TRUE(store.historyEntry(0).isCurrent());
  EXPECT_EQ(store.historyEntry(0).image.format, ImageFormat::Jpeg);
  EXPECT_EQ(store.historyEntry(1).image.format, ImageFormat::Bmp);
  EXPECT_TRUE(Storage.exists("/AirPage/20260730_123456.bmp"));
}

TEST_F(AirPageImageStoreTest, RollsBackARejectedDownloadedImage) {
  writeBmp("/AirPage/latest.bmp");
  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);

  SelectedImage selected;
  ASSERT_TRUE(store.selectCurrent(selected));
  EXPECT_EQ(store.rejectDisplayedImage(selected), AirPageImageStore::RejectResult::CurrentRestored);
  ASSERT_TRUE(store.selectCurrent(selected));
  EXPECT_EQ(selected.image.format, ImageFormat::Bmp);
  EXPECT_FALSE(store.hasPendingDownload());
  EXPECT_TRUE(Storage.exists("/AirPage/latest.bmp"));
  EXPECT_FALSE(Storage.exists("/AirPage/latest.jpg"));
}

TEST_F(AirPageImageStoreTest, RecoversACompletedPartAndCommitsItAfterDisplay) {
  writeBmp("/AirPage/latest.bmp");
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  EXPECT_TRUE(store.hasPendingDownload());
  EXPECT_EQ(store.currentImage().format, ImageFormat::Jpeg);

  store.commitDisplayedDownload(kArchiveDateKey);
  EXPECT_FALSE(store.hasPendingDownload());
  EXPECT_TRUE(Storage.exists("/AirPage/20260730_123456.bmp"));
  EXPECT_EQ(store.historyCount(), 2U);
}

TEST_F(AirPageImageStoreTest, RecoversAndArchivesABackupWithoutCollidingWithHistory) {
  writeBmp("/AirPage/latest.bmp");
  writeJpegHeader("/AirPage/latest.jpg.bak");
  writeBmp("/AirPage/00000001.bmp", 0x80);

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(kArchiveDateKey), AirPageImageStore::InitializationResult::Ready);
  EXPECT_TRUE(Storage.exists("/AirPage/20260730_123456.jpg"));
  ASSERT_EQ(store.historyCount(), 3U);
  EXPECT_EQ(store.historyEntry(1).archiveId, kArchiveDateKey * 100u);
  EXPECT_EQ(store.historyEntry(2).archiveId, 1U);
}

TEST_F(AirPageImageStoreTest, PagesAllImagesWithoutDeletingOldFiles) {
  writeBmp("/AirPage/latest.bmp");
  for (unsigned sequence = 1; sequence <= 45; ++sequence) {
    char path[96];
    snprintf(path, sizeof(path), "/AirPage/%08u.bmp", sequence);
    writeBmp(path, static_cast<uint8_t>(sequence));
  }

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  ASSERT_EQ(store.historyCount(), AirPageImageStore::kHistoryPageSize);
  EXPECT_TRUE(store.historyEntry(0).isCurrent());
  EXPECT_EQ(store.historyEntry(1).archiveId, 45U);
  EXPECT_EQ(store.historyEntry(19).archiveId, 27U);
  EXPECT_TRUE(Storage.exists("/AirPage/00000001.bmp"));
  EXPECT_FALSE(store.hasPreviousHistoryPage());
  ASSERT_TRUE(store.nextHistoryPage());
  ASSERT_EQ(store.historyCount(), 20U);
  EXPECT_EQ(store.historyEntry(0).archiveId, 26U);
  EXPECT_EQ(store.historyEntry(19).archiveId, 7U);
  ASSERT_TRUE(store.nextHistoryPage());
  ASSERT_EQ(store.historyCount(), 6U);
  EXPECT_EQ(store.historyEntry(0).archiveId, 6U);
  EXPECT_EQ(store.historyEntry(5).archiveId, 1U);
  EXPECT_FALSE(store.nextHistoryPage());
  ASSERT_TRUE(store.previousHistoryPage());
  EXPECT_EQ(store.historyEntry(0).archiveId, 26U);
  EXPECT_EQ(store.historyEntry(19).archiveId, 7U);
  ASSERT_TRUE(store.previousHistoryPage());
  EXPECT_TRUE(store.historyEntry(0).isCurrent());
  EXPECT_FALSE(store.previousHistoryPage());
  AirPageImageStore reopened;
  EXPECT_EQ(reopened.initialize(), AirPageImageStore::InitializationResult::Ready);
  for (unsigned sequence = 1; sequence <= 45; ++sequence) {
    char path[96];
    snprintf(path, sizeof(path), "/AirPage/%08u.bmp", sequence);
    EXPECT_TRUE(Storage.exists(path));
  }
}

TEST_F(AirPageImageStoreTest, DropsAHistoryEntryThatBecomesInvalidAfterInitialization) {
  writeBmp("/AirPage/latest.bmp");
  writeBmp("/AirPage/00000001.bmp");

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  ASSERT_EQ(store.historyCount(), 2U);
  ASSERT_TRUE(std::filesystem::remove(hostPath("/AirPage/00000001.bmp")));

  SelectedImage selected;
  EXPECT_FALSE(store.selectHistory(1, selected));
  EXPECT_EQ(store.historyCount(), 1U);
}

TEST_F(AirPageImageStoreTest, ArchivesJpegPixelCacheAndKeepsUnrelatedFiles) {
  writeJpegHeader("/AirPage/latest.jpg");
  writePixelCache("/AirPage/latest.pxc");
  writePixelCache("/AirPage/00000009.pxc");

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  EXPECT_TRUE(Storage.exists("/AirPage/00000009.pxc"));

  writeBmp(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  store.commitDisplayedDownload(kArchiveDateKey);
  EXPECT_TRUE(Storage.exists("/AirPage/20260730_123456.jpg"));
  EXPECT_TRUE(Storage.exists("/AirPage/20260730_123456.pxc"));
}

TEST_F(AirPageImageStoreTest, AddsASuffixWhenTwoArchivesShareTheSameSecond) {
  writeBmp("/AirPage/latest.bmp");
  writeBmp("/AirPage/20260730_123456.bmp", 0x80);

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  store.commitDisplayedDownload(kArchiveDateKey);

  EXPECT_TRUE(Storage.exists("/AirPage/20260730_123456-01.bmp"));
  ASSERT_EQ(store.historyCount(), 3U);
  EXPECT_EQ(store.historyEntry(1).archiveId, kArchiveDateKey * 100u + 1u);
  EXPECT_EQ(store.historyEntry(2).archiveId, kArchiveDateKey * 100u);
}

}  // namespace

TEST_F(AirPageImageStoreTest, PendingDownloadCanBeRetriedFromHistoryWithoutLosingBackup) {
  writeBmp("/AirPage/latest.bmp");
  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);

  // A transient render failure leaves the staged image untouched. Reopening
  // history must select the JPEG, not the previous BMP's stale list entry.
  ASSERT_EQ(store.historyCount(), 1U);
  SelectedImage retry;
  ASSERT_TRUE(store.selectHistory(0, retry));
  EXPECT_TRUE(retry.current);
  EXPECT_EQ(retry.image.format, ImageFormat::Jpeg);
  EXPECT_TRUE(store.hasPendingDownload());
  EXPECT_TRUE(Storage.exists("/AirPage/latest.jpg"));
  EXPECT_TRUE(Storage.exists("/AirPage/latest.bmp.bak"));
  store.commitDisplayedDownload(kArchiveDateKey);
  EXPECT_FALSE(store.hasPendingDownload());
  ASSERT_EQ(store.historyCount(), 2U);
  ASSERT_TRUE(store.selectHistory(1, retry));
  EXPECT_EQ(retry.image.format, ImageFormat::Bmp);
  EXPECT_TRUE(Storage.exists(retry.path));
}

TEST_F(AirPageImageStoreTest, LaterPushRecoversBackupAfterAnUnrenderedJpeg) {
  writeBmp("/AirPage/latest.bmp");
  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  writeJpegHeader(AirPageImageStore::kDownloadPartPath, 4, 5);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  SelectedImage selected;
  ASSERT_TRUE(store.selectHistory(0, selected));
  EXPECT_EQ(selected.image.width, 4);
  EXPECT_TRUE(Storage.exists("/AirPage/latest.bmp.bak"));
  store.commitDisplayedDownload(kArchiveDateKey);
  ASSERT_EQ(store.historyCount(), 2U);
  EXPECT_TRUE(store.selectHistory(1, selected));
  EXPECT_EQ(selected.image.format, ImageFormat::Bmp);
}

TEST_F(AirPageImageStoreTest, LeavesOldDirectoryAndUnknownRootFilesUntouched) {
  writeBmp("/.crosspoint/airpage/latest.bmp");
  writeBmp("/.crosspoint/airpage/history/00000001.bmp");
  writeBmp("/AirPage/holiday.bmp");
  writeBytes("/AirPage/readme.txt", {'h', 'i'});
  AirPageImageStore store;
  EXPECT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Empty);
  EXPECT_EQ(store.historyCount(), 0U);
  writeBmp(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  store.commitDisplayedDownload();
  EXPECT_TRUE(Storage.exists("/.crosspoint/airpage/latest.bmp"));
  EXPECT_TRUE(Storage.exists("/.crosspoint/airpage/history/00000001.bmp"));
  EXPECT_TRUE(Storage.exists("/AirPage/holiday.bmp"));
  EXPECT_TRUE(Storage.exists("/AirPage/readme.txt"));
}

TEST_F(AirPageImageStoreTest, PagesBothFormatsWithTheSameArchiveId) {
  for (unsigned sequence = 1; sequence <= 31; ++sequence) {
    char path[96];
    snprintf(path, sizeof(path), "/AirPage/%08u.bmp", sequence);
    writeBmp(path);
    snprintf(path, sizeof(path), "/AirPage/%08u.jpg", sequence);
    writeJpegHeader(path);
  }
  AirPageImageStore store;
  store.initialize();
  unsigned remaining = 62;
  do {
    for (size_t i = 0; i < store.historyCount(); ++i) {
      const auto& entry = store.historyEntry(i);
      EXPECT_EQ(entry.archiveId, (remaining + 1) / 2);
      EXPECT_EQ(entry.image.format, remaining % 2 == 0 ? ImageFormat::Jpeg : ImageFormat::Bmp);
      SelectedImage selected;
      EXPECT_TRUE(store.selectHistory(i, selected));
      --remaining;
    }
  } while (store.nextHistoryPage());
  EXPECT_EQ(remaining, 0U);
  store.firstHistoryPage();
  EXPECT_EQ(store.historyEntry(0).archiveId, 31U);
}

TEST_F(AirPageImageStoreTest, SequenceAllocationScansBeyondTheLoadedPage) {
  writeBmp("/AirPage/latest.bmp");
  for (unsigned sequence = 1; sequence <= 25; ++sequence) {
    char path[96];
    snprintf(path, sizeof(path), "/AirPage/%08u.bmp", sequence);
    writeBmp(path);
  }
  AirPageImageStore store;
  store.initialize();
  ASSERT_TRUE(store.nextHistoryPage());
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  store.commitDisplayedDownload();
  EXPECT_TRUE(Storage.exists("/AirPage/00000026.bmp"));
  EXPECT_TRUE(Storage.exists("/AirPage/00000025.bmp"));
}

TEST_F(AirPageImageStoreTest, ArchiveFailureRetainsBackupAndAllExistingImages) {
  writeBmp("/AirPage/latest.bmp");
  for (unsigned collision = 0; collision <= 99; ++collision) {
    char path[96];
    if (collision == 0)
      snprintf(path, sizeof(path), "/AirPage/20260730_123456.pxc");
    else
      snprintf(path, sizeof(path), "/AirPage/20260730_123456-%02u.pxc", collision);
    writeBytes(path, {0});
  }
  AirPageImageStore store;
  store.initialize();
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  store.commitDisplayedDownload(kArchiveDateKey);
  EXPECT_TRUE(Storage.exists("/AirPage/latest.bmp.bak"));
  EXPECT_TRUE(Storage.exists("/AirPage/latest.jpg"));
  EXPECT_TRUE(Storage.exists("/AirPage/20260730_123456.pxc"));
  EXPECT_TRUE(Storage.exists("/AirPage/20260730_123456-99.pxc"));
}

TEST_F(AirPageImageStoreTest, SequenceWrapDoesNotOverwriteAnOrphanPixelCache) {
  writeJpegHeader("/AirPage/latest.jpg");
  writeBmp("/AirPage/99999999.bmp");
  writePixelCache("/AirPage/00000001.pxc");
  AirPageImageStore store;
  store.initialize();
  writeBmp(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  store.commitDisplayedDownload();
  EXPECT_TRUE(Storage.exists("/AirPage/00000002.jpg"));
  EXPECT_TRUE(Storage.exists("/AirPage/00000001.pxc"));
}
