#include <ObfuscationUtils.h>
#include <expat.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "WeReadBrowse.h"
#include "WeReadStore.h"
#include "WeReadXhtmlCodec.h"

namespace obfuscation {

void xorTransform(std::string&) {}

void xorTransform(std::string& data, const uint8_t* key, const size_t keyLen) {
  if (!key || keyLen == 0) return;
  for (size_t i = 0; i < data.size(); ++i) data[i] ^= key[i % keyLen];
}

String obfuscateToBase64(const std::string& plaintext) { return String(plaintext.c_str()); }

std::string deobfuscateFromBase64(const char* encoded, bool* ok) {
  if (ok) *ok = encoded != nullptr;
  return encoded ? encoded : "";
}

void selfTest() {}

}  // namespace obfuscation

namespace {

uint16_t readLe16(const std::vector<uint8_t>& data, const size_t offset) {
  return static_cast<uint16_t>(data[offset]) | static_cast<uint16_t>(data[offset + 1] << 8);
}

uint32_t readLe32(const std::vector<uint8_t>& data, const size_t offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

class WeReadStoreTest : public ::testing::Test {
 protected:
  static constexpr const char* kBrowseBook = "browse-book";
  static constexpr const char* kBrowseOwner = "123456";

  void SetUp() override {
    static std::atomic<unsigned> serial{0};
    root_ = std::filesystem::temp_directory_path() / ("crossmux-weread-store-" + std::to_string(serial.fetch_add(1)));
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

  void beginBrowseCache(WeReadBrowse::CacheManifest& manifest) {
    ASSERT_TRUE(WeReadBrowse::beginCache(kBrowseBook, kBrowseOwner, manifest));
  }

  void writeBrowsePage(WeReadBrowse::CacheManifest& manifest, const WeReadBrowse::Kind kind, const uint32_t page,
                       const char* json, const uint32_t maxRecords = WeReadBrowse::kMaxRecords) {
    WeReadBrowse::ResponseParser parser(kBrowseBook, manifest.activeSlot, kind, page, maxRecords);
    ASSERT_TRUE(parser.reset());
    const size_t length = strlen(json);
    for (size_t offset = 0; offset < length;) {
      const size_t chunk = std::min<size_t>((offset % 7) + 1, length - offset);
      ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(json + offset), chunk));
      offset += chunk;
    }
    ASSERT_TRUE(parser.finish());
    manifest.pageCounts[WeReadBrowse::kindIndex(kind)] =
        std::max(manifest.pageCounts[WeReadBrowse::kindIndex(kind)], page + 1);
    manifest.recordCounts[WeReadBrowse::kindIndex(kind)] += parser.count();
  }

  std::filesystem::path root_;
};

TEST_F(WeReadStoreTest, StreamsLargeShelfAndTocIndexesAndRejectsCorruption) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  {
    struct LegacyShelfRecord {
      char bookId[64] = {};
      char title[192] = {};
      char author[96] = {};
      uint32_t readUpdateTime = 0;
    };
    static_assert(sizeof(LegacyShelfRecord) == 356);
    constexpr uint32_t kLegacyShelfMagic = 0x35535257;  // WRS5
    WeReadStore::IndexWriter legacyShelf;
    ASSERT_TRUE(legacyShelf.begin(WeReadStore::kShelfPath, kLegacyShelfMagic, sizeof(LegacyShelfRecord)));
    LegacyShelfRecord record;
    strcpy(record.bookId, "legacy-book");
    ASSERT_TRUE(legacyShelf.append(&record));
    ASSERT_TRUE(legacyShelf.finish());

    HalFile rejectedLegacy;
    uint32_t legacyCount = 0;
    EXPECT_FALSE(WeReadStore::openShelf(rejectedLegacy, legacyCount));
  }

  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  for (unsigned i = 0; i < 600; ++i) {
    WeReadStore::ShelfRecord record;
    snprintf(record.bookId, sizeof(record.bookId), "book-%03u", i);
    snprintf(record.title, sizeof(record.title), "标题-%03u", i);
    snprintf(record.author, sizeof(record.author), "作者-%03u", i);
    snprintf(record.coverUrl, sizeof(record.coverUrl), "https://cdn.example/cover-%03u.jpg", i);
    ASSERT_TRUE(shelf.append(&record));
  }
  ASSERT_EQ(shelf.count(), 600U);
  ASSERT_TRUE(shelf.finish());

  HalFile shelfFile;
  uint32_t count = 0;
  ASSERT_TRUE(WeReadStore::openShelf(shelfFile, count));
  ASSERT_EQ(count, 600U);
  WeReadStore::ShelfRecord shelfRecord;
  ASSERT_TRUE(WeReadStore::readShelfRecord(shelfFile, 599, shelfRecord));
  EXPECT_STREQ(shelfRecord.bookId, "book-599");
  EXPECT_STREQ(shelfRecord.title, "标题-599");
  EXPECT_STREQ(shelfRecord.coverUrl, "https://cdn.example/cover-599.jpg");

  const std::string tocPath = WeReadStore::tocPath("book-599");
  ASSERT_TRUE(Storage.ensureDirectoryExists(WeReadStore::bookDirectory("book-599").c_str()));
  WeReadStore::IndexWriter toc;
  ASSERT_TRUE(toc.begin(tocPath, WeReadStore::kTocMagic, sizeof(WeReadStore::TocRecord)));
  for (unsigned i = 0; i < 525; ++i) {
    WeReadStore::TocRecord record;
    snprintf(record.chapterUid, sizeof(record.chapterUid), "chapter-%03u", i);
    snprintf(record.title, sizeof(record.title), "章节-%03u", i);
    record.wordCount = 1000 + i;
    record.chapterIdx = i;
    record.paid = i % 2;
    ASSERT_TRUE(toc.append(&record));
  }
  ASSERT_TRUE(toc.finish());

  HalFile tocFile;
  ASSERT_TRUE(WeReadStore::openToc(tocPath, tocFile, count));
  ASSERT_EQ(count, 525U);
  WeReadStore::TocRecord tocRecord;
  ASSERT_TRUE(WeReadStore::readTocRecord(tocFile, 524, tocRecord));
  EXPECT_STREQ(tocRecord.chapterUid, "chapter-524");
  EXPECT_EQ(tocRecord.wordCount, 1524U);
  EXPECT_EQ(tocRecord.chapterIdx, 524U);

  std::ofstream corrupt(hostPath(WeReadStore::kShelfPath), std::ios::binary | std::ios::app);
  ASSERT_TRUE(corrupt.good());
  corrupt.put('\0');
  corrupt.close();
  HalFile rejected;
  EXPECT_FALSE(WeReadStore::openShelf(rejected, count));
}

TEST_F(WeReadStoreTest, RejectsWrt1AndMapsProgressWithoutLoadingTheCatalog) {
  const std::string bookDir = WeReadStore::bookDirectory("progress-book");
  const std::string tocPath = WeReadStore::tocPath("progress-book");
  ASSERT_TRUE(Storage.ensureDirectoryExists(bookDir.c_str()));

  {
    constexpr uint32_t kLegacyTocMagic = 0x31545257;  // WRT1
    WeReadStore::IndexWriter legacy;
    ASSERT_TRUE(legacy.begin(tocPath, kLegacyTocMagic, 264));
    std::array<uint8_t, 264> record = {};
    ASSERT_TRUE(legacy.append(record.data()));
    ASSERT_TRUE(legacy.finish());
    HalFile rejected;
    uint32_t count = 0;
    EXPECT_FALSE(WeReadStore::openToc(tocPath, rejected, count));
  }

  WeReadStore::IndexWriter writer;
  ASSERT_TRUE(writer.begin(tocPath, WeReadStore::kTocMagic, sizeof(WeReadStore::TocRecord)));
  const uint32_t words[] = {100, 0, 300};
  for (uint32_t i = 0; i < 3; ++i) {
    WeReadStore::TocRecord record;
    snprintf(record.chapterUid, sizeof(record.chapterUid), "chapter-%u", i);
    record.wordCount = words[i];
    record.chapterIdx = i;
    ASSERT_TRUE(writer.append(&record));
  }
  ASSERT_TRUE(writer.finish());

  WeReadStore::TocRecord chapter;
  uint32_t offset = 0;
  ASSERT_TRUE(WeReadStore::mapFractionToChapter(tocPath, 0.5f, chapter, offset));
  EXPECT_STREQ(chapter.chapterUid, "chapter-2");
  EXPECT_EQ(offset, 100U);

  float fraction = 0.0f;
  ASSERT_TRUE(WeReadStore::mapChapterToFraction(tocPath, "chapter-2", 100, fraction));
  EXPECT_FLOAT_EQ(fraction, 0.5f);
  ASSERT_TRUE(WeReadStore::mapFractionToChapter(tocPath, 1.0f, chapter, offset));
  EXPECT_STREQ(chapter.chapterUid, "chapter-2");
  EXPECT_EQ(offset, 300U);
  EXPECT_FALSE(WeReadStore::mapChapterToFraction(tocPath, "chapter-1", 0, fraction));
  EXPECT_FALSE(WeReadStore::mapChapterToFraction(tocPath, "missing", 0, fraction));
}

