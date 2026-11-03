#include <HardwareSerial.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "WeReadClient.h"
#include "WeReadXhtmlCodec.h"

#undef Serial
HardwareSerial Serial;
namespace obfuscation {
String obfuscateToBase64(const std::string& text) { return String(text.c_str()); }
std::string deobfuscateFromBase64(const char* text, bool* ok) {
  if (ok) *ok = text != nullptr;
  return text ? text : "";
}
}  // namespace obfuscation

namespace {
using WeReadClient::Error;
using WeReadXhtmlCodec::writeLiteral;
using WeReadXhtmlCodec::writeXmlText;
struct {
  time_t value = 1792497600;
  time_t nowUtc() const { return value; }
} halClock;

// Covers and image validation have their own tests; these cases exercise chapter packaging.
WeReadProtocol::ImageType findCoverSource(const std::string&, std::string&) { return WeReadProtocol::ImageType::None; }
const char* coverEntryName(WeReadProtocol::ImageType) { return "cover.png"; }
const char* imageMediaType(WeReadProtocol::ImageType) { return "image/png"; }
WeReadProtocol::ImageType imageTypeFromHref(const char*) { return WeReadProtocol::ImageType::Png; }
const char* imageTypeName(WeReadProtocol::ImageType) { return "none"; }
bool validImageWorkRecord(const WeReadStore::ImageWorkRecord&) { return true; }

#include "WeReadPackageFunctions.inc"

class WeReadEpubPackageTest : public ::testing::Test {
 protected:
  std::filesystem::path root;
  WeReadStore::BookRecord book;
  std::string dir;
  std::string toc;

  std::filesystem::path host(const std::string& path) const { return root / path.substr(1); }
  void put(const std::string& path, const std::string& text) {
    std::filesystem::create_directories(host(path).parent_path());
    std::ofstream(host(path), std::ios::binary) << text;
  }
  std::string get(const std::string& path) const {
    std::ifstream input(host(path), std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }
  void SetUp() override {
    root = std::filesystem::temp_directory_path() / "crossmux-weread-package";
    std::filesystem::remove_all(root);
    ASSERT_EQ(setenv("CROSSPOINT_SIM_SD", root.c_str(), 1), 0);
    ASSERT_TRUE(Storage.begin());
    strcpy(book.bookId, "footnote-package-test");
    strcpy(book.title, "Footnote test");
    strcpy(book.author, "Test");
    dir = WeReadStore::bookDirectory(book.bookId);
    toc = WeReadStore::tocPath(book.bookId);
    ASSERT_TRUE(Storage.ensureDirectoryExists((dir + "/chapters").c_str()));
    WeReadStore::IndexWriter writer;
    ASSERT_TRUE(writer.begin(toc, WeReadStore::kTocMagic, sizeof(WeReadStore::TocRecord)));
    for (unsigned i = 0; i < 2; ++i) {
      WeReadStore::TocRecord chapter;
      snprintf(chapter.chapterUid, sizeof(chapter.chapterUid), "%u", i + 1);
      snprintf(chapter.title, sizeof(chapter.title), "Chapter %u", i + 1);
      ASSERT_TRUE(writer.append(&chapter));
      put(WeReadStore::chapterPath(dir, i),
          "<html "
          "xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Test</title></head><body><p>Text</p></body></html>");
      put(WeReadStore::notesPath(dir, i), "");
    }
    ASSERT_TRUE(writer.finish());
    halClock.value = 1792497600;
  }
  void TearDown() override {
    unsetenv("CROSSPOINT_SIM_SD");
    std::filesystem::remove_all(root);
  }
};

TEST_F(WeReadEpubPackageTest, NotesAreAuxiliaryAndEmptySidecarsAreNotPackaged) {
  const std::string notes =
      "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>Note</title></head><body><aside id=\"wr-note-1\">Full "
      "note</aside></body></html>";
  put(WeReadStore::notesPath(dir, 0), notes);
  WeReadStore::IndexWriter images;
  ASSERT_TRUE(images.begin(dir + "/images.work", WeReadStore::kImageWorkMagic, sizeof(WeReadStore::ImageWorkRecord)));
  ASSERT_TRUE(images.finish());
  for (const auto policy : {WeReadStore::ImagePolicy::Exclude, WeReadStore::ImagePolicy::Embed}) {
    std::string nav, opf;
    ASSERT_EQ(writePackageFiles(dir, book, toc, 2, 0, 1, policy, dir + "/images.work", WeReadProtocol::ImageType::None,
                                nav, opf, nullptr, nullptr),
              Error::Ok);
    const std::string content = get(opf);
    EXPECT_NE(content.find("property=\"dcterms:modified\""), std::string::npos);
    EXPECT_NE(content.find("href=\"notes000000.xhtml\""), std::string::npos);
    EXPECT_EQ(content.find("notes000001"), std::string::npos);
    EXPECT_LT(content.find("<itemref idref=\"ch000001\""),
              content.find("<itemref idref=\"notes000000\" linear=\"no\""));
    EXPECT_EQ(get(nav).find("notes000000"), std::string::npos);
    std::array<uint8_t, 4096> buffer{};
    ASSERT_EQ(packageBook(book, dir, toc, 2, 0, 1, policy, dir + "/images.work", buffer.data(), buffer.size(),
                          "/sample.epub", nullptr, nullptr),
              Error::Ok);
    const std::string zip = get("/sample.epub");
    EXPECT_NE(zip.find("OEBPS/notes000000.xhtml"), std::string::npos);
    EXPECT_NE(zip.find(notes), std::string::npos);
    EXPECT_EQ(zip.find("OEBPS/notes000001.xhtml"), std::string::npos);
  }
}

TEST_F(WeReadEpubPackageTest, MissingSidecarRejectsIncompleteChapterWithoutReplacingBook) {
  std::filesystem::remove(host(WeReadStore::notesPath(dir, 1)));
  put("/old.epub", "old complete book");
  std::array<uint8_t, 512> buffer{};
  EXPECT_EQ(packageBook(book, dir, toc, 2, 0, 1, WeReadStore::ImagePolicy::Exclude, "", buffer.data(), buffer.size(),
                        "/new.epub.part", nullptr, nullptr),
            Error::Integrity);
  EXPECT_EQ(get("/old.epub"), "old complete book");
}

TEST_F(WeReadEpubPackageTest, PackageRequiresUtcModificationTime) {
  halClock.value = 0;
  std::string nav, opf;
  EXPECT_EQ(writePackageFiles(dir, book, toc, 2, 0, 1, WeReadStore::ImagePolicy::Exclude, "",
                              WeReadProtocol::ImageType::None, nav, opf, nullptr, nullptr),
            Error::Clock);
}
}  // namespace
