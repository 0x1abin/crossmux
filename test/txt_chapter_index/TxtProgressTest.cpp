#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include "Txt.h"
#include "TxtProgress.h"

namespace {
class TxtProgressTest : public ::testing::Test {
 protected:
  std::filesystem::path root = std::filesystem::temp_directory_path() / "crossmux-txt-progress";
  void SetUp() override {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "old");
    setenv("CROSSPOINT_SIM_SD", root.c_str(), 1);
    ASSERT_TRUE(Storage.begin());
  }
  void TearDown() override {
    unsetenv("CROSSPOINT_SIM_SD");
    std::filesystem::remove_all(root);
  }
  void write(const char* name, const std::vector<uint8_t>& bytes) {
    std::ofstream out(root / name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  static void u32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (int n = 0; n < 4; ++n) bytes.push_back(value >> (n * 8));
  }
};

TEST_F(TxtProgressTest, ReadsLegacySourceAndDoesNotChangeProgressOnFailure) {
  std::vector<uint8_t> bytes;
  bytes.reserve(8);
  u32(bytes, 0x4F545854);
  u32(bytes, 8123);
  write("old/progress.bin", bytes);
  uint32_t offset = 0;
  EXPECT_EQ(txt_progress::readLegacySource("/old", 20000, offset), txt_progress::LegacyResult::Restored);
  EXPECT_EQ(offset, 8123U);
  EXPECT_EQ(txt_progress::readLegacySource("/old", 8000, offset), txt_progress::LegacyResult::Failed);
  std::ifstream original(root / "old/progress.bin", std::ios::binary);
  std::vector<uint8_t> reread(std::istreambuf_iterator<char>(original), {});
  EXPECT_EQ(reread, bytes);
}

TEST_F(TxtProgressTest, ReadsOneLegacyPageForEverySupportedLayout) {
  for (uint8_t version = 4; version <= 8; ++version) {
    std::vector<uint8_t> progress;
    progress.reserve(4);
    u32(progress, 1);
    write("old/progress.bin", progress);
    std::vector<uint8_t> index;
    index.reserve(64);
    u32(index, 0x54585449);
    index.push_back(version);
    u32(index, 1000);
    for (int n = 0; n < 4; ++n) u32(index, 0);
    index.push_back(0);
    if (version >= 7) index.push_back(1);
    if (version >= 5) index.push_back(1);
    if (version >= 6) index.push_back(1);
    u32(index, 3);
    u32(index, 0);
    u32(index, 250);
    u32(index, 800);
    write("old/index.bin", index);
    uint32_t offset = 0;
    EXPECT_EQ(txt_progress::readLegacySource("/old", 1000, offset), txt_progress::LegacyResult::Restored);
    EXPECT_EQ(offset, 250U);
    index.pop_back();
    write("old/index.bin", index);
    EXPECT_EQ(txt_progress::readLegacySource("/old", 1000, offset), txt_progress::LegacyResult::Failed);
  }
}

TEST_F(TxtProgressTest, ResolvesAsciiGbkAndUtf8SpansIncludingMidCharacterAndEnd) {
  const txt_progress::Header header{txt_progress::MAGIC, 29, 4, 12, txt_progress::VERSION, 1};
  const txt_progress::Record records[] = {{0, 1, 1, 1}, {10, 11, 2, 1}, {20, 16, 3, 1}, {29, 19, 0, 0}};
  std::ofstream output(root / "map", std::ios::binary);
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  output.write(reinterpret_cast<const char*>(records), sizeof(records));
  output.close();
  HalFile mapping;
  ASSERT_TRUE(Storage.openFileForRead("TXT", "/map", mapping));
  for (const auto& [source, expected] :
       {std::pair{0U, 1U}, {9U, 10U}, {14U, 13U}, {15U, 13U}, {23U, 17U}, {29U, 19U}}) {
    uint32_t visible = 0;
    EXPECT_TRUE(txt_progress::resolve(mapping, 29, source, visible));
    EXPECT_EQ(visible, expected);
  }
  for (const auto& [visible, expected] : {std::pair{1U, 0U}, {10U, 9U}, {13U, 14U}, {17U, 23U}, {19U, 29U}}) {
    uint32_t source = 0;
    EXPECT_TRUE(txt_progress::sourceForVisible(mapping, 29, visible, source));
    EXPECT_EQ(source, expected);
  }
  uint32_t source = 0;
  EXPECT_FALSE(txt_progress::sourceForVisible(mapping, 29, 0, source));
  EXPECT_FALSE(txt_progress::sourceForVisible(mapping, 29, 20, source));
  uint32_t visible = 0;
  EXPECT_FALSE(txt_progress::resolve(mapping, 30, 1, visible));
  EXPECT_FALSE(txt_progress::resolve(mapping, 29, 30, visible));
  mapping.close();
  std::filesystem::resize_file(root / "map", sizeof(header) + 1);
  ASSERT_TRUE(Storage.openFileForRead("TXT", "/map", mapping));
  EXPECT_FALSE(txt_progress::resolve(mapping, 29, 1, visible));
}
TEST_F(TxtProgressTest, FindsTheCurrentConvertedChapterWithoutRetainingChapterTables) {
  const std::string book = "Preface\nChapter 1\nbody\nChapter 2\nend\n";
  {
    std::ofstream output(root / "book.txt", std::ios::binary);
    output << book;
  }
  Txt txt("/book.txt", "/.crosspoint");
  ASSERT_TRUE(txt.load());
  std::array<uint8_t, 8192> scratch{};
  uint32_t count = 0;
  auto encoding = txt_encoding::Encoding::Unknown;
  ASSERT_TRUE(txt.buildChapterIndex(encoding, scratch.data(), scratch.size(), count));
  ASSERT_EQ(count, 2U);
  std::filesystem::create_directories(root / "converted");
  const txt_progress::Header header{
      txt_progress::MAGIC, static_cast<uint32_t>(book.size()), 2, 12, txt_progress::VERSION, 1};
  const txt_progress::Record records[] = {
      {0, 1, 1, 1}, {static_cast<uint32_t>(book.size()), static_cast<uint32_t>(book.size() + 1), 0, 0}};
  {
    std::ofstream output(root / "converted/txt-map.bin", std::ios::binary);
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(records), sizeof(records));
  }
  EXPECT_EQ(Txt::tocIndexForPosition("/book.txt", "/converted", 1), 0);
  EXPECT_EQ(Txt::tocIndexForPosition("/book.txt", "/converted", book.find("Chapter 1") + 1), 1);
  EXPECT_EQ(Txt::tocIndexForPosition("/book.txt", "/converted", book.find("Chapter 2") + 1), 2);
  EXPECT_EQ(Txt::tocIndexForPosition("/book.txt", "/converted", book.size() + 1), 2);
  uint8_t progress = 255;
  EXPECT_EQ(Txt::tocIndexForPosition("/book.txt", "/converted", book.find("Chapter 2") + 1, &progress), 2);
  EXPECT_EQ(progress, 0);
  EXPECT_EQ(Txt::tocIndexForPosition("/book.txt", "/converted", book.size() + 1, &progress), 2);
  EXPECT_EQ(progress, 100);
  std::filesystem::resize_file(root / "converted/txt-map.bin", sizeof(header));
  EXPECT_EQ(Txt::tocIndexForPosition("/book.txt", "/converted", 1), -1);
}
}  // namespace