TEST_F(WeReadStoreTest, MapsGeneratedChapterVisibleOffsetsWithoutWholeBookApproximation) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  const std::string tocPath = WeReadStore::tocPath("precise-book");
  WeReadStore::IndexWriter writer;
  ASSERT_TRUE(writer.begin(tocPath, WeReadStore::kTocMagic, sizeof(WeReadStore::TocRecord)));
  const uint32_t words[] = {100, 0, 300};
  for (uint32_t i = 0; i < 3; ++i) {
    WeReadStore::TocRecord record;
    snprintf(record.chapterUid, sizeof(record.chapterUid), "chapter-%u", i);
    record.wordCount = words[i];
    record.chapterIdx = i;
    ASSERT_TRUE(writer.append(&record));
  }
  ASSERT_TRUE(writer.finish());

  WeReadStore::TocRecord chapter;
  uint32_t offset = 0;
  float fraction = 0.0f;
  ASSERT_TRUE(WeReadStore::mapVisibleOffsetToChapter(tocPath, 0, 50, chapter, offset, fraction));
  EXPECT_STREQ(chapter.chapterUid, "chapter-0");
  EXPECT_EQ(offset, 50U);
  EXPECT_FLOAT_EQ(fraction, 0.125f);

  ASSERT_TRUE(WeReadStore::mapVisibleOffsetToChapter(tocPath, 2, 150, chapter, offset, fraction));
  EXPECT_STREQ(chapter.chapterUid, "chapter-2");
  EXPECT_EQ(offset, 150U);
  EXPECT_FLOAT_EQ(fraction, 0.625f);

  ASSERT_TRUE(WeReadStore::mapVisibleOffsetToChapter(tocPath, 2, 0, chapter, offset, fraction));
  EXPECT_EQ(offset, 0U);
  EXPECT_FLOAT_EQ(fraction, 0.25f);
  ASSERT_TRUE(WeReadStore::mapVisibleOffsetToChapter(tocPath, 2, 400, chapter, offset, fraction));
  EXPECT_EQ(offset, 300U);
  EXPECT_FLOAT_EQ(fraction, 1.0f);

  EXPECT_FALSE(WeReadStore::mapVisibleOffsetToChapter(tocPath, 1, 0, chapter, offset, fraction));
  EXPECT_FALSE(WeReadStore::mapVisibleOffsetToChapter(tocPath, 3, 0, chapter, offset, fraction));

  ASSERT_TRUE(WeReadStore::mapNativeOffsetToChapter(tocPath, 0, 1234, chapter, offset));
  EXPECT_STREQ(chapter.chapterUid, "chapter-0");
  EXPECT_EQ(offset, 1234U);
  ASSERT_TRUE(WeReadStore::mapNativeOffsetToChapter(tocPath, 1, 456, chapter, offset));
  EXPECT_STREQ(chapter.chapterUid, "chapter-1");
  EXPECT_EQ(offset, 456U);
  EXPECT_FALSE(WeReadStore::mapNativeOffsetToChapter(tocPath, 3, 0, chapter, offset));

  uint32_t tocIndex = 0;
  float chapterFraction = 0.0f;
  ASSERT_TRUE(WeReadStore::mapChapterToPosition(tocPath, "chapter-2", 100, tocIndex, chapterFraction, fraction));
  EXPECT_EQ(tocIndex, 2U);
  EXPECT_NEAR(chapterFraction, 1.0f / 3.0f, 0.000001f);
  EXPECT_FLOAT_EQ(fraction, 0.5f);
}

TEST_F(WeReadStoreTest, MapsGeneratedVisibleOffsetsToRawXhtmlUtf16Coordinates) {
  const std::string bookDir = WeReadStore::bookDirectory("source-offset-book");
  ASSERT_TRUE(Storage.ensureDirectoryExists((bookDir + "/chapters").c_str()));
  const std::string path = WeReadStore::chapterPath(bookDir, 0);
  std::ofstream chapter(hostPath(path.c_str()), std::ios::binary);
  ASSERT_TRUE(chapter.good());
  // The first 15-byte marker starts at byte 123, crossing the resolver's
  // 128-byte read boundary.
  chapter << "\xEF\xBB\xBF<?xml version=\"1.0\"?><html><head><title>" << std::string(59, 'x')
          << "</title></head><body>"
             "<!--wr-co:10-->A中"
             "<!--wr-co:20-->&amp;"
             "<!--wr-co:25-->😀"
             "<!--wr-co:30-->\r\nZ"
             "<!--wr-co:33-->"
             "<!--wr-co:40--><img src=\"image.png\"/>"
             "<!--wr-co:50--></body></html>";
  chapter.close();

  struct Mapping {
    uint32_t visible;
    uint32_t native;
  };
  static constexpr Mapping kMappings[] = {{0, 10}, {1, 11}, {2, 20}, {3, 25}, {4, 30}, {5, 32}, {6, 50}};
  for (const auto& mapping : kMappings) {
    uint32_t native = 0;
    ASSERT_TRUE(WeReadXhtmlCodec::visibleToNativeOffset(path, mapping.visible, native));
    EXPECT_EQ(native, mapping.native) << "visible=" << mapping.visible;
  }

  static constexpr Mapping kReverseMappings[] = {
      {0, 0},  {0, 10}, {1, 11}, {2, 12}, {2, 20}, {3, 25}, {3, 26},
      {4, 30}, {5, 31}, {5, 32}, {6, 33}, {6, 40}, {6, 50},
  };
  for (const auto& mapping : kReverseMappings) {
    uint32_t visible = UINT32_MAX;
    ASSERT_TRUE(WeReadXhtmlCodec::nativeToVisibleOffset(path, mapping.native, visible));
    EXPECT_EQ(visible, mapping.visible) << "native=" << mapping.native;
  }
  uint32_t rejected = 0;
  EXPECT_FALSE(WeReadXhtmlCodec::visibleToNativeOffset(path, 7, rejected));
  EXPECT_FALSE(WeReadXhtmlCodec::nativeToVisibleOffset(path, 51, rejected));
}

TEST_F(WeReadStoreTest, KeepsUtf8CodepointsIntactAcrossSanitizerReadBoundaries) {
  const std::string bookDir = WeReadStore::bookDirectory("utf8-boundary-book");
  ASSERT_TRUE(Storage.ensureDirectoryExists((bookDir + "/chapters").c_str()));
  const std::string inputPath = bookDir + "/decoded.xhtml";
  const std::string outputPath = WeReadStore::chapterPath(bookDir, 0);
  const std::string imageIndexPath = WeReadStore::imageIndexPath(bookDir, 0);

  std::string input = "<p>";
  input.append(508, 'a');
  input += "中";  // Lead byte is the last byte of the first 512-byte read.
  input.append(1022 - input.size(), 'b');
  input += "😀";  // Two bytes land on each side of the next read boundary.
  input += "c</p>";
  {
    std::ofstream source(hostPath(inputPath.c_str()), std::ios::binary);
    ASSERT_TRUE(source.good());
    source.write(input.data(), static_cast<std::streamsize>(input.size()));
  }

  std::array<uint8_t, 512> readBuffer{};
  std::array<char, 4096> tagBuffer{};
  ASSERT_TRUE(WeReadXhtmlCodec::sanitizeChapter(inputPath, outputPath, imageIndexPath, 0, "Boundary", false,
                                                readBuffer.data(), readBuffer.size(), tagBuffer.data(),
                                                tagBuffer.size()));

  std::ifstream generated(hostPath(outputPath.c_str()), std::ios::binary);
  ASSERT_TRUE(generated.good());
  const std::string xhtml((std::istreambuf_iterator<char>(generated)), std::istreambuf_iterator<char>());
  XML_Parser parser = XML_ParserCreate(nullptr);
  ASSERT_NE(parser, nullptr);
  const XML_Status status = XML_Parse(parser, xhtml.data(), static_cast<int>(xhtml.size()), XML_TRUE);
  EXPECT_EQ(status, XML_STATUS_OK) << XML_ErrorString(XML_GetErrorCode(parser));
  XML_ParserFree(parser);

  uint32_t nativeOffset = 0;
  EXPECT_TRUE(WeReadXhtmlCodec::visibleToNativeOffset(outputPath, 508, nativeOffset));
  EXPECT_EQ(nativeOffset, 511U);
  EXPECT_TRUE(WeReadXhtmlCodec::visibleToNativeOffset(outputPath, 1017, nativeOffset));
  EXPECT_EQ(nativeOffset, 1020U);
  EXPECT_TRUE(WeReadXhtmlCodec::visibleToNativeOffset(outputPath, 1018, nativeOffset));
  EXPECT_EQ(nativeOffset, 1022U);

  uint32_t visibleOffset = 0;
  EXPECT_TRUE(WeReadXhtmlCodec::nativeToVisibleOffset(outputPath, 1021, visibleOffset));
  EXPECT_EQ(visibleOffset, 1017U);
}

TEST_F(WeReadStoreTest, FiltersBookAnnotationsWithoutDroppingOrdinaryAsideText) {
  const std::string bookDir = WeReadStore::bookDirectory("annotation-book");
  ASSERT_TRUE(Storage.ensureDirectoryExists((bookDir + "/chapters").c_str()));
  const std::string inputPath = bookDir + "/decoded.xhtml";
  const std::string outputPath = WeReadStore::chapterPath(bookDir, 0);
  const std::string imageIndexPath = WeReadStore::imageIndexPath(bookDir, 0);
  const std::string input =
      "<p>A<a class='marker DUOKAN-footnote extra' href='#d1'><img src='https://res.weread.qq.com/note.png'/></a>"
      "<img class='duokan-footnote' src='https://res.weread.qq.com/direct.png'/>B</p>"
      "<aside class='note'>Sidebar</aside>"
      "<ol class='duokan-footnote-content'><li class='duokan-footnote-item' id='d1'><p>Duokan note "
      "<a href='#back'>back</a></p></li></ol>"
      "<p>C<a epub:type='noteref' href='#n1'>2</a>D</p>"
      "<aside epub:type='footnote' id='n1'><p>Semantic footnote</p></aside>"
      "<aside epub:type='endnote'>Semantic endnote</aside><aside epub:type='rearnote'>Semantic rearnote</aside>"
      "<a type='noteref'>Type ref</a><aside type='footnote'>Type footnote</aside>"
      "<aside type='endnote'>Type endnote</aside><aside type='rearnote'>Type rearnote</aside>"
      "<a role='doc-noteref'>Role ref</a><aside role='doc-footnote'>Role footnote</aside>"
      "<aside role='doc-endnote'>Role endnote</aside><aside role='doc-rearnote'>Role rearnote</aside><p>E</p>";
  {
    std::ofstream source(hostPath(inputPath.c_str()), std::ios::binary);
    ASSERT_TRUE(source.good());
    source.write(input.data(), static_cast<std::streamsize>(input.size()));
  }

  std::array<uint8_t, 7> readBuffer{};
  std::array<char, 4096> tagBuffer{};
  ASSERT_TRUE(WeReadXhtmlCodec::sanitizeChapter(inputPath, outputPath, imageIndexPath, 0, "Annotations", false,
                                                readBuffer.data(), readBuffer.size(), tagBuffer.data(),
                                                tagBuffer.size()));

  std::ifstream generated(hostPath(outputPath.c_str()), std::ios::binary);
  ASSERT_TRUE(generated.good());
  const std::string xhtml((std::istreambuf_iterator<char>(generated)), std::istreambuf_iterator<char>());
  XML_Parser parser = XML_ParserCreate(nullptr);
  ASSERT_NE(parser, nullptr);
  const XML_Status status = XML_Parse(parser, xhtml.data(), static_cast<int>(xhtml.size()), XML_TRUE);
  EXPECT_EQ(status, XML_STATUS_OK) << XML_ErrorString(XML_GetErrorCode(parser));
  XML_ParserFree(parser);

  EXPECT_NE(xhtml.find("Sidebar"), std::string::npos);
  static constexpr const char* kRemovedText[] = {
      "Duokan note",  "Semantic footnote", "Semantic endnote", "Semantic rearnote", "Type ref",     "Type footnote",
      "Type endnote", "Type rearnote",     "Role ref",         "Role footnote",     "Role endnote", "Role rearnote"};
  for (const char* removed : kRemovedText) EXPECT_EQ(xhtml.find(removed), std::string::npos) << removed;
  EXPECT_EQ(xhtml.find("note.png"), std::string::npos);
  EXPECT_EQ(xhtml.find("direct.png"), std::string::npos);
  EXPECT_EQ(xhtml.find(">2<"), std::string::npos);

  HalFile imageIndex;
  uint32_t imageCount = UINT32_MAX;
  ASSERT_TRUE(WeReadStore::openImageIndex(imageIndexPath, imageIndex, imageCount));
  EXPECT_EQ(imageCount, 0U);

  const struct {
    uint32_t visible;
    const char* sourceRun;
  } mappings[] = {{0, ">A<"}, {1, ">B<"}, {2, ">Sidebar<"}, {9, ">C<"}, {10, ">D<"}, {11, ">E<"}};
  for (const auto& mapping : mappings) {
    uint32_t nativeOffset = 0;
    ASSERT_TRUE(WeReadXhtmlCodec::visibleToNativeOffset(outputPath, mapping.visible, nativeOffset));
    EXPECT_EQ(nativeOffset, input.find(mapping.sourceRun) + 1) << "visible=" << mapping.visible;
  }
  uint32_t visibleOffset = UINT32_MAX;
  ASSERT_TRUE(WeReadXhtmlCodec::nativeToVisibleOffset(outputPath, input.find("Duokan note"), visibleOffset));
  EXPECT_EQ(visibleOffset, 9U);
}

