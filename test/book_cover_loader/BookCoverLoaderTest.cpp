#include <Bitmap.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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
    cover_stub::overrideConversions = 0;
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

  void writeOverride(const std::initializer_list<uint8_t> bytes) {
    ASSERT_TRUE(Storage.ensureDirectoryExists("/.crosspoint/epub"));
    std::ofstream output(hostPath("/.crosspoint/epub/cover.override"), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    for (const uint8_t byte : bytes) output.put(static_cast<char>(byte));
  }

  std::filesystem::path hostPath(const char* path) const { return root / (path + 1); }

  // Writes a BI_RGB BMP with the given palette size and number of pixel bytes after the header.
  void writeThumb(const char* path, const int width, const int height, const uint16_t bpp,
                  const uint32_t paletteEntries, const size_t pixelBytes) {
    const uint32_t offset = 54 + paletteEntries * 4;
    std::vector<uint8_t> bytes(offset + pixelBytes, 0);
    const auto put16 = [&](const size_t at, const uint16_t value) {
      bytes[at] = value & 0xFF;
      bytes[at + 1] = value >> 8;
    };
    const auto put32 = [&](const size_t at, const uint32_t value) {
      for (int i = 0; i < 4; ++i) bytes[at + i] = (value >> (8 * i)) & 0xFF;
    };
    bytes[0] = 'B';
    bytes[1] = 'M';
    put32(2, static_cast<uint32_t>(bytes.size()));
    put32(10, offset);
    put32(14, 40);
    put32(18, static_cast<uint32_t>(width));
    put32(22, static_cast<uint32_t>(height));
    put16(26, 1);
    put16(28, bpp);
    put32(46, paletteEntries);
    ASSERT_TRUE(Storage.ensureDirectoryExists("/.crosspoint/epub"));
    std::ofstream output(hostPath(path), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.good());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

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

TEST_F(BookCoverLoaderTest, GeneratesEachThumbnailHeightOnlyOnce) {
  writeBook("/book.epub");
  constexpr int firstHeight = 220;
  constexpr int batchSize = 10;

  for (int offset = 0; offset < batchSize; ++offset) {
    bool generated = false;
    EXPECT_FALSE(BookCoverLoader::ensureThumbnail("/book.epub", firstHeight + offset, &generated).empty());
    EXPECT_TRUE(generated);
  }
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, batchSize);

  for (int offset = 0; offset < batchSize; ++offset) {
    bool generated = true;
    EXPECT_FALSE(BookCoverLoader::ensureThumbnail("/book.epub", firstHeight + offset, &generated).empty());
    EXPECT_FALSE(generated);
  }
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, batchSize);
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

TEST_F(BookCoverLoaderTest, RegeneratesTruncatedThumbnail) {
  writeBook("/book.epub");
  writeThumb("/.crosspoint/epub/thumb_226.bmp", 8, 4, 2, 4, 3 * 4);

  bool generated = false;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 226, &generated), "/.crosspoint/epub/thumb_226.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
}

TEST_F(BookCoverLoaderTest, RegeneratesPalettedThumbnailWithoutPixels) {
  writeBook("/book.epub");
  // A color table without pixel rows is regenerated.
  writeThumb("/.crosspoint/epub/thumb_226.bmp", 16, 16, 8, 256, 0);
  ASSERT_EQ(std::filesystem::file_size(hostPath("/.crosspoint/epub/thumb_226.bmp")), 1078U);

  bool generated = false;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 226, &generated), "/.crosspoint/epub/thumb_226.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
}

TEST_F(BookCoverLoaderTest, RegeneratesThumbnailWithPixelsPointingIntoPalette) {
  writeBook("/book.epub");
  writeThumb("/.crosspoint/epub/thumb_226.bmp", 16, 16, 8, 256, 0);
  {
    std::fstream file(hostPath("/.crosspoint/epub/thumb_226.bmp"), std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.good());
    const char offset[] = {54, 0, 0, 0};
    file.seekp(10);
    file.write(offset, sizeof(offset));
    ASSERT_TRUE(file.good());
  }
  bool generated = false;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 226, &generated), "/.crosspoint/epub/thumb_226.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
  EXPECT_EQ(std::filesystem::file_size(hostPath("/.crosspoint/epub/thumb_226.bmp")), 66U);
}

