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

  writeBytes("/.crosspoint/airpage/latest.bmp", {'B', 'M', 0});
  EXPECT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Invalid);

  writeBmp("/.crosspoint/airpage/latest.bmp");
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
  writeBmp("/.crosspoint/airpage/latest.bmp", 0x80);
  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  writeBmp(AirPageImageStore::kDownloadPartPath, 0x80);

  EXPECT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::Unchanged);
  EXPECT_FALSE(Storage.exists(AirPageImageStore::kDownloadPartPath));
  EXPECT_FALSE(store.hasPendingDownload());
  EXPECT_EQ(store.historyCount(), 1U);
}

TEST_F(AirPageImageStoreTest, CommitsAFormatChangeAndArchivesThePreviousImage) {
  writeBmp("/.crosspoint/airpage/latest.bmp");
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
  EXPECT_TRUE(Storage.exists("/.crosspoint/airpage/history/20260730_123456.bmp"));
}

TEST_F(AirPageImageStoreTest, RollsBackARejectedDownloadedImage) {
  writeBmp("/.crosspoint/airpage/latest.bmp");
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
  EXPECT_TRUE(Storage.exists("/.crosspoint/airpage/latest.bmp"));
  EXPECT_FALSE(Storage.exists("/.crosspoint/airpage/latest.jpg"));
}

TEST_F(AirPageImageStoreTest, RecoversACompletedPartAndCommitsItAfterDisplay) {
  writeBmp("/.crosspoint/airpage/latest.bmp");
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  EXPECT_TRUE(store.hasPendingDownload());
  EXPECT_EQ(store.currentImage().format, ImageFormat::Jpeg);

  store.commitDisplayedDownload(kArchiveDateKey);
  EXPECT_FALSE(store.hasPendingDownload());
  EXPECT_TRUE(Storage.exists("/.crosspoint/airpage/history/20260730_123456.bmp"));
  EXPECT_EQ(store.historyCount(), 2U);
}

TEST_F(AirPageImageStoreTest, RecoversAndArchivesABackupWithoutCollidingWithHistory) {
  writeBmp("/.crosspoint/airpage/latest.bmp");
  writeJpegHeader("/.crosspoint/airpage/latest.jpg.bak");
  writeBmp("/.crosspoint/airpage/history/00000001.bmp", 0x80);

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(kArchiveDateKey), AirPageImageStore::InitializationResult::Ready);
  EXPECT_TRUE(Storage.exists("/.crosspoint/airpage/history/20260730_123456.jpg"));
  ASSERT_EQ(store.historyCount(), 3U);
  EXPECT_EQ(store.historyEntry(1).archiveId, kArchiveDateKey * 100u);
  EXPECT_EQ(store.historyEntry(2).archiveId, 1U);
}

TEST_F(AirPageImageStoreTest, KeepsTwentyNewestImagesIncludingCurrent) {
  writeBmp("/.crosspoint/airpage/latest.bmp");
  for (unsigned sequence = 1; sequence <= 20; ++sequence) {
    char path[96];
    snprintf(path, sizeof(path), "/.crosspoint/airpage/history/%08u.bmp", sequence);
    writeBmp(path, static_cast<uint8_t>(sequence));
  }

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  ASSERT_EQ(store.historyCount(), AirPageImageStore::kMaxHistoryEntries);
  EXPECT_TRUE(store.historyEntry(0).isCurrent());
  EXPECT_EQ(store.historyEntry(1).archiveId, 20U);
  EXPECT_EQ(store.historyEntry(19).archiveId, 2U);
  EXPECT_FALSE(Storage.exists("/.crosspoint/airpage/history/00000001.bmp"));
}

TEST_F(AirPageImageStoreTest, DropsAHistoryEntryThatBecomesInvalidAfterInitialization) {
  writeBmp("/.crosspoint/airpage/latest.bmp");
  writeBmp("/.crosspoint/airpage/history/00000001.bmp");

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  ASSERT_EQ(store.historyCount(), 2U);
  ASSERT_TRUE(std::filesystem::remove(hostPath("/.crosspoint/airpage/history/00000001.bmp")));

  SelectedImage selected;
  EXPECT_FALSE(store.selectHistory(1, selected));
  EXPECT_EQ(store.historyCount(), 1U);
}

TEST_F(AirPageImageStoreTest, ArchivesJpegPixelCacheAndRemovesOrphans) {
  writeJpegHeader("/.crosspoint/airpage/latest.jpg");
  writePixelCache("/.crosspoint/airpage/latest.pxc");
  writePixelCache("/.crosspoint/airpage/history/00000009.pxc");

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  EXPECT_FALSE(Storage.exists("/.crosspoint/airpage/history/00000009.pxc"));

  writeBmp(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  store.commitDisplayedDownload(kArchiveDateKey);
  EXPECT_TRUE(Storage.exists("/.crosspoint/airpage/history/20260730_123456.jpg"));
  EXPECT_TRUE(Storage.exists("/.crosspoint/airpage/history/20260730_123456.pxc"));
}

TEST_F(AirPageImageStoreTest, AddsASuffixWhenTwoArchivesShareTheSameSecond) {
  writeBmp("/.crosspoint/airpage/latest.bmp");
  writeBmp("/.crosspoint/airpage/history/20260730_123456.bmp", 0x80);

  AirPageImageStore store;
  ASSERT_EQ(store.initialize(), AirPageImageStore::InitializationResult::Ready);
  writeJpegHeader(AirPageImageStore::kDownloadPartPath);
  ASSERT_EQ(store.stageDownloadedImage(), AirPageImageStore::StageResult::PendingDisplay);
  store.commitDisplayedDownload(kArchiveDateKey);

  EXPECT_TRUE(Storage.exists("/.crosspoint/airpage/history/20260730_123456-01.bmp"));
  ASSERT_EQ(store.historyCount(), 3U);
  EXPECT_EQ(store.historyEntry(1).archiveId, kArchiveDateKey * 100u + 1u);
  EXPECT_EQ(store.historyEntry(2).archiveId, kArchiveDateKey * 100u);
}

}  // namespace