TEST_F(WeReadStoreTest, RejectsMissingAndMalformedSourceOffsetMarkers) {
  const std::string bookDir = WeReadStore::bookDirectory("bad-source-offset-book");
  ASSERT_TRUE(Storage.ensureDirectoryExists((bookDir + "/chapters").c_str()));
  const std::string path = WeReadStore::chapterPath(bookDir, 0);
  uint32_t output = 0;
  const auto reject = [&](const char* xhtml, const uint32_t visibleOffset) {
    {
      std::ofstream chapter(hostPath(path.c_str()), std::ios::binary | std::ios::trunc);
      chapter << xhtml;
    }
    EXPECT_FALSE(WeReadXhtmlCodec::visibleToNativeOffset(path, visibleOffset, output));
  };
  reject("<html><body>text</body></html>", 0);
  reject("<html><body><!--wr-co:bad-->text</body></html>", 0);
  reject("<html><body><!--wr-co:20-->a<!--wr-co:10-->b</body></html>", 0);
}

TEST(WeReadStore, ParsesOnlyGeneratedChapterHrefs) {
  uint32_t tocIndex = 0;
  EXPECT_TRUE(WeReadStore::parseGeneratedChapterHref("ch000042.xhtml", tocIndex));
  EXPECT_EQ(tocIndex, 42U);
  EXPECT_TRUE(WeReadStore::parseGeneratedChapterHref("OPS/text/ch1234567.xhtml", tocIndex));
  EXPECT_EQ(tocIndex, 1234567U);

  EXPECT_FALSE(WeReadStore::parseGeneratedChapterHref("chapter000042.xhtml", tocIndex));
  EXPECT_FALSE(WeReadStore::parseGeneratedChapterHref("ch.xhtml", tocIndex));
  EXPECT_FALSE(WeReadStore::parseGeneratedChapterHref("ch000042.xhtml#anchor", tocIndex));
  EXPECT_FALSE(WeReadStore::parseGeneratedChapterHref("ch4294967296.xhtml", tocIndex));
}

TEST_F(WeReadStoreTest, FindsOnlyTheExactGeneratedBookPath) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  WeReadStore::ShelfRecord record;
  strcpy(record.bookId, "book-1");
  strcpy(record.title, "Test Book");
  ASSERT_TRUE(shelf.append(&record));
  ASSERT_TRUE(shelf.finish());

  char bookId[64] = {};
  EXPECT_TRUE(WeReadStore::findBookIdForPath("/WeRead/Test Book-book-1.epub", bookId, sizeof(bookId)));
  EXPECT_STREQ(bookId, "book-1");
  EXPECT_FALSE(WeReadStore::findBookIdForPath("/WeRead/Renamed-book-1.epub", bookId, sizeof(bookId)));
}

TEST_F(WeReadStoreTest, SortsEmptySingleAndTwentyBookShelves) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  for (const uint32_t shelfSize : {0U, 1U, 20U}) {
    WeReadStore::IndexWriter shelf;
    ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
    for (uint32_t i = 0; i < shelfSize; ++i) {
      WeReadStore::ShelfRecord record;
      snprintf(record.bookId, sizeof(record.bookId), "book-%02u", static_cast<unsigned>(i));
      record.readUpdateTime = i;
      ASSERT_TRUE(shelf.append(&record));
    }
    ASSERT_TRUE(shelf.finish());
    ASSERT_EQ(WeReadStore::sortShelfByRecent(), WeReadStore::ShelfSortResult::Ok);

    HalFile file;
    uint32_t count = 0;
    ASSERT_TRUE(WeReadStore::openShelf(file, count));
    ASSERT_EQ(count, shelfSize);
    for (uint32_t i = 0; i < shelfSize; ++i) {
      WeReadStore::ShelfRecord record;
      ASSERT_TRUE(WeReadStore::readShelfRecord(file, i, record));
      char expected[64];
      snprintf(expected, sizeof(expected), "book-%02u", static_cast<unsigned>(shelfSize - 1 - i));
      EXPECT_STREQ(record.bookId, expected);
    }
  }
}

TEST_F(WeReadStoreTest, FullySortsShelfAtThresholdWithStableTiesAndUnreadLast) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  for (uint32_t i = 0; i < WeReadStore::kLargeShelfThreshold; ++i) {
    WeReadStore::ShelfRecord record;
    snprintf(record.bookId, sizeof(record.bookId), "book-%03u", i);
    record.readUpdateTime = i == 1 ? 100 : i == 2 || i == 3 ? 300 : i == 4 ? 200 : 0;
    ASSERT_TRUE(shelf.append(&record));
  }
  ASSERT_TRUE(shelf.finish());
  ASSERT_EQ(WeReadStore::sortShelfByRecent(), WeReadStore::ShelfSortResult::Ok);

  HalFile file;
  uint32_t count = 0;
  ASSERT_TRUE(WeReadStore::openShelf(file, count));
  ASSERT_EQ(count, WeReadStore::kLargeShelfThreshold);
  uint32_t previousTime = UINT32_MAX;
  unsigned previousSourceIndex = 0;
  for (uint32_t i = 0; i < count; ++i) {
    WeReadStore::ShelfRecord record;
    ASSERT_TRUE(WeReadStore::readShelfRecord(file, i, record));
    unsigned sourceIndex = 0;
    ASSERT_EQ(sscanf(record.bookId, "book-%u", &sourceIndex), 1);
    EXPECT_LE(record.readUpdateTime, previousTime);
    if (record.readUpdateTime == previousTime) EXPECT_GT(sourceIndex, previousSourceIndex);
    previousTime = record.readUpdateTime;
    previousSourceIndex = sourceIndex;
  }

  WeReadStore::ShelfRecord record;
  ASSERT_TRUE(WeReadStore::readShelfRecord(file, 0, record));
  EXPECT_STREQ(record.bookId, "book-002");
  ASSERT_TRUE(WeReadStore::readShelfRecord(file, 1, record));
  EXPECT_STREQ(record.bookId, "book-003");
  ASSERT_TRUE(WeReadStore::readShelfRecord(file, 4, record));
  EXPECT_STREQ(record.bookId, "book-000");
}

TEST_F(WeReadStoreTest, SortsOnlyRecentHeadForLargeShelvesAndPreservesTailOrder) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  for (const uint32_t shelfSize : {501U, 1000U, 2000U}) {
    WeReadStore::IndexWriter shelf;
    ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
    for (uint32_t i = 0; i < shelfSize; ++i) {
      WeReadStore::ShelfRecord record;
      snprintf(record.bookId, sizeof(record.bookId), "book-%04u", static_cast<unsigned>(i));
      record.readUpdateTime = i + 1;
      ASSERT_TRUE(shelf.append(&record));
    }
    ASSERT_TRUE(shelf.finish());
    ASSERT_EQ(WeReadStore::sortShelfByRecent(), WeReadStore::ShelfSortResult::Degraded);

    HalFile file;
    uint32_t count = 0;
    ASSERT_TRUE(WeReadStore::openShelf(file, count));
    ASSERT_EQ(count, shelfSize);
    for (uint32_t i = 0; i < WeReadStore::kRecentShelfWindow; ++i) {
      WeReadStore::ShelfRecord record;
      ASSERT_TRUE(WeReadStore::readShelfRecord(file, i, record));
      char expected[64];
      snprintf(expected, sizeof(expected), "book-%04u", static_cast<unsigned>(shelfSize - 1 - i));
      EXPECT_STREQ(record.bookId, expected);
    }
    for (uint32_t i = WeReadStore::kRecentShelfWindow; i < shelfSize; ++i) {
      WeReadStore::ShelfRecord record;
      ASSERT_TRUE(WeReadStore::readShelfRecord(file, i, record));
      char expected[64];
      snprintf(expected, sizeof(expected), "book-%04u", static_cast<unsigned>(i - WeReadStore::kRecentShelfWindow));
      EXPECT_STREQ(record.bookId, expected);
    }
  }
}