TEST_F(BookCoverLoaderTest, KeepsCompleteThumbnail) {
  writeBook("/book.epub");
  // A complete 24bpp thumbnail is reused.
  writeThumb("/.crosspoint/epub/thumb_226.bmp", 2, 1, 24, 0, 8);
  ASSERT_EQ(std::filesystem::file_size(hostPath("/.crosspoint/epub/thumb_226.bmp")), 62U);

  bool generated = true;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 226, &generated), "/.crosspoint/epub/thumb_226.bmp");
  EXPECT_FALSE(generated);
  EXPECT_EQ(std::filesystem::file_size(hostPath("/.crosspoint/epub/thumb_226.bmp")), 62U);

  // A complete paletted thumbnail is reused.
  writeThumb("/.crosspoint/epub/thumb_146.bmp", 16, 16, 8, 256, 16 * 16);
  ASSERT_EQ(std::filesystem::file_size(hostPath("/.crosspoint/epub/thumb_146.bmp")), 1334U);

  generated = true;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/book.epub", 146, &generated), "/.crosspoint/epub/thumb_146.bmp");
  EXPECT_FALSE(generated);
  EXPECT_EQ(std::filesystem::file_size(hostPath("/.crosspoint/epub/thumb_146.bmp")), 1334U);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 0);
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

TEST_F(BookCoverLoaderTest, UsesOverrideAfterInternalEpubCoverFails) {
  writeBook("/coverless.epub");
  writeOverride({0xFF, 0xD8, 0xFF, 0xE0, 0, 0, 0, 0, 0});

  bool generated = false;
  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/coverless.epub", 226, &generated), "/.crosspoint/epub/thumb_226.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
  EXPECT_EQ(cover_stub::overrideConversions, 1);
}

TEST_F(BookCoverLoaderTest, PrefersInternalEpubCoverOverOverride) {
  writeBook("/book.epub");
  writeOverride({0xFF, 0xD8, 0xFF, 0xE0, 0, 0, 0, 0, 0});

  EXPECT_FALSE(BookCoverLoader::ensureThumbnail("/book.epub", 226).empty());
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
  EXPECT_EQ(cover_stub::overrideConversions, 0);
}

TEST_F(BookCoverLoaderTest, RetriesEmptyMarkerWhenOverrideAppears) {
  writeBook("/coverless.epub");
  bool generated = true;
  EXPECT_TRUE(BookCoverLoader::ensureThumbnail("/coverless.epub", 226, &generated).empty());
  writeOverride({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0});

  EXPECT_EQ(BookCoverLoader::ensureThumbnail("/coverless.epub", 226, &generated), "/.crosspoint/epub/thumb_226.bmp");
  EXPECT_TRUE(generated);
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 2);
  EXPECT_EQ(cover_stub::overrideConversions, 1);
}

TEST_F(BookCoverLoaderTest, IgnoresInvalidOverrideAfterEmptyMarker) {
  writeBook("/coverless.epub");
  EXPECT_TRUE(BookCoverLoader::ensureThumbnail("/coverless.epub", 226).empty());
  writeOverride({'n', 'o', 't', '-', 'a', 'n', '-', 'i', 'm', 'a', 'g', 'e'});

  EXPECT_TRUE(BookCoverLoader::ensureThumbnail("/coverless.epub", 226).empty());
  EXPECT_EQ(cover_stub::epubThumbnailGenerations, 1);
  EXPECT_EQ(cover_stub::overrideConversions, 0);
}

TEST_F(BookCoverLoaderTest, UsesOverrideForFullEpubCover) {
  writeBook("/coverless.epub");
  writeOverride({0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0});

  std::string title;
  std::string author;
  EXPECT_EQ(BookCoverLoader::ensureFullCover("/coverless.epub", &title, &author), "/.crosspoint/epub/cover.bmp");
  EXPECT_EQ(title, "EPUB title");
  EXPECT_EQ(author, "EPUB author");
  EXPECT_EQ(cover_stub::overrideConversions, 1);
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

// Incomplete thumbs are removed; an empty EPUB thumb stays as the no-cover marker.
TEST_F(BookCoverLoaderTest, HomeShortcutClassifiesCachedCover) {
  writeThumb("/.crosspoint/epub/thumb_226.bmp", 1, 1, 1, 2, 0);
  EXPECT_EQ(BookCoverLoader::checkCachedCover("/.crosspoint/epub/thumb_226.bmp", true),
            BookCoverLoader::CachedCoverState::Missing);
  EXPECT_FALSE(std::filesystem::exists(hostPath("/.crosspoint/epub/thumb_226.bmp")));

  ASSERT_TRUE(cover_stub::writeEmpty("/.crosspoint/epub/thumb_226.bmp"));
  EXPECT_EQ(BookCoverLoader::checkCachedCover("/.crosspoint/epub/thumb_226.bmp", true),
            BookCoverLoader::CachedCoverState::Terminal);
  EXPECT_EQ(std::filesystem::file_size(hostPath("/.crosspoint/epub/thumb_226.bmp")), 0U);
}
}  // namespace
