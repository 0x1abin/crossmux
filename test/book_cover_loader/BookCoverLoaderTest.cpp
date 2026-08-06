#include <HalStorage.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "BookCoverLoader.h"
#include "CoverStub.h"

namespace {
class BookCoverLoaderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<unsigned> serial{0};
    root = std::filesystem::temp_directory_path() / ("crossmux-cover-loader-" + std::to_string(serial++));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    ASSERT_TRUE(std::filesystem::create_directories(root, error));
    ASSERT_EQ(setenv("CROSSPOINT_SIM_SD", root.c_str(), 1), 0);
    ASSERT_TRUE(Storage.begin());
    cover_stub::epubThumbnailGenerations = 0;
    cover_stub::fullCoverGenerations = 0;
  }

  void TearDown() override {
    unsetenv("CROSSPOINT_SIM_SD");
    std::error_code error;
    std::filesystem::remove_all(root, error);
  }

  void writeBook(const char* path) {
    std::ofstream output(root / (path + 1), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output << "book";
  }

  std::filesystem::path hostPath(const char* path) const { return root / (path + 1); }

  std::filesystem::path root;
};

TEST_F(BookCoverLoaderTest, GeneratesAndReusesMissingThumbnail) {
  writeBook("/book.epub");
  bool generated = false;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 226, &generated), "/.crosspoint/epub/thumb_226.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);

  generated = true;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 226, &generated), "/.crosspoint/epub/thumb_226.bmp");
  EXPECT_FALSE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
}

TEST_F(BookCoverLoaderTest, ReplacesCorruptThumbnail) {
  writeBook("/book.epub");
  Storage.mkdir("/.crosspoint/epub");
  {
    std::ofstream output(hostPath("/.crosspoint/epub/thumb_226.bmp"), std::ios::binary | std::ios::trunc);
    output << "broken";
  }

  bool generated = false;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 226, &generated), "/.crosspoint/epub/thumb_226.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
}

TEST_F(BookCoverLoaderTest, KeepsLayoutThumbnailSizesIndependent) {
  writeBook("/book.epub");
  bool generated = false;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 146, &generated), "/.crosspoint/epub/thumb_146.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 685, &generated), "/.crosspoint/epub/thumb_685.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 2);

  {
    std::ofstream output(hostPath("/.crosspoint/epub/thumb_146.bmp"), std::ios::binary | std::ios::trunc);
    output << "broken";
  }
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 685, &generated), "/.crosspoint/epub/thumb_685.bmp");
  EXPECT_FALSE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 2);
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 146, &generated), "/.crosspoint/epub/thumb_146.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 3);
}

TEST_F(BookCoverLoaderTest, PreservesCoverlessEpubMarker) {
  writeBook("/coverless.epub");
  bool generated = true;
  EXPECT_TRUE(BookCoverLoader::ensureThumbnail("/coverless.epub", 226, &generated).empty());
  EXPECT_FALSE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
  ASSERT_TRUE(std::filesystem::exists(hostPath("/.crosspoint/epub/thumb_226.bmp")));
  EXPECT_EQ(std::filesystem::file_size(hostPath("/.crosspoint/epub/thumb_226.bmp")), 0U);

  EXPECT_TRUE(BookCoverLoader::ensureThumbnail("/coverless.epub", 226, &generated).empty());
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
}

TEST_F(BookCoverLoaderTest, RejectsMissingBook) {
  bool generated = true;
  EXPECT_TRUE(BookCoverLoader::ensureThumbnail("/missing.epub", 226, &generated).empty());
  EXPECT_FALSE(generated);
  EXPECT_TRUE(BookCoverLoader::ensureFullCover("/missing.txt").empty());
}

TEST_F(BookCoverLoaderTest, GeneratesFullCoverAndMetadata) {
  writeBook("/notes.txt");
  std::string title;
  std::string author = "stale";
  bool generated = false;
  EXPECT_EQ(BookCoverLoader::ensureFullCover("/notes.txt", &title, &author, &generated), "/.crosspoint/txt/cover.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(title, "Text title");
  EXPECT_TRUE(author.empty());
  EXPECT_EQ(cover_stub::fullCoverGenerations, 1);

  generated = true;
  EXPECT_EQ(BookCoverLoader::ensureFullCover("/notes.txt", &title, &author, &generated), "/.crosspoint/txt/cover.bmp");
  EXPECT_FALSE(generated);
  EXPECT_EQ(cover_stub::fullCoverGenerations, 1);
}
}  // namespace