TEST_F(WeReadStoreTest, FallsBackToRecentHeadWhenFullSortAllocationFails) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  for (uint32_t i = 0; i < 100; ++i) {
    WeReadStore::ShelfRecord record;
    snprintf(record.bookId, sizeof(record.bookId), "book-%03u", static_cast<unsigned>(i));
    record.readUpdateTime = i;
    ASSERT_TRUE(shelf.append(&record));
  }
  ASSERT_TRUE(shelf.finish());

  WeReadStore::failNextFullShelfSortAllocationForTest();
  ASSERT_EQ(WeReadStore::sortShelfByRecent(), WeReadStore::ShelfSortResult::Degraded);
  HalFile file;
  uint32_t count = 0;
  ASSERT_TRUE(WeReadStore::openShelf(file, count));
  ASSERT_EQ(count, 100U);
  for (uint32_t i = 0; i < WeReadStore::kRecentShelfWindow; ++i) {
    WeReadStore::ShelfRecord record;
    ASSERT_TRUE(WeReadStore::readShelfRecord(file, i, record));
    char expected[64];
    snprintf(expected, sizeof(expected), "book-%03u", static_cast<unsigned>(99 - i));
    EXPECT_STREQ(record.bookId, expected);
  }
}

TEST_F(WeReadStoreTest, PromotesSmallShelfBookAtomically) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  for (uint32_t i = 0; i < 3; ++i) {
    WeReadStore::ShelfRecord record;
    snprintf(record.bookId, sizeof(record.bookId), "book-%u", static_cast<unsigned>(i));
    record.readUpdateTime = 300 - i * 100;
    ASSERT_TRUE(shelf.append(&record));
  }
  ASSERT_TRUE(shelf.finish());

  ASSERT_EQ(WeReadStore::promoteShelfBook("book-2", 300), WeReadStore::ShelfSortResult::Ok);

  HalFile file;
  uint32_t count = 0;
  ASSERT_TRUE(WeReadStore::openShelf(file, count));
  ASSERT_EQ(count, 3U);
  WeReadStore::ShelfRecord record;
  ASSERT_TRUE(WeReadStore::readShelfRecord(file, 0, record));
  EXPECT_STREQ(record.bookId, "book-2");
  EXPECT_EQ(record.readUpdateTime, 300U);
  ASSERT_TRUE(WeReadStore::readShelfRecord(file, 1, record));
  EXPECT_STREQ(record.bookId, "book-0");
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/shelf.bin.part"));

  EXPECT_EQ(WeReadStore::promoteShelfBook("missing", 400), WeReadStore::ShelfSortResult::Ok);
  EXPECT_EQ(WeReadStore::promoteShelfBook("book-1", 0), WeReadStore::ShelfSortResult::Ok);
}

TEST_F(WeReadStoreTest, DefersPromotionForLargeShelfWithoutRewritingIt) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  for (uint32_t i = 0; i <= WeReadStore::kLargeShelfThreshold; ++i) {
    WeReadStore::ShelfRecord record;
    snprintf(record.bookId, sizeof(record.bookId), "book-%03u", static_cast<unsigned>(i));
    ASSERT_TRUE(shelf.append(&record));
  }
  ASSERT_TRUE(shelf.finish());

  EXPECT_EQ(WeReadStore::promoteShelfBook("book-500", 123), WeReadStore::ShelfSortResult::Degraded);
  HalFile file;
  uint32_t count = 0;
  ASSERT_TRUE(WeReadStore::openShelf(file, count));
  WeReadStore::ShelfRecord first;
  ASSERT_TRUE(WeReadStore::readShelfRecord(file, 0, first));
  EXPECT_STREQ(first.bookId, "book-000");
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/shelf.bin.part"));
}

TEST_F(WeReadStoreTest, RejectsCorruptShelfSortWithoutLeavingPartialReplacement) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  ASSERT_TRUE(Storage.writeFile(WeReadStore::kShelfPath, "corrupt"));
  EXPECT_EQ(WeReadStore::sortShelfByRecent(), WeReadStore::ShelfSortResult::StorageError);
  EXPECT_EQ(WeReadStore::promoteShelfBook("book", 100), WeReadStore::ShelfSortResult::StorageError);
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/shelf.bin.part"));
  EXPECT_TRUE(Storage.exists(WeReadStore::kShelfPath));
}

TEST_F(WeReadStoreTest, OpensValidEmptyShelfIndex) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  ASSERT_TRUE(shelf.finish());

  HalFile shelfFile;
  uint32_t count = 1;
  EXPECT_TRUE(WeReadStore::openShelf(shelfFile, count));
  EXPECT_EQ(count, 0U);
}

TEST(WeReadStorePaths, UsesVersionedCoverThumbnailPath) {
  EXPECT_EQ(WeReadStore::coverPath("/book"), "/book/cover.v2.bmp");
  EXPECT_EQ(WeReadStore::kCoverThumbWidth, 112);
  EXPECT_EQ(WeReadStore::kCoverThumbHeight, 164);
}

TEST_F(WeReadStoreTest, WritesCurrentImageIndexesAndRejectsLegacyOrCorruptIndexes) {
  ASSERT_TRUE(Storage.ensureDirectoryExists("/work"));
  const std::string path = WeReadStore::imageIndexPath("/work", 7);
  WeReadStore::IndexWriter images;
  ASSERT_TRUE(images.begin(path, WeReadStore::kImageMagic, sizeof(WeReadStore::ImageRecord)));
  ASSERT_TRUE(images.finish());

  uint32_t count = 1;
  {
    HalFile file;
    ASSERT_TRUE(WeReadStore::openImageIndex(path, file, count));
    EXPECT_EQ(count, 0U);
  }

  ASSERT_TRUE(images.begin(path, WeReadStore::kImageMagic, sizeof(WeReadStore::ImageRecord)));
  WeReadStore::ImageRecord first;
  strcpy(first.href, "images/ch000007-0.jpg");
  strcpy(first.url, "https://res.weread.qq.com/a.jpg?token=1");
  ASSERT_TRUE(images.append(&first));
  WeReadStore::ImageRecord second;
  strcpy(second.href, "images/ch000007-1.png");
  strcpy(second.url, "https://cdn.example/b.png");
  ASSERT_TRUE(images.append(&second));
  ASSERT_TRUE(images.finish());

  {
    HalFile file;
    ASSERT_TRUE(WeReadStore::openImageIndex(path, file, count));
    ASSERT_EQ(count, 2U);
    WeReadStore::ImageRecord loaded;
    ASSERT_TRUE(WeReadStore::readImageRecord(file, 1, loaded));
    EXPECT_STREQ(loaded.href, second.href);
    EXPECT_STREQ(loaded.url, second.url);
  }

  static constexpr uint32_t kLegacyImageMagic = 0x31495257;  // WRI1
  ASSERT_TRUE(images.begin(path, kLegacyImageMagic, sizeof(WeReadStore::ImageRecord)));
  ASSERT_TRUE(images.finish());
  {
    HalFile rejected;
    EXPECT_FALSE(WeReadStore::openImageIndex(path, rejected, count));
  }

  ASSERT_TRUE(images.begin(path, WeReadStore::kImageMagic, sizeof(WeReadStore::ImageRecord)));
  ASSERT_TRUE(images.finish());

  std::ofstream corrupt(hostPath(path.c_str()), std::ios::binary | std::ios::app);
  ASSERT_TRUE(corrupt.good());
  corrupt.put('\0');
  corrupt.close();
  HalFile rejected;
  EXPECT_FALSE(WeReadStore::openImageIndex(path, rejected, count));
}

TEST_F(WeReadStoreTest, UpdatesTransientImageWorkIndexAndRebuildsCorruption) {
  ASSERT_TRUE(Storage.ensureDirectoryExists("/work"));
  const std::string path = WeReadStore::imageWorkPath("/work");
  WeReadStore::IndexWriter writer;
  ASSERT_TRUE(writer.begin(path, WeReadStore::kImageWorkMagic, sizeof(WeReadStore::ImageWorkRecord)));

  WeReadStore::ImageWorkRecord first;
  strcpy(first.image.href, "images/ch000001-0.jpg");
  strcpy(first.image.url, "https://res.weread.qq.com/a.jpg");
  ASSERT_TRUE(writer.append(&first));
  WeReadStore::ImageWorkRecord second;
  strcpy(second.image.href, "images/ch000001-1.png");
  strcpy(second.image.url, "https://cdn.example/b.png");
  second.state = WeReadStore::ImageWorkState::Complete;
  ASSERT_TRUE(writer.append(&second));
  ASSERT_TRUE(writer.finish());

  uint32_t count = 0;
  {
    HalFile file;
    ASSERT_TRUE(WeReadStore::openImageWorkIndexForUpdate(path, file, count));
    ASSERT_EQ(count, 2U);
    first.attempts = 1;
    first.redirects = 2;
    strcpy(first.image.url, "https://cdn.example/a.jpg");
    ASSERT_TRUE(WeReadStore::updateImageWorkRecord(file, count, 0, first));
    EXPECT_FALSE(WeReadStore::updateImageWorkRecord(file, count, 2, first));
    WeReadStore::ImageWorkRecord loaded;
    ASSERT_TRUE(WeReadStore::readImageWorkRecord(file, 0, loaded));
    EXPECT_STREQ(loaded.image.url, first.image.url);
    EXPECT_EQ(loaded.state, WeReadStore::ImageWorkState::Pending);
    EXPECT_EQ(loaded.attempts, 1U);
    EXPECT_EQ(loaded.redirects, 2U);
  }

  {
    HalFile file;
    ASSERT_TRUE(WeReadStore::openImageWorkIndex(path, file, count));
    WeReadStore::ImageWorkRecord loaded;
    ASSERT_TRUE(WeReadStore::readImageWorkRecord(file, 0, loaded));
    EXPECT_STREQ(loaded.image.url, first.image.url);
    EXPECT_EQ(loaded.attempts, 1U);
    EXPECT_EQ(loaded.redirects, 2U);
  }

  std::ofstream corrupt(hostPath(path.c_str()), std::ios::binary | std::ios::app);
  ASSERT_TRUE(corrupt.good());
  corrupt.put('\0');
  corrupt.close();
  HalFile rejected;
  EXPECT_FALSE(WeReadStore::openImageWorkIndex(path, rejected, count));

  ASSERT_TRUE(writer.begin(path, WeReadStore::kImageWorkMagic, sizeof(WeReadStore::ImageWorkRecord)));
  ASSERT_TRUE(writer.finish());
  HalFile rebuilt;
  ASSERT_TRUE(WeReadStore::openImageWorkIndex(path, rebuilt, count));
  EXPECT_EQ(count, 0U);
}

