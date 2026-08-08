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

#include "WoodfishStore.h"

namespace {

constexpr char kSavePath[] = "/.crosspoint/woodfish.bin";
constexpr char kTempPath[] = "/.crosspoint/woodfish.bin.tmp";
constexpr char kBackupPath[] = "/.crosspoint/woodfish.bin.bak";

std::array<uint8_t, 12> recordFor(const uint32_t total) {
  return {'W',
          'D',
          'F',
          '1',
          static_cast<uint8_t>(total),
          static_cast<uint8_t>(total >> 8),
          static_cast<uint8_t>(total >> 16),
          static_cast<uint8_t>(total >> 24),
          static_cast<uint8_t>(~total),
          static_cast<uint8_t>(~total >> 8),
          static_cast<uint8_t>(~total >> 16),
          static_cast<uint8_t>(~total >> 24)};
}

class WoodfishStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<unsigned> serial{0};
    root_ = std::filesystem::temp_directory_path() / ("crossmux-woodfish-store-" + std::to_string(serial.fetch_add(1)));
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

  template <typename Bytes>
  void writeBytes(const char* path, const Bytes& bytes) {
    const auto target = hostPath(path);
    std::filesystem::create_directories(target.parent_path());
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(output.good());
  }

  std::filesystem::path root_;
};

TEST_F(WoodfishStoreTest, MissingAndMalformedRecordsStartAtZero) {
  uint32_t total = 99;
  EXPECT_FALSE(WoodfishStore::load(total));
  EXPECT_EQ(total, 0U);

  writeBytes(kSavePath, std::vector<uint8_t>{'W', 'D', 'F'});
  total = 99;
  EXPECT_FALSE(WoodfishStore::load(total));
  EXPECT_EQ(total, 0U);

  auto record = recordFor(123u);
  record[0] = 'X';
  writeBytes(kSavePath, record);
  EXPECT_FALSE(WoodfishStore::load(total));

  record = recordFor(123u);
  record[8] ^= 1u;
  writeBytes(kSavePath, record);
  EXPECT_FALSE(WoodfishStore::load(total));
}

TEST_F(WoodfishStoreTest, LoadsMaximumCounterValue) {
  writeBytes(kSavePath, recordFor(UINT32_MAX));
  uint32_t total = 0;
  ASSERT_TRUE(WoodfishStore::load(total));
  EXPECT_EQ(total, UINT32_MAX);
}

TEST_F(WoodfishStoreTest, SavesAndReplacesARecordWithoutLeavingWorkFiles) {
  ASSERT_TRUE(WoodfishStore::save(123u));
  ASSERT_TRUE(WoodfishStore::save(456u));

  uint32_t total = 0;
  ASSERT_TRUE(WoodfishStore::load(total));
  EXPECT_EQ(total, 456U);
  EXPECT_EQ(std::filesystem::file_size(hostPath(kSavePath)), 12U);
  EXPECT_FALSE(Storage.exists(kTempPath));
  EXPECT_FALSE(Storage.exists(kBackupPath));
}

TEST_F(WoodfishStoreTest, RecoversAValidBackupWhenTheCanonicalRecordIsInvalid) {
  writeBytes(kSavePath, std::vector<uint8_t>{0x00});
  writeBytes(kBackupPath, recordFor(987654321u));

  uint32_t total = 0;
  ASSERT_TRUE(WoodfishStore::load(total));
  EXPECT_EQ(total, 987654321U);
  EXPECT_TRUE(Storage.exists(kSavePath));
  EXPECT_FALSE(Storage.exists(kBackupPath));
}

TEST_F(WoodfishStoreTest, KeepsTheCanonicalRecordWhenBackupPreparationFails) {
  ASSERT_TRUE(WoodfishStore::save(42u));
  const auto blocker = hostPath(kBackupPath) / "blocker";
  ASSERT_TRUE(std::filesystem::create_directories(blocker));

  EXPECT_FALSE(WoodfishStore::save(84u));
  uint32_t total = 0;
  ASSERT_TRUE(WoodfishStore::load(total));
  EXPECT_EQ(total, 42U);
}

}  // namespace
