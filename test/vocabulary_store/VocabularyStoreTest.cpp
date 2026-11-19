#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalMemory.h>
#include <HalStorage.h>
#include <gtest/gtest.h>
#include <vocabulary/VocabularyStore.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#if defined(__unix__)
#include <sys/resource.h>

#include <csignal>
#endif

ESPMock ESP;
HWCDC Serial;

class VocabularyTest : public ::testing::Test {
 protected:
  std::filesystem::path root;
  VocabularyContext context;
  void SetUp() override {
    std::string pattern = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/vocabulary.XXXXXX";
    const char* path = mkdtemp(pattern.data());
    ASSERT_NE(path, nullptr);
    root = path;
    ASSERT_EQ(setenv("CROSSPOINT_SIM_SD", root.c_str(), 1), 0);
    fake::vocabularyFreeHeap = fake::vocabularyLargestBlock = 1000000;
    context.title = "A \"book\"";
    context.author = "author";
    context.chapter = "chapter";
  }
  void TearDown() override {
    unsetenv("CROSSPOINT_SIM_SD");
    std::filesystem::remove_all(root);
  }
  std::filesystem::path path() const { return root / "Vocabulary.txt"; }
  void write(const std::string& text) { std::ofstream(path(), std::ios::binary) << text; }
  std::string read() const {
    std::ifstream input(path(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
  }
  bool append(const std::string& word = "word", const std::string& definition = "definition", bool html = false) {
    return VocabularyStore::append(context, word, "headword", definition, html);
  }
};

TEST_F(VocabularyTest, EscapesAndTruncatesAtUtf8Boundaries) {
  ASSERT_TRUE(append(std::string(159, 'a') + "é", std::string(2047, 'b') + "é"));
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, read()));
  EXPECT_EQ(doc["v"].as<int>(), 1);
  EXPECT_EQ(doc["id"].as<std::string>().size(), 32u);
  EXPECT_EQ(doc["word"].as<std::string>(), std::string(159, 'a'));
  EXPECT_EQ(doc["definition"].as<std::string>(), std::string(2047, 'b'));
  EXPECT_TRUE(doc["definition_truncated"].as<bool>());
  EXPECT_EQ(doc["title"].as<std::string>(), context.title);
  const auto text = read();
  EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 1);
}

TEST_F(VocabularyTest, NeverChangesUnknownExistingFiles) {
  for (const std::string text :
       {"my book without newline", "my notes\n", "{\"v\":2,\"id\":\"other\"}\n", "{\"v\":1,\"id\":\"other\"}\n"}) {
    write(text);
    EXPECT_FALSE(append());
    EXPECT_EQ(read(), text);
  }
}

TEST_F(VocabularyTest, PreservesCompleteUnterminatedRecordAndRepairsOwnedPartialTail) {
  ASSERT_TRUE(append());
  const std::string first = read();
  write(first.substr(0, first.size() - 1));
  ASSERT_TRUE(append("second"));
  const auto complete = read();
  EXPECT_EQ(complete.substr(0, first.size()), first);
  EXPECT_EQ(std::count(complete.begin(), complete.end(), '\n'), 2);
  write(first + first.substr(0, 80));
  ASSERT_TRUE(append("third"));
  const auto repaired = read();
  EXPECT_EQ(repaired.substr(0, first.size()), first);
  EXPECT_EQ(std::count(repaired.begin(), repaired.end(), '\n'), 2);
  EXPECT_EQ(repaired.size(), first.size() + first.size() + 1);
}

TEST_F(VocabularyTest, RefusesUnknownOrOversizeTailsWithoutTruncation) {
  ASSERT_TRUE(append());
  const auto first = read();
  for (const auto& tail :
       {std::string("notes"), std::string("notes\n"), std::string("{\"v\":"), std::string(33 * 1024, 'x')}) {
    const auto text = first + tail;
    write(text);
    EXPECT_FALSE(append());
    EXPECT_EQ(read(), text);
  }
}

TEST_F(VocabularyTest, HtmlExportChecksTotalAndContiguousHeadroom) {
  const std::string html = "<b>" + std::string(2400, 'a') + "</b>";
  fake::vocabularyFreeHeap = 12000;
  fake::vocabularyLargestBlock = 1000000;
  EXPECT_FALSE(append("word", html, true));
  EXPECT_FALSE(std::filesystem::exists(path()));
  fake::vocabularyFreeHeap = 1000000;
  fake::vocabularyLargestBlock = 3000;
  EXPECT_FALSE(append("word", html, true));
  fake::vocabularyLargestBlock = 1000000;
  ASSERT_TRUE(append("word", html, true));
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, read()));
  EXPECT_TRUE(doc["definition_truncated"].as<bool>());
  EXPECT_EQ(doc["definition"].as<std::string>().find('<'), std::string::npos);
}

TEST_F(VocabularyTest, RefusesAnUnavailableStorageRoot) {
  std::ofstream(root / "not-directory") << "existing file";
  ASSERT_EQ(setenv("CROSSPOINT_SIM_SD", (root / "not-directory").c_str(), 1), 0);
  EXPECT_FALSE(append());
}

#if defined(__unix__)
TEST_F(VocabularyTest, ShortWriteRestoresThePreviousFile) {
  ASSERT_TRUE(append());
  const auto original = read();
  struct rlimit saved;
  ASSERT_EQ(getrlimit(RLIMIT_FSIZE, &saved), 0);
  const auto previousHandler = std::signal(SIGXFSZ, SIG_IGN);
  ASSERT_NE(previousHandler, SIG_ERR);
  struct RestoreLimit {
    struct rlimit limit;
    decltype(previousHandler) handler;
    ~RestoreLimit() {
      setrlimit(RLIMIT_FSIZE, &limit);
      std::signal(SIGXFSZ, handler);
    }
  } restore{saved, previousHandler};
  auto limit = saved;
  limit.rlim_cur = original.size() + 16;
  ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &limit), 0);
  EXPECT_FALSE(append("second", std::string(2000, 'x')));
  EXPECT_EQ(read(), original);
}
#endif