TEST_F(WeReadStoreTest, PersistsFixedBookOptionsAndDefaultsOnMissingOrCorruptFiles) {
  ASSERT_TRUE(Storage.ensureDirectoryExists("/work"));
  WeReadStore::BookOptions options;
  EXPECT_FALSE(WeReadStore::loadBookOptions("/work", options));
  EXPECT_EQ(options.imagePolicy, WeReadStore::ImagePolicy::Embed);

  options.imagePolicy = WeReadStore::ImagePolicy::Exclude;
  ASSERT_TRUE(WeReadStore::saveBookOptions("/work", options));
  EXPECT_EQ(std::filesystem::file_size(hostPath("/work/options.bin")), 8U);
  WeReadStore::BookOptions loaded;
  ASSERT_TRUE(WeReadStore::loadBookOptions("/work", loaded));
  EXPECT_EQ(loaded.imagePolicy, WeReadStore::ImagePolicy::Exclude);
  EXPECT_FALSE(Storage.exists("/work/options.bin.part"));

  std::fstream corrupt(hostPath("/work/options.bin"), std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(corrupt.good());
  corrupt.put('\0');
  corrupt.close();
  EXPECT_FALSE(WeReadStore::loadBookOptions("/work", loaded));
  EXPECT_EQ(loaded.imagePolicy, WeReadStore::ImagePolicy::Embed);
}

TEST_F(WeReadStoreTest, PersistsAndValidatesOneShotInitialProgress) {
  float loaded = -1.0f;
  EXPECT_FALSE(WeReadStore::loadInitialProgress("progress-book", loaded));
  EXPECT_FLOAT_EQ(loaded, 0.0f);

  for (const float fraction : {0.0f, 0.456789f, 1.0f}) {
    ASSERT_TRUE(WeReadStore::saveInitialProgress("progress-book", fraction));
    EXPECT_EQ(std::filesystem::file_size(hostPath(WeReadStore::initialProgressPath("progress-book").c_str())), 8U);
    EXPECT_FALSE(Storage.exists((WeReadStore::initialProgressPath("progress-book") + ".part").c_str()));
    ASSERT_TRUE(WeReadStore::loadInitialProgress("progress-book", loaded));
    EXPECT_NEAR(loaded, fraction, 0.000001f);
  }

  EXPECT_FALSE(WeReadStore::saveInitialProgress("progress-book", -0.01f));
  EXPECT_FALSE(WeReadStore::saveInitialProgress("progress-book", 1.01f));
  EXPECT_FALSE(WeReadStore::saveInitialProgress("progress-book", std::numeric_limits<float>::quiet_NaN()));
  ASSERT_TRUE(WeReadStore::loadInitialProgress("progress-book", loaded));
  EXPECT_FLOAT_EQ(loaded, 1.0f);

  const auto path = hostPath(WeReadStore::initialProgressPath("progress-book").c_str());
  WeReadStore::InitialProgress corrupt;
  corrupt.magic = 0;
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&corrupt), sizeof(corrupt));
  }
  EXPECT_FALSE(WeReadStore::loadInitialProgress("progress-book", loaded));
  corrupt.magic = WeReadStore::kInitialProgressMagic;
  corrupt.millionths = 1000001;
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&corrupt), sizeof(corrupt));
  }
  EXPECT_FALSE(WeReadStore::loadInitialProgress("progress-book", loaded));
  std::filesystem::resize_file(path, 7);
  EXPECT_FALSE(WeReadStore::loadInitialProgress("progress-book", loaded));

  EXPECT_TRUE(WeReadStore::clearInitialProgress("progress-book"));
  EXPECT_FALSE(Storage.exists(WeReadStore::initialProgressPath("progress-book").c_str()));
  EXPECT_TRUE(WeReadStore::clearInitialProgress("progress-book"));
}

TEST_F(WeReadStoreTest, StreamsAndValidatesAtomicBookDetailFiles) {
  ASSERT_TRUE(Storage.ensureDirectoryExists("/work"));
  WeReadStore::BookDetailHeader header;
  strcpy(header.title, "测试书");
  strcpy(header.author, "作者");
  strcpy(header.publisher, "出版社");
  strcpy(header.category, "文学");
  strcpy(header.coverUrl, "https://cdn.example/cover.jpg");
  header.newRating = 890;
  header.newRatingCount = 1234;
  header.totalWords = 456789;

  WeReadStore::BookDetailWriter writer;
  ASSERT_TRUE(writer.begin("/work"));
  constexpr char first[] = "第一段";
  constexpr char second[] = "，第二段。";
  ASSERT_TRUE(writer.appendIntro(reinterpret_cast<const uint8_t*>(first), sizeof(first) - 1));
  ASSERT_TRUE(writer.appendIntro(reinterpret_cast<const uint8_t*>(second), sizeof(second) - 1));
  ASSERT_TRUE(writer.finish(header));
  EXPECT_FALSE(Storage.exists("/work/detail.bin.part"));

  HalFile file;
  WeReadStore::BookDetailHeader loaded;
  ASSERT_TRUE(WeReadStore::openBookDetail("/work", loaded, file));
  EXPECT_STREQ(loaded.title, header.title);
  EXPECT_EQ(loaded.newRating, 890U);
  EXPECT_EQ(loaded.totalWords, 456789U);
  ASSERT_EQ(loaded.introLength, sizeof(first) + sizeof(second) - 2);
  std::string intro(loaded.introLength, '\0');
  ASSERT_EQ(file.read(intro.data(), intro.size()), static_cast<int>(intro.size()));
  EXPECT_EQ(intro, "第一段，第二段。");
  file.close();

  std::ifstream goodFile(hostPath("/work/detail.bin"), std::ios::binary);
  const std::vector<char> good((std::istreambuf_iterator<char>(goodFile)), std::istreambuf_iterator<char>());
  const auto rejectMutation = [this, &good](const size_t offset, const char value) {
    std::vector<char> damaged = good;
    damaged[offset] = value;
    std::ofstream output(hostPath("/work/detail.bin"), std::ios::binary | std::ios::trunc);
    output.write(damaged.data(), static_cast<std::streamsize>(damaged.size()));
    output.close();
    HalFile rejected;
    WeReadStore::BookDetailHeader rejectedHeader;
    EXPECT_FALSE(WeReadStore::openBookDetail("/work", rejectedHeader, rejected));
  };
  rejectMutation(offsetof(WeReadStore::BookDetailHeader, version), '\0');
  rejectMutation(offsetof(WeReadStore::BookDetailHeader, headerSize), '\1');
  rejectMutation(offsetof(WeReadStore::BookDetailHeader, reserved), '\1');

  std::ofstream shortFile(hostPath("/work/detail.bin"), std::ios::binary | std::ios::trunc);
  shortFile.write(good.data(), static_cast<std::streamsize>(good.size() - 1));
  shortFile.close();
  HalFile rejected;
  EXPECT_FALSE(WeReadStore::openBookDetail("/work", loaded, rejected));
}

TEST_F(WeReadStoreTest, CapsBookIntroductionWithoutSplittingDecoderChunks) {
  ASSERT_TRUE(Storage.ensureDirectoryExists("/work"));
  WeReadStore::BookDetailWriter writer;
  ASSERT_TRUE(writer.begin("/work"));
  std::vector<uint8_t> full(WeReadStore::kMaxBookIntroBytes, 'a');
  ASSERT_TRUE(writer.appendIntro(full.data(), full.size()));
  constexpr uint8_t extra[] = {0xE4, 0xB8, 0xAD};
  ASSERT_TRUE(writer.appendIntro(extra, sizeof(extra)));
  WeReadStore::BookDetailHeader header;
  strcpy(header.title, "长简介");
  ASSERT_TRUE(writer.finish(header));

  HalFile file;
  WeReadStore::BookDetailHeader loaded;
  ASSERT_TRUE(WeReadStore::openBookDetail("/work", loaded, file));
  EXPECT_EQ(loaded.introLength, WeReadStore::kMaxBookIntroBytes);
  EXPECT_NE(loaded.flags & WeReadStore::kBookDetailIntroTruncated, 0U);
}

