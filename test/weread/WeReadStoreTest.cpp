#include <ObfuscationUtils.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    constexpr uint32_t kLegacyShelfMagic = 0x33535257;  // WRS3
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
  EXPECT_EQ(tocRecord.chapterIdx, 524U);

  std::ofstream corrupt(hostPath(WeReadStore::kShelfPath), std::ios::binary | std::ios::app);
  ASSERT_TRUE(corrupt.good());
  corrupt.put('\0');
  corrupt.close();
  HalFile rejected;
  EXPECT_FALSE(WeReadStore::openShelf(rejected, count));
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

TEST_F(WeReadStoreTest, ClearsSessionAndShelfButPreservesDownloadedContent) {
  ASSERT_TRUE(WeReadStore::ensureRoot());
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

TEST_F(WeReadStoreTest, WritesValidStoreOnlyEpubInReadingOrder) {
  ASSERT_TRUE(Storage.ensureDirectoryExists("/work"));
  static constexpr char kMimetype[] = "application/epub+zip";
  static constexpr char kContainer[] =
      "<?xml version=\"1.0\"?><container><rootfiles><rootfile full-path=\"OEBPS/content.opf\"/>"
      "</rootfiles></container>";
  static constexpr char kOpf[] =
      "<package><manifest><item id=\"nav\" href=\"nav.xhtml\"/><item id=\"ch000000\" "
      "href=\"ch000000.xhtml\"/><item id=\"ch000001\" href=\"ch000001.xhtml\"/></manifest>"
      "<spine><itemref idref=\"ch000000\"/><itemref idref=\"ch000001\"/></spine></package>";
  static constexpr char kNav[] =
      "<html><nav><ol><li><a href=\"ch000000.xhtml\">一</a></li><li><a "
      "href=\"ch000001.xhtml\">二</a></li></ol></nav></html>";
  ASSERT_TRUE(Storage.writeFile("/work/ch0.xhtml", "<html><body><p>一</p></body></html>"));
  ASSERT_TRUE(Storage.writeFile("/work/ch1.xhtml", "<html><body><p>二</p></body></html>"));

  WeReadStore::StoreOnlyZipWriter zip;
  ASSERT_TRUE(zip.begin("/work/book.epub", "/work/central.part"));
  ASSERT_TRUE(zip.addBuffer("mimetype", reinterpret_cast<const uint8_t*>(kMimetype), strlen(kMimetype)));
  ASSERT_TRUE(
      zip.addBuffer("META-INF/container.xml", reinterpret_cast<const uint8_t*>(kContainer), strlen(kContainer)));
  ASSERT_TRUE(zip.addBuffer("OEBPS/content.opf", reinterpret_cast<const uint8_t*>(kOpf), strlen(kOpf)));
  ASSERT_TRUE(zip.addBuffer("OEBPS/nav.xhtml", reinterpret_cast<const uint8_t*>(kNav), strlen(kNav)));
  ASSERT_TRUE(zip.addFile("OEBPS/ch000000.xhtml", "/work/ch0.xhtml"));
  ASSERT_TRUE(zip.addFile("OEBPS/ch000001.xhtml", "/work/ch1.xhtml"));
  ASSERT_TRUE(zip.finish());
  ASSERT_TRUE(WeReadStore::looksLikeZip("/work/book.epub"));
  EXPECT_FALSE(Storage.exists("/work/central.part"));

  std::ifstream input(hostPath("/work/book.epub"), std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  ASSERT_GE(bytes.size(), 22U);
  const size_t eocd = bytes.size() - 22;
  ASSERT_EQ(readLe32(bytes, eocd), 0x06054B50U);
  ASSERT_EQ(readLe16(bytes, eocd + 10), 6U);
  size_t cursor = readLe32(bytes, eocd + 16);
  const std::vector<std::string> expected = {"mimetype",        "META-INF/container.xml", "OEBPS/content.opf",
                                             "OEBPS/nav.xhtml", "OEBPS/ch000000.xhtml",   "OEBPS/ch000001.xhtml"};
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
  ASSERT_TRUE(incomplete.begin("/work/incomplete.epub", "/work/incomplete.central"));
  ASSERT_TRUE(incomplete.addBuffer("mimetype", reinterpret_cast<const uint8_t*>(kMimetype), strlen(kMimetype)));
  ASSERT_TRUE(incomplete.addBuffer("one", reinterpret_cast<const uint8_t*>("1"), 1));
  ASSERT_TRUE(incomplete.addBuffer("two", reinterpret_cast<const uint8_t*>("2"), 1));
  ASSERT_TRUE(incomplete.finish());
  EXPECT_FALSE(WeReadStore::looksLikeZip("/work/incomplete.epub"));
}

}  // namespace
