#include <ObfuscationUtils.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "WeReadStore.h"

std::string g_simulator_sd_root;

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
  void SetUp() override {
    static std::atomic<unsigned> serial{0};
    root_ = std::filesystem::temp_directory_path() / ("crossmux-weread-store-" + std::to_string(serial.fetch_add(1)));
    std::error_code error;
    std::filesystem::remove_all(root_, error);
    ASSERT_TRUE(std::filesystem::create_directories(root_, error));
    ASSERT_FALSE(error);
    g_simulator_sd_root = root_.string();
    ASSERT_TRUE(Storage.begin());
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  std::filesystem::path hostPath(const char* sdPath) const {
    while (*sdPath == '/') ++sdPath;
    return root_ / sdPath;
  }

  std::filesystem::path root_;
};

TEST_F(WeReadStoreTest, StreamsLargeShelfAndTocIndexesAndRejectsCorruption) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  {
    constexpr uint32_t kLegacyShelfMagic = 0x34535257;  // WRS4
    WeReadStore::IndexWriter legacyShelf;
    ASSERT_TRUE(legacyShelf.begin(WeReadStore::kShelfPath, kLegacyShelfMagic, sizeof(WeReadStore::ShelfRecord)));
    WeReadStore::ShelfRecord record;
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

TEST_F(WeReadStoreTest, MapsGeneratedChapterPagesWithoutWholeBookApproximation) {
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
  ASSERT_TRUE(WeReadStore::mapPageToChapter(tocPath, 0, 1, 3, chapter, offset, fraction));
  EXPECT_STREQ(chapter.chapterUid, "chapter-0");
  EXPECT_EQ(offset, 50U);
  EXPECT_FLOAT_EQ(fraction, 0.125f);

  ASSERT_TRUE(WeReadStore::mapPageToChapter(tocPath, 2, 1, 3, chapter, offset, fraction));
  EXPECT_STREQ(chapter.chapterUid, "chapter-2");
  EXPECT_EQ(offset, 150U);
  EXPECT_FLOAT_EQ(fraction, 0.625f);

  ASSERT_TRUE(WeReadStore::mapPageToChapter(tocPath, 2, 0, 3, chapter, offset, fraction));
  EXPECT_EQ(offset, 0U);
  EXPECT_FLOAT_EQ(fraction, 0.25f);
  ASSERT_TRUE(WeReadStore::mapPageToChapter(tocPath, 2, 2, 3, chapter, offset, fraction));
  EXPECT_EQ(offset, 300U);
  EXPECT_FLOAT_EQ(fraction, 1.0f);

  EXPECT_FALSE(WeReadStore::mapPageToChapter(tocPath, 1, 0, 1, chapter, offset, fraction));
  EXPECT_FALSE(WeReadStore::mapPageToChapter(tocPath, 3, 0, 1, chapter, offset, fraction));
  EXPECT_FALSE(WeReadStore::mapPageToChapter(tocPath, 0, 3, 3, chapter, offset, fraction));

  uint32_t tocIndex = 0;
  float chapterFraction = 0.0f;
  ASSERT_TRUE(WeReadStore::mapChapterToPosition(tocPath, "chapter-2", 100, tocIndex, chapterFraction, fraction));
  EXPECT_EQ(tocIndex, 2U);
  EXPECT_NEAR(chapterFraction, 1.0f / 3.0f, 0.000001f);
  EXPECT_FLOAT_EQ(fraction, 0.5f);
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

TEST_F(WeReadStoreTest, SortsLargeShelfByRecentReadingWithStableTiesAndUnreadLast) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  WeReadStore::IndexWriter shelf;
  ASSERT_TRUE(shelf.begin(WeReadStore::kShelfPath, WeReadStore::kShelfMagic, sizeof(WeReadStore::ShelfRecord)));
  for (unsigned i = 0; i < 600; ++i) {
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
  ASSERT_EQ(count, 600U);
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

TEST_F(WeReadStoreTest, RejectsCorruptShelfSortWithoutLeavingPartialReplacement) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
  ASSERT_TRUE(Storage.writeFile(WeReadStore::kShelfPath, "corrupt"));
  EXPECT_EQ(WeReadStore::sortShelfByRecent(), WeReadStore::ShelfSortResult::StorageError);
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

TEST_F(WeReadStoreTest, WritesEmptyAndPopulatedImageIndexesAndRejectsCorruption) {
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
  ASSERT_TRUE(Storage.writeFile(
      "/work/ch0.xhtml", "<html><body><p>一</p><img src=\"images/ch000000-0.png\" alt=\"插图\"/></body></html>"));
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
  ASSERT_TRUE(zip.addFile("OEBPS/ch000000.xhtml", "/work/ch0.xhtml"));
  ASSERT_TRUE(zip.addFile("OEBPS/ch000001.xhtml", "/work/ch1.xhtml"));
  ASSERT_TRUE(zip.addFile("OEBPS/images/ch000000-0.png", "/work/image.png"));
  ASSERT_TRUE(zip.finish());
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

}  // namespace