TEST_F(WeReadStoreTest, SessionRoundTripsOnlyWhitelistedCookiesAndRejectsBadMagic) {
  WeReadStore::Session session;
  ASSERT_TRUE(session.setCookie("wr_vid", "wrong", 5));
  ASSERT_TRUE(session.setCookie("wr_vid", "12345", 5));
  ASSERT_TRUE(session.setCookie("wr_skey", "old", 3));
  ASSERT_TRUE(session.setCookie("wr_skey", "secret", 6));
  ASSERT_TRUE(session.setCookie("wr_rt", "refresh", 7));
  EXPECT_FALSE(session.setCookie("other", "leak", 4));
  ASSERT_TRUE(WeReadStore::saveSession(session));

  WeReadStore::Session loaded;
  ASSERT_TRUE(WeReadStore::loadSession(loaded));
  EXPECT_STREQ(loaded.vid, "12345");
  EXPECT_STREQ(loaded.skey, "secret");
  EXPECT_STREQ(loaded.rt, "refresh");

  ASSERT_TRUE(session.setCookie("wr_rt", "", 0));
  char cookie[128];
  ASSERT_TRUE(session.cookieHeader(cookie, sizeof(cookie)));
  EXPECT_EQ(strstr(cookie, "wr_rt"), nullptr);
  ASSERT_TRUE(WeReadStore::saveSession(session));

  std::ofstream trailing(hostPath(WeReadStore::kSessionPath), std::ios::binary | std::ios::app);
  ASSERT_TRUE(trailing.good());
  trailing.put('X');
  trailing.close();
  EXPECT_FALSE(WeReadStore::loadSession(loaded));

  std::fstream file(hostPath(WeReadStore::kSessionPath), std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(file.good());
  file.seekp(0);
  file.write("BAD", 3);
  file.close();
  EXPECT_FALSE(WeReadStore::loadSession(loaded));

  ASSERT_TRUE(Storage.writeFile(WeReadStore::kSessionPath, "WRD3\n12345\nsecret\nrefresh\n"));
  EXPECT_FALSE(WeReadStore::loadSession(loaded));
}

TEST_F(WeReadStoreTest, DisclaimerAcceptanceRequiresExactMarker) {
  EXPECT_FALSE(WeReadStore::hasAcceptedDisclaimer());
  ASSERT_TRUE(WeReadStore::acceptDisclaimer());
  EXPECT_TRUE(WeReadStore::hasAcceptedDisclaimer());

  const auto overwriteMarker = [this](const char* value, const size_t size) {
    std::ofstream file(hostPath(WeReadStore::kDisclaimerAcceptancePath), std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file.good());
    file.write(value, static_cast<std::streamsize>(size));
    file.close();
  };

  overwriteMarker("", 0);
  EXPECT_FALSE(WeReadStore::hasAcceptedDisclaimer());
  overwriteMarker("WRD1", 4);
  EXPECT_FALSE(WeReadStore::hasAcceptedDisclaimer());
  overwriteMarker("BAD1\n", 5);
  EXPECT_FALSE(WeReadStore::hasAcceptedDisclaimer());
}

TEST_F(WeReadStoreTest, ClearsSessionAndShelfButPreservesDownloadedContent) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  ASSERT_TRUE(WeReadStore::acceptDisclaimer());
  WeReadStore::Session session;
  ASSERT_TRUE(session.setCookie("wr_vid", "12345", 5));
  ASSERT_TRUE(session.setCookie("wr_skey", "secret", 6));
  ASSERT_TRUE(WeReadStore::saveSession(session));
  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  WeReadStore::ShelfRecord record;
  strcpy(record.bookId, "book-1");
  strcpy(record.title, "Test Book");
  ASSERT_TRUE(shelf.append(&record));
  ASSERT_TRUE(shelf.finish());
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/shelf.bin.part", "partial"));

  ASSERT_TRUE(Storage.ensureDirectoryExists("/.crosspoint/weread/book-1"));
  ASSERT_TRUE(Storage.ensureDirectoryExists("/.crosspoint/weread/book-1/chapters"));
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/book-1/toc.bin", "toc"));
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/book-1/chapters/000000.xhtml", "chapter"));
  const std::string bookPath = WeReadStore::finalBookPath(record);
  EXPECT_EQ(bookPath, "/WeRead/Test Book-book-1.epub");
  ASSERT_TRUE(Storage.ensureDirectoryExists("/WeRead"));
  ASSERT_TRUE(Storage.writeFile(bookPath.c_str(), "epub"));

  ASSERT_TRUE(WeReadStore::clearSession());
  ASSERT_TRUE(WeReadStore::clearShelf());
  EXPECT_FALSE(Storage.exists(WeReadStore::kSessionPath));
  EXPECT_FALSE(Storage.exists(WeReadStore::kShelfPath));
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/shelf.bin.part"));
  EXPECT_TRUE(WeReadStore::hasAcceptedDisclaimer());
  EXPECT_TRUE(Storage.exists("/.crosspoint/weread/book-1/toc.bin"));
  EXPECT_TRUE(Storage.exists("/.crosspoint/weread/book-1/chapters/000000.xhtml"));
  EXPECT_TRUE(Storage.exists(bookPath.c_str()));

  HalFile missingShelf;
  uint32_t count = 0;
  EXPECT_FALSE(WeReadStore::openShelf(missingShelf, count));
  EXPECT_TRUE(WeReadStore::clearSession());
  EXPECT_TRUE(WeReadStore::clearShelf());
}

TEST_F(WeReadStoreTest, ClearsWeReadCacheButPreservesAccountAndDownloadedBooks) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  ASSERT_TRUE(WeReadStore::acceptDisclaimer());
  WeReadStore::Session session;
  ASSERT_TRUE(session.setCookie("wr_vid", "12345", 5));
  ASSERT_TRUE(session.setCookie("wr_skey", "secret", 6));
  ASSERT_TRUE(WeReadStore::saveSession(session));
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/cache.version", "legacy"));

  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  ASSERT_TRUE(shelf.finish());
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/shelf.bin.part", "partial"));
  ASSERT_TRUE(Storage.ensureDirectoryExists("/.crosspoint/weread/book-1/chapters"));
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/book-1/cover.bmp", "legacy"));
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/book-1/cover.v2.bmp", "current"));
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/book-1/chapters/000000.xhtml", "chapter"));
  ASSERT_TRUE(Storage.ensureDirectoryExists("/.crosspoint/weread/browse-cache/book-1/slot0"));
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/browse-cache/book-1/slot0/page.txt", "review"));
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/weread/detail.bin.part", "partial"));
  ASSERT_TRUE(Storage.ensureDirectoryExists("/WeRead"));
  ASSERT_TRUE(Storage.writeFile("/WeRead/Cached Book.epub", "epub"));
  ASSERT_TRUE(Storage.ensureDirectoryExists("/.crosspoint/epub_123"));
  ASSERT_TRUE(Storage.writeFile("/.crosspoint/epub_123/progress.bin", "progress"));

  ASSERT_TRUE(WeReadStore::clearCache());
  EXPECT_TRUE(Storage.exists(WeReadStore::kSessionPath));
  EXPECT_TRUE(WeReadStore::hasAcceptedDisclaimer());
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/cache.version"));
  EXPECT_FALSE(Storage.exists(WeReadStore::kShelfPath));
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/shelf.bin.part"));
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/book-1"));
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/browse-cache"));
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/detail.bin.part"));
  EXPECT_TRUE(Storage.exists("/WeRead/Cached Book.epub"));
  EXPECT_TRUE(Storage.exists("/.crosspoint/epub_123/progress.bin"));
  EXPECT_TRUE(WeReadStore::clearCache());
}

TEST_F(WeReadStoreTest, ClearingMissingWeReadCacheIsIdempotent) {
  EXPECT_TRUE(WeReadStore::clearCache());
  ASSERT_TRUE(WeReadStore::ensureRoot());
  EXPECT_TRUE(WeReadStore::clearCache());
}

TEST_F(WeReadStoreTest, AtomicReplaceRecoversInterruptedBackupBeforeReplacing) {
  ASSERT_TRUE(Storage.ensureDirectoryExists("/work"));
  ASSERT_TRUE(Storage.writeFile("/work/book.epub", "old"));
  ASSERT_TRUE(Storage.rename("/work/book.epub", "/work/book.epub.bak"));
  ASSERT_TRUE(Storage.writeFile("/work/book.epub.part", "new"));

  ASSERT_TRUE(WeReadStore::atomicReplace("/work/book.epub.part", "/work/book.epub"));
  EXPECT_EQ(Storage.readFile("/work/book.epub"), "new");
  EXPECT_FALSE(Storage.exists("/work/book.epub.bak"));

  ASSERT_TRUE(Storage.rename("/work/book.epub", "/work/book.epub.bak"));
  EXPECT_FALSE(WeReadStore::atomicReplace("/work/missing.part", "/work/book.epub"));
  EXPECT_EQ(Storage.readFile("/work/book.epub"), "new");
  EXPECT_FALSE(Storage.exists("/work/book.epub.bak"));
}

