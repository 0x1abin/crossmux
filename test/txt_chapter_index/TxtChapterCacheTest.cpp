#include <HalStorage.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Txt.h"

namespace {

class TxtChapterCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<unsigned> serial{0};
    root_ = std::filesystem::temp_directory_path() / ("crossmux-txt-chapters-" + std::to_string(serial++));
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

  std::filesystem::path hostPath(std::string path) const {
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    return root_ / path;
  }

  void writeBook(const std::string& contents) {
    std::ofstream output(hostPath("/book.txt"), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    ASSERT_TRUE(output.good());
  }

  std::filesystem::path root_;
};

TEST_F(TxtChapterCacheTest, BuildsReusesAndInvalidatesTheFixedRecordCache) {
  const std::string contents = "Preface\nChapter 1: Start\nbody\nPart II - End\n";
  writeBook(contents);

  Txt txt("/book.txt", "/.crosspoint");
  ASSERT_TRUE(txt.load());
  std::array<uint8_t, 8193> scratch{};
  txt_encoding::Encoding encoding = txt_encoding::Encoding::Unknown;
  uint32_t count = 0;
  ASSERT_TRUE(txt.buildChapterIndex(encoding, scratch.data(), scratch.size(), count));
  ASSERT_EQ(count, 2U);

  HalFile cache;
  encoding = txt_encoding::Encoding::Unknown;
  ASSERT_TRUE(txt.openChapterIndex(cache, encoding, count));
  EXPECT_EQ(encoding, txt_encoding::Encoding::Unknown);
  txt_chapter_index::Record first;
  ASSERT_TRUE(txt.readChapter(cache, count, 0, first));
  EXPECT_STREQ(first.title, "Chapter 1: Start");
  EXPECT_EQ(first.sourceOffset, contents.find("Chapter 1"));
  cache.close();

  const auto cachePath = hostPath(txt.getCachePath() + "/chapters.bin");
  ASSERT_TRUE(std::filesystem::exists(cachePath));
  std::filesystem::resize_file(cachePath, 17);
  HalFile truncated;
  EXPECT_FALSE(txt.openChapterIndex(truncated, encoding, count));
  truncated.close();

  ASSERT_TRUE(txt.buildChapterIndex(encoding, scratch.data(), scratch.size(), count));
  {
    std::fstream damaged(cachePath, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(damaged.good());
    damaged.seekp(20);  // Header plus the first record's source offset.
    damaged.put('x');
  }
  HalFile damaged;
  EXPECT_FALSE(txt.openChapterIndex(damaged, encoding, count));
  damaged.close();

  ASSERT_TRUE(txt.buildChapterIndex(encoding, scratch.data(), scratch.size(), count));
  writeBook(contents + "Chapter 3\n");
  Txt changed("/book.txt", "/.crosspoint");
  ASSERT_TRUE(changed.load());
  HalFile stale;
  EXPECT_FALSE(changed.openChapterIndex(stale, encoding, count));
}

TEST_F(TxtChapterCacheTest, CachesAnEmptyChapterList) {
  writeBook("plain text without headings\n");
  Txt txt("/book.txt", "/.crosspoint");
  ASSERT_TRUE(txt.load());
  std::array<uint8_t, 8193> scratch{};
  txt_encoding::Encoding encoding = txt_encoding::Encoding::Unknown;
  uint32_t count = 99;
  ASSERT_TRUE(txt.buildChapterIndex(encoding, scratch.data(), scratch.size(), count));
  EXPECT_EQ(count, 0U);

  HalFile cache;
  ASSERT_TRUE(txt.openChapterIndex(cache, encoding, count));
  EXPECT_EQ(count, 0U);
}

TEST_F(TxtChapterCacheTest, DetectsAndCachesGbkChapterTitles) {
  const std::string gbkBook = "ASCII preface\n\xB5\xDA\xD2\xBB\xD5\xC2 \xBF\xAA\xCA\xBC\n";
  writeBook(gbkBook);
  Txt txt("/book.txt", "/.crosspoint");
  ASSERT_TRUE(txt.load());

  std::array<uint8_t, 8193> scratch{};
  txt_encoding::Encoding encoding = txt_encoding::Encoding::Unknown;
  uint32_t count = 0;
  ASSERT_TRUE(txt.buildChapterIndex(encoding, scratch.data(), scratch.size(), count));
  ASSERT_EQ(encoding, txt_encoding::Encoding::Gbk);
  ASSERT_EQ(count, 1U);

  HalFile cache;
  ASSERT_TRUE(txt.openChapterIndex(cache, encoding, count));
  txt_chapter_index::Record chapter;
  ASSERT_TRUE(txt.readChapter(cache, count, 0, chapter));
  EXPECT_STREQ(chapter.title, "第一章 开始");
  EXPECT_EQ(chapter.sourceOffset, gbkBook.find("\xB5\xDA"));
}

}  // namespace