TEST_F(WeReadStoreTest, WritesPngCoverWithEmbeddedBodyImagesInReadingOrder) {
  ASSERT_TRUE(Storage.ensureDirectoryExists("/work"));
  static constexpr char kMimetype[] = "application/epub+zip";
  static constexpr char kContainer[] =
      "<?xml version=\"1.0\"?><container><rootfiles><rootfile full-path=\"OEBPS/content.opf\"/>"
      "</rootfiles></container>";
  static constexpr char kOpf[] =
      "<package><manifest><item id=\"nav\" href=\"nav.xhtml\"/>"
      "<item id=\"cover-image\" href=\"cover.png\" media-type=\"image/png\" properties=\"cover-image\"/>"
      "<item id=\"ch000000\" "
      "href=\"ch000000.xhtml\"/><item id=\"ch000001\" href=\"ch000001.xhtml\"/>"
      "<item id=\"img000000_0\" href=\"images/ch000000-0.png\" media-type=\"image/png\"/></manifest>"
      "<spine><itemref idref=\"ch000000\"/><itemref idref=\"ch000001\"/></spine></package>";
  static constexpr char kNav[] =
      "<html><nav><ol><li><a href=\"ch000000.xhtml\">一</a></li><li><a "
      "href=\"ch000001.xhtml\">二</a></li></ol></nav></html>";
  std::string largeChapter = "<html><body><p>一</p><img src=\"images/ch000000-0.png\" alt=\"插图\"/><p>";
  largeChapter.append(8192, 'x');
  largeChapter += "</p></body></html>";
  {
    HalFile chapter;
    ASSERT_TRUE(Storage.openFileForWrite("WR", "/work/ch0.xhtml", chapter));
    ASSERT_EQ(chapter.write(largeChapter.data(), largeChapter.size()), largeChapter.size());
  }
  ASSERT_TRUE(Storage.writeFile("/work/ch1.xhtml", "<html><body><p>二</p></body></html>"));
  static constexpr uint8_t kPng[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  {
    HalFile image;
    ASSERT_TRUE(Storage.openFileForWrite("WR", "/work/image.png", image));
    ASSERT_EQ(image.write(kPng, sizeof(kPng)), sizeof(kPng));
  }
  {
    HalFile cover;
    ASSERT_TRUE(Storage.openFileForWrite("WR", "/work/cover.png", cover));
    ASSERT_EQ(cover.write(kPng, sizeof(kPng)), sizeof(kPng));
  }

  WeReadStore::StoreOnlyZipWriter zip;
  std::array<uint8_t, 4096> zipBuffer{};
  ASSERT_TRUE(zip.begin("/work/book.epub", "/work/central.part", zipBuffer.data(), zipBuffer.size()));
  ASSERT_TRUE(zip.addBuffer("mimetype", reinterpret_cast<const uint8_t*>(kMimetype), strlen(kMimetype)));
  ASSERT_TRUE(
      zip.addBuffer("META-INF/container.xml", reinterpret_cast<const uint8_t*>(kContainer), strlen(kContainer)));
  ASSERT_TRUE(zip.addBuffer("OEBPS/content.opf", reinterpret_cast<const uint8_t*>(kOpf), strlen(kOpf)));
  ASSERT_TRUE(zip.addBuffer("OEBPS/nav.xhtml", reinterpret_cast<const uint8_t*>(kNav), strlen(kNav)));
  ASSERT_TRUE(zip.addFile("OEBPS/cover.png", "/work/cover.png"));
  unsigned callbackCount = 0;
  const auto countWork = [](void* context) { ++*static_cast<unsigned*>(context); };
  ASSERT_TRUE(zip.addFile("OEBPS/ch000000.xhtml", "/work/ch0.xhtml", countWork, &callbackCount));
  EXPECT_GE(callbackCount, 3U);
  const unsigned copyCallbackCount = callbackCount;
  ASSERT_TRUE(zip.addFile("OEBPS/ch000001.xhtml", "/work/ch1.xhtml"));
  ASSERT_TRUE(zip.addFile("OEBPS/images/ch000000-0.png", "/work/image.png"));
  ASSERT_TRUE(zip.finish(countWork, &callbackCount));
  EXPECT_EQ(callbackCount, copyCallbackCount + 8);
  ASSERT_TRUE(WeReadStore::looksLikeZip("/work/book.epub"));
  EXPECT_FALSE(Storage.exists("/work/central.part"));

  std::ifstream input(hostPath("/work/book.epub"), std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const std::string archive(bytes.begin(), bytes.end());
  EXPECT_NE(archive.find("src=\"images/ch000000-0.png\""), std::string::npos);
  EXPECT_NE(archive.find("id=\"cover-image\" href=\"cover.png\" media-type=\"image/png\" properties=\"cover-image\""),
            std::string::npos);
  EXPECT_EQ(archive.find("images/failed.jpg"), std::string::npos);
  ASSERT_GE(bytes.size(), 22U);
  const size_t eocd = bytes.size() - 22;
  ASSERT_EQ(readLe32(bytes, eocd), 0x06054B50U);
  ASSERT_EQ(readLe16(bytes, eocd + 10), 8U);
  size_t cursor = readLe32(bytes, eocd + 16);
  const std::vector<std::string> expected = {
      "mimetype",        "META-INF/container.xml", "OEBPS/content.opf",    "OEBPS/nav.xhtml",
      "OEBPS/cover.png", "OEBPS/ch000000.xhtml",   "OEBPS/ch000001.xhtml", "OEBPS/images/ch000000-0.png"};
  for (const auto& name : expected) {
    ASSERT_EQ(readLe32(bytes, cursor), 0x02014B50U);
    EXPECT_EQ(readLe16(bytes, cursor + 10), 0U);
    const size_t nameLen = readLe16(bytes, cursor + 28);
    const size_t extraLen = readLe16(bytes, cursor + 30);
    const size_t commentLen = readLe16(bytes, cursor + 32);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(&bytes[cursor + 46]), nameLen), name);
    cursor += 46 + nameLen + extraLen + commentLen;
  }

  ASSERT_EQ(readLe32(bytes, 0), 0x04034B50U);
  ASSERT_EQ(readLe16(bytes, 8), 0U);
  const size_t firstNameLen = readLe16(bytes, 26);
  const size_t firstExtraLen = readLe16(bytes, 28);
  const size_t firstData = 30 + firstNameLen + firstExtraLen;
  ASSERT_EQ(std::string(reinterpret_cast<const char*>(&bytes[30]), firstNameLen), "mimetype");
  ASSERT_EQ(std::string(reinterpret_cast<const char*>(&bytes[firstData]), strlen(kMimetype)), kMimetype);

  std::fstream corrupt(hostPath("/work/book.epub"), std::ios::binary | std::ios::in | std::ios::out);
  ASSERT_TRUE(corrupt.good());
  corrupt.seekp(static_cast<std::streamoff>(readLe32(bytes, eocd + 16)));
  corrupt.put('\0');
  corrupt.close();
  EXPECT_FALSE(WeReadStore::looksLikeZip("/work/book.epub"));

  WeReadStore::StoreOnlyZipWriter incomplete;
  ASSERT_TRUE(
      incomplete.begin("/work/incomplete.epub", "/work/incomplete.central", zipBuffer.data(), zipBuffer.size()));
  ASSERT_TRUE(incomplete.addBuffer("mimetype", reinterpret_cast<const uint8_t*>(kMimetype), strlen(kMimetype)));
  ASSERT_TRUE(incomplete.addBuffer("one", reinterpret_cast<const uint8_t*>("1"), 1));
  ASSERT_TRUE(incomplete.addBuffer("two", reinterpret_cast<const uint8_t*>("2"), 1));
  ASSERT_TRUE(incomplete.finish());
  EXPECT_FALSE(WeReadStore::looksLikeZip("/work/incomplete.epub"));
}

TEST_F(WeReadStoreTest, WritesJpegCoverWithExcludedBodyImagesAndAllowsCoverlessEpub) {
  ASSERT_TRUE(Storage.ensureDirectoryExists("/work"));
  static constexpr char kMimetype[] = "application/epub+zip";
  static constexpr char kContainer[] =
      "<?xml version=\"1.0\"?><container><rootfiles><rootfile full-path=\"OEBPS/content.opf\"/>"
      "</rootfiles></container>";
  static constexpr char kCoverOpf[] =
      "<package><manifest><item id=\"cover-image\" href=\"cover.jpg\" media-type=\"image/jpeg\" "
      "properties=\"cover-image\"/></manifest><spine/></package>";
  static constexpr char kCoverlessOpf[] = "<package><manifest/><spine/></package>";
  static constexpr uint8_t kJpeg[] = {0xFF, 0xD8, 0xFF, 0xD9};
  {
    HalFile cover;
    ASSERT_TRUE(Storage.openFileForWrite("WR", "/work/cover.jpg", cover));
    ASSERT_EQ(cover.write(kJpeg, sizeof(kJpeg)), sizeof(kJpeg));
  }

  std::array<uint8_t, 4096> zipBuffer{};
  WeReadStore::StoreOnlyZipWriter withCover;
  ASSERT_TRUE(withCover.begin("/work/jpeg-cover.epub", "/work/jpeg-cover.central", zipBuffer.data(), zipBuffer.size()));
  ASSERT_TRUE(withCover.addBuffer("mimetype", reinterpret_cast<const uint8_t*>(kMimetype), strlen(kMimetype)));
  ASSERT_TRUE(
      withCover.addBuffer("META-INF/container.xml", reinterpret_cast<const uint8_t*>(kContainer), strlen(kContainer)));
  ASSERT_TRUE(withCover.addBuffer("OEBPS/content.opf", reinterpret_cast<const uint8_t*>(kCoverOpf), strlen(kCoverOpf)));
  ASSERT_TRUE(withCover.addFile("OEBPS/cover.jpg", "/work/cover.jpg"));
  ASSERT_TRUE(withCover.finish());
  ASSERT_TRUE(WeReadStore::looksLikeZip("/work/jpeg-cover.epub"));

  std::ifstream input(hostPath("/work/jpeg-cover.epub"), std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::string archive((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  EXPECT_NE(archive.find("id=\"cover-image\" href=\"cover.jpg\" media-type=\"image/jpeg\" properties=\"cover-image\""),
            std::string::npos);

  WeReadStore::StoreOnlyZipWriter coverless;
  ASSERT_TRUE(coverless.begin("/work/coverless.epub", "/work/coverless.central", zipBuffer.data(), zipBuffer.size()));
  ASSERT_TRUE(coverless.addBuffer("mimetype", reinterpret_cast<const uint8_t*>(kMimetype), strlen(kMimetype)));
  ASSERT_TRUE(
      coverless.addBuffer("META-INF/container.xml", reinterpret_cast<const uint8_t*>(kContainer), strlen(kContainer)));
  ASSERT_TRUE(
      coverless.addBuffer("OEBPS/content.opf", reinterpret_cast<const uint8_t*>(kCoverlessOpf), strlen(kCoverlessOpf)));
  ASSERT_TRUE(coverless.finish());
  EXPECT_TRUE(WeReadStore::looksLikeZip("/work/coverless.epub"));
}

TEST_F(WeReadStoreTest, StreamsAllBrowseResponseShapesAcrossArbitraryChunks) {
  struct Case {
    WeReadBrowse::Kind kind;
    const char* json;
    const char* text;
  };
  const Case cases[] = {
      {WeReadBrowse::Kind::PopularHighlights,
       R"({"bestBookMarks":{"items":[{"chapterUid":"c1","markText":"热门\u5212线","totalCount":12}],"chapters":[]}})",
       "热门划线"},
      {WeReadBrowse::Kind::MyHighlights,
       R"({"updated":[{"chapterUid":"c2","markText":"我的划线","createTime":1700000000}],"chapters":[]})", "我的划线"},
      {WeReadBrowse::Kind::PopularReviews,
       R"({"reviewsHasMore":true,"reviews":[{"idx":9,"review":{"review":{"htmlContent":"<p>好 &amp; \uD83D\uDE00 &#x1F680;</p>","star":50,"author":{"name":"读者"}}}}],"synckey":123456})",
       "好 & 😀 🚀"},
  };

  WeReadBrowse::CacheManifest manifest;
  beginBrowseCache(manifest);
  for (size_t caseIndex = 0; caseIndex < std::size(cases); ++caseIndex) {
    writeBrowsePage(manifest, cases[caseIndex].kind, 0, cases[caseIndex].json);

    WeReadBrowse::PageHeader header;
    HalFile index;
    HalFile text;
    ASSERT_TRUE(WeReadBrowse::openPage(kBrowseBook, manifest, cases[caseIndex].kind, 0, header, index, text));
    ASSERT_EQ(header.count, 1U);
    WeReadBrowse::Record record;
    ASSERT_TRUE(WeReadBrowse::readRecord(index, header, 0, record));
    std::string body(record.textLength, '\0');
    ASSERT_TRUE(text.seek(record.textOffset));
    ASSERT_EQ(text.read(body.data(), body.size()), static_cast<int>(body.size()));
    EXPECT_EQ(body, cases[caseIndex].text);
    if (cases[caseIndex].kind == WeReadBrowse::Kind::PopularReviews) {
      EXPECT_STREQ(record.author, "读者");
      EXPECT_EQ(record.rating, 50U);
      EXPECT_EQ(header.nextMaxIdx, 9U);
      EXPECT_EQ(header.nextSyncKey, 123456U);
    }
  }
}

TEST_F(WeReadStoreTest, BrowseParserHandlesEmptyMissingFieldsAndExpiredSession) {
  WeReadBrowse::CacheManifest manifest;
  beginBrowseCache(manifest);
  static constexpr char kEmpty[] = R"({})";
  writeBrowsePage(manifest, WeReadBrowse::Kind::MyHighlights, 0, kEmpty);
  EXPECT_EQ(manifest.recordCounts[WeReadBrowse::kindIndex(WeReadBrowse::Kind::MyHighlights)], 0U);

  static constexpr char kMissing[] = R"({"items":[{"totalCount":2},{"markText":"可读"}]})";
  writeBrowsePage(manifest, WeReadBrowse::Kind::PopularHighlights, 0, kMissing);
  EXPECT_EQ(manifest.recordCounts[WeReadBrowse::kindIndex(WeReadBrowse::Kind::PopularHighlights)], 1U);

  WeReadBrowse::ResponseParser expired(kBrowseBook, manifest.activeSlot, WeReadBrowse::Kind::PopularReviews, 0);
  ASSERT_TRUE(expired.reset());
  static constexpr char kExpired[] = R"({"errCode":-2012})";
  ASSERT_TRUE(expired.feed(reinterpret_cast<const uint8_t*>(kExpired), sizeof(kExpired) - 1));
  ASSERT_TRUE(expired.finish());
  EXPECT_EQ(expired.errorCode(), -2012);
  EXPECT_FALSE(Storage.exists(
      WeReadBrowse::indexPath(kBrowseBook, manifest.activeSlot, WeReadBrowse::Kind::PopularReviews, 0).c_str()));

  WeReadBrowse::ResponseParser unexpected(kBrowseBook, manifest.activeSlot, WeReadBrowse::Kind::MyHighlights, 1);
  ASSERT_TRUE(unexpected.reset());
  static constexpr char kUnexpected[] = R"({"book":{}})";
  ASSERT_TRUE(unexpected.feed(reinterpret_cast<const uint8_t*>(kUnexpected), sizeof(kUnexpected) - 1));
  EXPECT_FALSE(unexpected.finish());
  EXPECT_FALSE(unexpected.storageFailed());
}

TEST_F(WeReadStoreTest, BrowseCacheCommitIsAtomicAndBoundToAccount) {
  WeReadBrowse::CacheManifest first;
  beginBrowseCache(first);
  writeBrowsePage(first, WeReadBrowse::Kind::PopularHighlights, 0, R"({"items":[{"markText":"old mark"}]})");
  writeBrowsePage(first, WeReadBrowse::Kind::MyHighlights, 0, R"({"updated":[]})");
  writeBrowsePage(first, WeReadBrowse::Kind::PopularReviews, 0,
                  R"({"reviewsHasMore":false,"reviews":[{"idx":1,"review":{"content":"kept"}}],"synckey":3})");
  ASSERT_TRUE(WeReadBrowse::commitCache(kBrowseBook, first));

  WeReadBrowse::CacheManifest loaded;
  ASSERT_TRUE(WeReadBrowse::loadCache(kBrowseBook, kBrowseOwner, loaded));
  EXPECT_EQ(loaded.activeSlot, first.activeSlot);
  EXPECT_FALSE(WeReadBrowse::loadCache(kBrowseBook, "different-owner", loaded));

  WeReadBrowse::CacheManifest replacement;
  beginBrowseCache(replacement);
  ASSERT_NE(replacement.activeSlot, first.activeSlot);
  writeBrowsePage(replacement, WeReadBrowse::Kind::PopularHighlights, 0, R"({"items":[{"markText":"new mark"}]})");
  WeReadBrowse::ResponseParser interrupted(kBrowseBook, replacement.activeSlot, WeReadBrowse::Kind::MyHighlights, 0);
  ASSERT_TRUE(interrupted.reset());
  static constexpr char kBroken[] = R"({"reviews":[{"review":{"content":"replacement)";
  ASSERT_TRUE(interrupted.feed(reinterpret_cast<const uint8_t*>(kBroken), sizeof(kBroken) - 1));
  EXPECT_FALSE(interrupted.finish());
  WeReadBrowse::abortCache(kBrowseBook, replacement.activeSlot);

  ASSERT_TRUE(WeReadBrowse::loadCache(kBrowseBook, kBrowseOwner, loaded));
  EXPECT_EQ(loaded.activeSlot, first.activeSlot);
  {
    WeReadBrowse::PageHeader header;
    HalFile index;
    HalFile text;
    ASSERT_TRUE(
        WeReadBrowse::openPage(kBrowseBook, loaded, WeReadBrowse::Kind::PopularReviews, 0, header, index, text));
    ASSERT_EQ(header.count, 1U);
    WeReadBrowse::Record record;
    ASSERT_TRUE(WeReadBrowse::readRecord(index, header, 0, record));
    std::string body(record.textLength, '\0');
    ASSERT_TRUE(text.seek(record.textOffset));
    ASSERT_EQ(text.read(body.data(), body.size()), static_cast<int>(body.size()));
    EXPECT_EQ(body, "kept");
  }

  WeReadBrowse::CacheManifest completedReplacement;
  beginBrowseCache(completedReplacement);
  ASSERT_NE(completedReplacement.activeSlot, first.activeSlot);
  writeBrowsePage(completedReplacement, WeReadBrowse::Kind::PopularHighlights, 0,
                  R"({"items":[{"markText":"new mark"}]})");
  writeBrowsePage(completedReplacement, WeReadBrowse::Kind::MyHighlights, 0, R"({"updated":[]})");
  writeBrowsePage(completedReplacement, WeReadBrowse::Kind::PopularReviews, 0,
                  R"({"reviewsHasMore":false,"reviews":[{"idx":2,"review":{"content":"fresh"}}],"synckey":4})");
  ASSERT_TRUE(WeReadBrowse::commitCache(kBrowseBook, completedReplacement));
  ASSERT_TRUE(WeReadBrowse::loadCache(kBrowseBook, kBrowseOwner, loaded));
  EXPECT_EQ(loaded.activeSlot, completedReplacement.activeSlot);
  EXPECT_FALSE(Storage.exists(
      WeReadBrowse::indexPath(kBrowseBook, first.activeSlot, WeReadBrowse::Kind::PopularReviews, 0).c_str()));

  const auto manifestPath = hostPath("/.crosspoint/weread/browse-cache/browse-book/cache.bin");
  const auto backupPath = hostPath("/.crosspoint/weread/browse-cache/browse-book/cache.bin.bak");
  std::filesystem::rename(manifestPath, backupPath);
  ASSERT_TRUE(WeReadBrowse::loadCache(kBrowseBook, kBrowseOwner, loaded));
  EXPECT_TRUE(std::filesystem::exists(manifestPath));
  EXPECT_FALSE(std::filesystem::exists(backupPath));

  {
    HalFile damaged = Storage.open(
        WeReadBrowse::indexPath(kBrowseBook, loaded.activeSlot, WeReadBrowse::Kind::PopularReviews, 0).c_str(), O_RDWR);
    ASSERT_TRUE(damaged.isOpen());
    const uint32_t badMagic = 0;
    ASSERT_TRUE(damaged.seek(offsetof(WeReadBrowse::PageHeader, magic)));
    ASSERT_EQ(damaged.write(&badMagic, sizeof(badMagic)), sizeof(badMagic));
  }
  EXPECT_FALSE(WeReadBrowse::loadCache(kBrowseBook, kBrowseOwner, loaded));
  EXPECT_TRUE(WeReadBrowse::clearAllCaches());
  EXPECT_FALSE(Storage.exists("/.crosspoint/weread/browse-cache"));
}

TEST_F(WeReadStoreTest, BrowseParserCapsOversizedRecordText) {
  WeReadBrowse::CacheManifest manifest;
  beginBrowseCache(manifest);
  WeReadBrowse::ResponseParser parser(kBrowseBook, manifest.activeSlot, WeReadBrowse::Kind::PopularReviews, 2);
  ASSERT_TRUE(parser.reset());
  std::string response = R"({"reviews":[{"idx":7,"review":{"content":")";
  response.append(WeReadBrowse::kMaxResponseBytes + 1024, 'x');
  response += R"("}}]})";
  for (size_t offset = 0; offset < response.size(); offset += 257) {
    const size_t chunk = std::min<size_t>(257, response.size() - offset);
    ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(response.data() + offset), chunk));
  }
  ASSERT_TRUE(parser.finish());

  WeReadBrowse::PageHeader header;
  HalFile index;
  HalFile text;
  manifest.pageCounts[WeReadBrowse::kindIndex(WeReadBrowse::Kind::PopularReviews)] = 3;
  ASSERT_TRUE(
      WeReadBrowse::openPage(kBrowseBook, manifest, WeReadBrowse::Kind::PopularReviews, 2, header, index, text));
  ASSERT_EQ(header.count, 1U);
  EXPECT_NE(header.flags & WeReadBrowse::kPageResponseTruncated, 0U);
  WeReadBrowse::Record record;
  ASSERT_TRUE(WeReadBrowse::readRecord(index, header, 0, record));
  EXPECT_EQ(record.textLength, WeReadBrowse::kMaxItemTextBytes);
  EXPECT_NE(record.flags & WeReadBrowse::kRecordTextTruncated, 0U);
}

TEST_F(WeReadStoreTest, BrowseParserCapsTheFinalReviewPageAtTenRecords) {
  WeReadBrowse::CacheManifest manifest;
  beginBrowseCache(manifest);
  WeReadBrowse::ResponseParser parser(kBrowseBook, manifest.activeSlot, WeReadBrowse::Kind::PopularReviews, 2, 10);
  ASSERT_TRUE(parser.reset());
  std::string response = R"({"reviewsHasMore":true,"reviews":[)";
  for (uint32_t index = 0; index < 12; ++index) {
    if (index != 0) response += ',';
    response += R"({"idx":)" + std::to_string(index + 1) + R"(,"review":{"content":"review"}})";
  }
  response += R"(],"synckey":99})";
  ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(response.data()), response.size()));
  ASSERT_TRUE(parser.finish());
  EXPECT_EQ(parser.count(), 10U);
  EXPECT_TRUE(parser.responseTruncated());

  manifest.pageCounts[WeReadBrowse::kindIndex(WeReadBrowse::Kind::PopularReviews)] = 3;
  WeReadBrowse::PageHeader header;
  HalFile index;
  HalFile text;
  ASSERT_TRUE(
      WeReadBrowse::openPage(kBrowseBook, manifest, WeReadBrowse::Kind::PopularReviews, 2, header, index, text));
  EXPECT_EQ(header.count, 10U);
  EXPECT_NE(header.flags & WeReadBrowse::kPageResponseTruncated, 0U);
  EXPECT_NE(header.flags & WeReadBrowse::kPageHasMore, 0U);
}

}  // namespace
