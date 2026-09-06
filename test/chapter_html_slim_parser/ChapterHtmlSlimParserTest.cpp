#include <Arduino.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>

#define class struct
#define private public
#include "Epub/Section.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class

extern bool failPageSerialization;
extern std::vector<std::string> collectedFootnotes;
extern std::vector<std::string> laidOutWords;
extern bool invalidateNextTextBlock;

namespace {
size_t failObjectSize = 0;
bool failNextArray = false;
}  // namespace

void* operator new[](const size_t size, const std::nothrow_t&) noexcept {
  if (std::exchange(failNextArray, false)) return nullptr;
  try {
    return ::operator new[](size);
  } catch (...) {
    return nullptr;
  }
}

void* operator new(const size_t size, const std::nothrow_t&) noexcept {
  if (size == failObjectSize) {
    failObjectSize = 0;
    failNextArray = false;
    invalidateNextTextBlock = false;
    return nullptr;
  }
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}

namespace {

class ChapterHtmlSlimParserTest : public ::testing::TestWithParam<const char*> {
 protected:
  std::string filepath = "unused.xhtml";
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  ChapterHtmlSlimParser parser{nullptr,
                               filepath,
                               renderer,
                               0,
                               1.0f,
                               false,
                               0,
                               static_cast<uint16_t>(renderer.getScreenWidth()),
                               static_cast<uint16_t>(renderer.getScreenHeight()),
                               false,
                               false,
                               {},
                               true,
                               "",
                               "",
                               0,
                               {},
                               nullptr,
                               &cssParser,
                               true};

  void SetUp() override {
    ESP = {};
    collectedFootnotes.clear();
    laidOutWords.clear();
    parser.currentTextBlock = std::make_unique<ParsedText>(false, false, false, BlockStyle{}, true);
  }
  void TearDown() override {
    ESP = {};
    failObjectSize = 0;
    failPageSerialization = false;
    parser.abortParse();
    if (filepath != "unused.xhtml") std::filesystem::remove(filepath);
  }
  void writeHtml(const std::string& html) {
    filepath = (std::filesystem::temp_directory_path() / ("crossmux-parser-oom-" + std::to_string(getpid()) + ".xhtml"))
                   .string();
    std::ofstream(filepath) << html;
  }
};

TEST_F(ChapterHtmlSlimParserTest, NoTouchKeepsFootnotesWithoutLinkStorage) {
  parser.collectTouchLinks = false;
  parser.currentTextBlock = std::make_unique<ParsedText>(false, false, false, BlockStyle{}, false);
  const XML_Char* attributes[] = {"href", "#note-target", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "a", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "1", 1);
  ChapterHtmlSlimParser::endElement(&parser, "a");
  ASSERT_EQ(parser.pendingFootnotes.size(), 1U);
  EXPECT_STREQ(parser.pendingFootnotes[0].second.href, "#note-target");
  EXPECT_EQ(parser.currentTextBlock->wordLinkIds.capacity(), 0U);
  EXPECT_EQ(parser.currentTextBlock->linkTargets.capacity(), 0U);
  size_t lines = 0;
  EXPECT_TRUE(
      parser.currentTextBlock->layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> line, uint32_t) {
        EXPECT_EQ(line->takeLinkSpans().capacity(), 0U);
        ++lines;
        return true;
      }));
  EXPECT_EQ(lines, 1U);
}

TEST_F(ChapterHtmlSlimParserTest, StyledHeadroomFailureStopsCallbacksAndFinish) {
  writeHtml("<html><body><p>first</p><p>second</p></body></html>");
  size_t pages = 0;
  parser.completePageFn = [&](auto, auto, auto, auto) { ++pages; };
  ASSERT_TRUE(parser.beginParse());
  ESP.maxAlloc = 16 * 1024 - 1;
  EXPECT_EQ(parser.parseStep(), ChapterHtmlSlimParser::ParseStatus::OutOfMemory);
  ChapterHtmlSlimParser::characterData(&parser, "ignored", 7);
  EXPECT_TRUE(parser.currentTextBlock->isEmpty());
  EXPECT_FALSE(parser.finishParse());
  EXPECT_EQ(pages, 0U);
}

TEST_F(ChapterHtmlSlimParserTest, BasicLayoutDoesNotUseStyledHeadroomGate) {
  parser.embeddedStyle = false;
  ESP.freeHeap = 48 * 1024 - 1;
  ESP.maxAlloc = 16 * 1024 - 1;
  EXPECT_TRUE(parser.checkMemory());
  EXPECT_FALSE(parser.allocationFailed());
}

TEST_F(ChapterHtmlSlimParserTest, HeadroomCrossedInsideCallbackAbortsExpat) {
  writeHtml("<html><body><p>ignored</p></body></html>");
  parser.completePageFn = [](auto, auto, auto, auto) { FAIL() << "Page emitted after OOM"; };
  ASSERT_TRUE(parser.beginParse());
  ESP.callsUntilLow = 2;
  EXPECT_EQ(parser.parseStep(), ChapterHtmlSlimParser::ParseStatus::OutOfMemory);
  EXPECT_EQ(XML_GetErrorCode(parser.xmlParser_), XML_ERROR_ABORTED);
  EXPECT_EQ(parser.visibleTextOffset, 0U);
  EXPECT_FALSE(parser.finishParse());
}

TEST_F(ChapterHtmlSlimParserTest, InvalidTextArenaFailsLayoutInsteadOfDroppingLine) {
  parser.currentTextBlock->addWord("first", EpdFontFamily::REGULAR);
  size_t lines = 0;
  invalidateNextTextBlock = true;
  EXPECT_FALSE(parser.currentTextBlock->layoutAndExtractLines(renderer, 0, 400, [&](auto, auto) {
    ++lines;
    return true;
  }));
  EXPECT_FALSE(invalidateNextTextBlock);
  EXPECT_EQ(lines, 0U);
}

TEST_F(ChapterHtmlSlimParserTest, TextBlockFailureInsideExpatStopsBeforeNextParagraph) {
  writeHtml("<html><body><p>first</p><p>second</p></body></html>");
  size_t pages = 0;
  parser.completePageFn = [&](auto, auto, auto, auto) { ++pages; };
  ASSERT_TRUE(parser.beginParse());
  failObjectSize = sizeof(TextBlock);
  EXPECT_EQ(parser.parseStep(), ChapterHtmlSlimParser::ParseStatus::OutOfMemory);
  EXPECT_EQ(XML_GetErrorCode(parser.xmlParser_), XML_ERROR_ABORTED);
  EXPECT_LT(parser.visibleTextOffset, 11U);
  EXPECT_FALSE(parser.finishParse());
  EXPECT_EQ(pages, 0U);
}

TEST_F(ChapterHtmlSlimParserTest, PageFailureStopsTableAndTrailingText) {
  writeHtml("<html><body><table><tr><td>first</td><td>second</td></tr></table><p>tail</p></body></html>");
  size_t pages = 0;
  parser.completePageFn = [&](auto, auto, auto, auto) { ++pages; };
  ASSERT_TRUE(parser.beginParse());
  failObjectSize = sizeof(Page);
  EXPECT_EQ(parser.parseStep(), ChapterHtmlSlimParser::ParseStatus::OutOfMemory);
  EXPECT_FALSE(parser.finishParse());
  EXPECT_EQ(pages, 0U);
}

TEST_F(ChapterHtmlSlimParserTest, LineWorkspaceFailureEmitsNothing) {
  parser.currentTextBlock->addWord("first", EpdFontFamily::REGULAR);
  parser.currentTextBlock->addWord("second", EpdFontFamily::REGULAR);
  size_t lines = 0;
  failNextArray = true;
  EXPECT_FALSE(parser.currentTextBlock->layoutAndExtractLines(renderer, 0, 400, [&](auto, auto) {
    ++lines;
    return true;
  }));
  EXPECT_FALSE(failNextArray);
  EXPECT_EQ(lines, 0U);
}

TEST_F(ChapterHtmlSlimParserTest, TableLayoutFailureStopsBeforeTrailingText) {
  writeHtml("<html><body><table><tr><td>first</td><td>second</td></tr></table><p>tail</p></body></html>");
  size_t pages = 0;
  parser.completePageFn = [&](auto, auto, auto, auto) { ++pages; };
  ASSERT_TRUE(parser.beginParse());
  failObjectSize = sizeof(TextBlock);
  EXPECT_EQ(parser.parseStep(), ChapterHtmlSlimParser::ParseStatus::OutOfMemory);
  EXPECT_EQ(XML_GetErrorCode(parser.xmlParser_), XML_ERROR_ABORTED);
  EXPECT_LT(parser.visibleTextOffset, 15U);
  EXPECT_FALSE(parser.finishParse());
  EXPECT_EQ(pages, 0U);
}

TEST_F(ChapterHtmlSlimParserTest, SoftFlushFailureStopsLaterCallbacks) {
  size_t pages = 0;
  parser.completePageFn = [&](auto, auto, auto, auto) { ++pages; };
  std::string text;
  for (int i = 0; i < 330; ++i) text += "word ";
  failObjectSize = sizeof(PageLine);
  ChapterHtmlSlimParser::characterData(&parser, text.data(), text.size());
  EXPECT_TRUE(parser.allocationFailed());
  const auto offset = parser.visibleTextOffset;
  ChapterHtmlSlimParser::characterData(&parser, "tail", 4);
  EXPECT_EQ(parser.visibleTextOffset, offset);
  EXPECT_FALSE(parser.finishParse());
  EXPECT_EQ(pages, 0U);
}

TEST_F(ChapterHtmlSlimParserTest, BasicLongCallbackFlushesEarlyWithoutLosingTextOrRuby) {
  parser.embeddedStyle = false;
  parser.insideBody = true;
  parser.completePageFn = [](auto, auto, auto, auto) {};
  parser.inRuby = true;
  std::string text;
  for (int i = 0; i < 1200; ++i) text += "阅读";
  ChapterHtmlSlimParser::characterData(&parser, text.data(), text.size());
  EXPECT_TRUE(laidOutWords.empty());  // A ruby group cannot be split by soft flushing.
  parser.inRuby = false;
  ChapterHtmlSlimParser::characterData(&parser, " ", 1);
  ASSERT_FALSE(parser.hasFailed());
  EXPECT_LE(parser.currentTextBlock->size(), 320U);
  EXPECT_FALSE(parser.currentTextBlock->firstLinePending);
  ASSERT_TRUE(parser.finishParse());
  std::string actual;
  for (const auto& word : laidOutWords) actual += word;
  EXPECT_EQ(actual, text);
}

TEST_F(ChapterHtmlSlimParserTest, BasicLongCallbackKeepsWindowAndFootnoteOffsets) {
  parser.embeddedStyle = false;
  parser.insideBody = true;
  size_t pages = 0;
  uint32_t lastOffset = 0;
  parser.completePageFn = [&](auto, auto, auto, uint32_t offset) {
    EXPECT_GE(offset, lastOffset);
    lastOffset = offset;
    ++pages;
  };
  std::string text;
  for (int i = 0; i < 1800; ++i) text += "word ";
  ChapterHtmlSlimParser::characterData(&parser, text.data(), text.size());
  ASSERT_FALSE(parser.hasFailed());
  EXPECT_LE(parser.currentTextBlock->size(), 320U);
  EXPECT_GT(pages, 0U);  // output happens inside this single callback
  const XML_Char* attrs[] = {"href", "#note", nullptr};
  ChapterHtmlSlimParser::startElement(&parser, "a", attrs);
  ChapterHtmlSlimParser::characterData(&parser, "1", 1);
  ChapterHtmlSlimParser::endElement(&parser, "a");
  ASSERT_TRUE(parser.finishParse());
  ASSERT_EQ(laidOutWords.size(), 1801U);
  EXPECT_EQ(laidOutWords.back(), "1");
  for (size_t i = 0; i < 1800; ++i) EXPECT_EQ(laidOutWords[i], "word");
  ASSERT_EQ(collectedFootnotes.size(), 1U);
  EXPECT_EQ(collectedFootnotes[0], "#note");
}

TEST_F(ChapterHtmlSlimParserTest, FinishFailureDoesNotEmitTrailingPage) {
  writeHtml("<html><body>tail</body></html>");
  size_t pages = 0;
  parser.completePageFn = [&](auto, auto, auto, auto) { ++pages; };
  ASSERT_TRUE(parser.beginParse());
  ASSERT_EQ(parser.parseStep(), ChapterHtmlSlimParser::ParseStatus::Done);
  failObjectSize = sizeof(TextBlock);
  EXPECT_FALSE(parser.finishParse());
  EXPECT_TRUE(parser.allocationFailed());
  EXPECT_EQ(pages, 0U);
}

TEST_F(ChapterHtmlSlimParserTest, MalformedXmlAndMissingFileAreNotOutOfMemory) {
  EXPECT_FALSE(parser.beginParse());
  EXPECT_TRUE(parser.ioFailed());
  EXPECT_FALSE(parser.allocationFailed());
  writeHtml("<html><body><p>first</body></html>");
  parser.completePageFn = [](auto, auto, auto, auto) {};
  ASSERT_TRUE(parser.beginParse());
  EXPECT_EQ(parser.parseStep(), ChapterHtmlSlimParser::ParseStatus::Error);
  EXPECT_FALSE(parser.ioFailed());
  EXPECT_FALSE(parser.allocationFailed());
}

TEST_P(ChapterHtmlSlimParserTest, KeepsCssVerticalAlignAndInternalLinkMetadata) {
  const char* verticalAlign = GetParam();
  const char* expectedHref = "#note-target";
  const XML_Char* attributes[] = {"href", expectedHref, "style", verticalAlign, nullptr};

  ChapterHtmlSlimParser::startElement(&parser, "a", attributes);
  const uint8_t linkId = parser.currentFootnoteLinkId;
  ASSERT_NE(linkId, 0u);
  ChapterHtmlSlimParser::characterData(&parser, "1", 1);
  ChapterHtmlSlimParser::endElement(&parser, "a");

  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  const auto style = parser.currentTextBlock->getWordStyleAt(0);
  const auto expectedStyle =
      std::string(verticalAlign).find("super") != std::string::npos ? EpdFontFamily::SUP : EpdFontFamily::SUB;
  EXPECT_NE(static_cast<uint8_t>(style) & static_cast<uint8_t>(expectedStyle), 0u);

  ASSERT_EQ(parser.pendingFootnotes.size(), 1u);
  const FootnoteEntry& footnote = parser.pendingFootnotes.front().second;
  EXPECT_STREQ(footnote.href, expectedHref);
  ASSERT_EQ(parser.currentTextBlock->wordLinkIds.size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->wordLinkIds.front(), linkId);
  EXPECT_TRUE(parser.currentTextBlock->linkTargetMatches(linkId, expectedHref));
}

INSTANTIATE_TEST_SUITE_P(CssVerticalAlign, ChapterHtmlSlimParserTest,
                         ::testing::Values("vertical-align: super", "vertical-align: sub"));

class SectionMemoryTest : public ::testing::Test {
 protected:
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  GfxRenderer renderer;
  ReaderRenderSpec spec{};

  void SetUp() override {
    ESP = {};
    // CTest launches each case in a separate process when running in parallel.
    epub->cachePath =
        (std::filesystem::temp_directory_path() / ("crossmux-section-memory-test-" + std::to_string(getpid())))
            .string();
    std::filesystem::remove_all(epub->cachePath);
    std::filesystem::create_directories(epub->cachePath + "/html");
    spec.viewportWidth = 160;
    spec.viewportHeight = 64;
    spec.lineCompression = 1;
    spec.embeddedStyle = true;
    spec.collectTouchLinks = false;
  }
  void TearDown() override {
    failObjectSize = 0;
    failNextArray = false;
    failPageSerialization = false;
    ESP = {};
    std::filesystem::remove_all(epub->cachePath);
  }
  void writeHtml(const std::string& html) { std::ofstream(epub->cachePath + "/html/0.html") << html; }
  void expectNoCache() {
    EXPECT_FALSE(std::filesystem::exists(epub->cachePath + "/sections/0.bin"));
    EXPECT_FALSE(std::filesystem::exists(epub->cachePath + "/sections/0.bin.part"));
  }
};

TEST_F(SectionMemoryTest, OomAbandonsBuildAndBasicRetryPreservesBodyAndOffsets) {
  std::string html = "<html><body><p id='start'>";
  for (int i = 0; i < 400; ++i) html += "word" + std::to_string(i) + " ";
  html += "</p></body></html>";
  writeHtml(html);
  uint32_t offset = 0;
  {
    Section section(epub, 0, renderer);
    ASSERT_TRUE(section.startBuild(spec));
    ASSERT_TRUE(section.buildSomeMore(1));
    ASSERT_TRUE(section.isBuilding());
    ASSERT_GT(section.pageCount, 1U);
    offset = *section.getVisibleTextOffsetForPage(1);
    ESP.freeHeap = 48 * 1024 - 1;
    EXPECT_FALSE(section.buildSomeMore(1));
    EXPECT_EQ(section.buildError(), Section::BuildError::OutOfMemory);
    EXPECT_FALSE(section.isBuilding());
  }
  expectNoCache();  // Destructor must not commit a partial after failure.
  laidOutWords.clear();
  spec.embeddedStyle = false;
  Section retry(epub, 0, renderer);
  ASSERT_TRUE(retry.createSectionFile(spec));
  ASSERT_EQ(laidOutWords.size(), 400U);
  for (int i = 0; i < 400; ++i) EXPECT_EQ(laidOutWords[i], "word" + std::to_string(i));
  const auto page = retry.getPageForVisibleTextOffset(offset);
  ASSERT_TRUE(page.has_value());
  EXPECT_LE(*retry.getVisibleTextOffsetForPage(*page), offset);
  EXPECT_EQ(retry.findAnchor("start"), 0);
  Section restored(epub, 0, renderer);
  ASSERT_TRUE(restored.loadSectionFile(spec));
  EXPECT_EQ(restored.getPageForVisibleTextOffset(offset), page);
  spec.embeddedStyle = true;
  EXPECT_FALSE(restored.loadSectionFile(spec));
}

TEST_F(SectionMemoryTest, ReclaimsCachesBeforeStartAndContinuation) {
  std::string html = "<html><body>";
  for (int i = 0; i < 500; ++i) html += "<p>word</p>";
  html += "</body></html>";
  writeHtml(html);
  ESP.freeHeap = 40 * 1024;
  renderer.fontCache.reclaimedBytes = 16 * 1024;
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.startBuild(spec));
  EXPECT_EQ(renderer.fontCache.releases, 1);
  ESP.freeHeap = 32 * 1024;
  renderer.fontCache.reclaimedBytes = 24 * 1024;
  ASSERT_TRUE(section.buildSomeMore(0));
  EXPECT_EQ(renderer.fontCache.releases, 2);
  EXPECT_TRUE(section.isBuildComplete());
  EXPECT_EQ(section.buildError(), Section::BuildError::None);
}

TEST_F(SectionMemoryTest, TouchCapabilityInvalidatesCompleteAndPartialCaches) {
  std::string html = "<html><body>";
  for (int i = 0; i < 400; ++i) html += "<p>word</p>";
  html += "</body></html>";
  writeHtml(html);
  for (bool partial : {false, true}) {
    spec.collectTouchLinks = true;
    {
      Section section(epub, 0, renderer);
      ASSERT_TRUE(section.startBuild(spec));
      ASSERT_TRUE(section.buildSomeMore(partial ? 1 : 0));
    }
    Section restored(epub, 0, renderer);
    ASSERT_TRUE(restored.loadSectionFile(spec));
    EXPECT_EQ(restored.isPartial(), partial);
    spec.collectTouchLinks = false;
    EXPECT_FALSE(restored.loadSectionFile(spec));
  }
}

TEST_F(SectionMemoryTest, LayoutOomAndWriteErrorNeverCommitCache) {
  writeHtml("<html><body><p>first</p><p>second</p></body></html>");
  for (bool oom : {false, true}) {
    {
      Section section(epub, 0, renderer);
      ASSERT_TRUE(section.startBuild(spec));
      if (oom)
        failObjectSize = sizeof(TextBlock);
      else
        failPageSerialization = true;
      EXPECT_FALSE(section.buildSomeMore(0));
      EXPECT_EQ(section.buildError(), oom ? Section::BuildError::OutOfMemory : Section::BuildError::Io);
      EXPECT_FALSE(section.isBuilding());
      failPageSerialization = false;
    }
    expectNoCache();
  }
  writeHtml("<html><body><p>broken</body></html>");
  Section section(epub, 0, renderer);
  EXPECT_FALSE(section.createSectionFile(spec));
  EXPECT_EQ(section.buildError(), Section::BuildError::InvalidData);
  expectNoCache();
}

TEST_F(SectionMemoryTest, InitializationFailureReleasesResourcesAndPreservesPriorCache) {
  std::string html = "<html><body>";
  for (int i = 0; i < 400; ++i) html += "<p>word</p>";
  html += "</body></html>";
  writeHtml(html);
  for (const bool partial : {false, true}) {
    {
      Section original(epub, 0, renderer);
      ASSERT_TRUE(original.startBuild(spec));
      ASSERT_TRUE(original.buildSomeMore(partial ? 1 : 0));
    }
    for (const size_t size : {sizeof(Section::BuildContext), sizeof(ChapterHtmlSlimParser), sizeof(ParsedText)}) {
      SCOPED_TRACE(size);
      const auto cachePath = epub->cachePath + "/sections/0.bin";
      std::ifstream beforeFile(cachePath, std::ios::binary);
      const std::string before{std::istreambuf_iterator<char>(beforeFile), {}};
      {
        Section section(epub, 0, renderer);
        ASSERT_TRUE(section.loadSectionFile(spec));
        failObjectSize = size;
        EXPECT_FALSE(section.startBuild(spec));
        EXPECT_EQ(failObjectSize, 0U);
        EXPECT_EQ(section.buildError(), Section::BuildError::OutOfMemory);
        EXPECT_FALSE(section.isBuilding());
        EXPECT_FALSE(section.file.isOpen());
        EXPECT_FALSE(std::filesystem::exists(section.binTmpPath()));
      }
      std::ifstream afterFile(cachePath, std::ios::binary);
      EXPECT_EQ((std::string{std::istreambuf_iterator<char>(afterFile), {}}), before);
      Section restored(epub, 0, renderer);
      ASSERT_TRUE(restored.loadSectionFile(spec));
      EXPECT_EQ(restored.isPartial(), partial);
    }
  }
}

TEST_F(SectionMemoryTest, MixedChapterCacheMatchesVerifiedLayout) {
  std::string html = "<html><body><p id='start'>";
  for (int i = 0; i < 900; ++i) html += "中文正文";
  html += "<ruby>汉字<rt>han zi</rt></ruby><a href='#note'>[1]</a></p>";
  for (int i = 0; i < 80; ++i) html += "<p>English paragraph words.</p>";
  html += "<p id='note'>Footnote text.</p></body></html>";
  writeHtml(html);
  laidOutWords.clear();
  collectedFootnotes.clear();
  spec.embeddedStyle = false;
  Section section(epub, 0, renderer);
  ASSERT_TRUE(section.createSectionFile(spec));
  const auto path = epub->cachePath + "/sections/0.bin";
  std::ifstream file(path, std::ios::binary);
  const std::string bytes{std::istreambuf_iterator<char>(file), {}};
  uint64_t digest = 14695981039346656037ULL;
  const auto append = [&digest](const std::string& value) {
    for (const unsigned char byte : value) digest = (digest ^ byte) * 1099511628211ULL;
  };
  append(bytes);
  for (const auto& word : laidOutWords) append(word);
  for (const auto& href : collectedFootnotes) append(href);
  EXPECT_EQ(digest, 13330287791149729058ULL);  // Pre-refactor cache, text and footnotes.
}

TEST_F(SectionMemoryTest, CssCacheOomIsReportedAndBasicBuildDoesNotHydrateCss) {
  writeHtml("<html><body><p>text</p></body></html>");
  CssParser css(epub->cachePath);
  const auto cssPath = epub->cachePath + "/style.css";
  std::ofstream(cssPath) << "p { margin-top: 20px; }";
  HalFile source;
  ASSERT_TRUE(Storage.openFileForRead("TEST", cssPath, source));
  ASSERT_EQ(css.loadFromStream(source), CssParser::ParseResult::Complete);
  ASSERT_TRUE(css.saveToCache(true));
  css.clear();
  epub->css = &css;
  Section section(epub, 0, renderer);
  failNextArray = true;
  EXPECT_FALSE(section.startBuild(spec));
  EXPECT_EQ(section.buildError(), Section::BuildError::OutOfMemory);
  EXPECT_FALSE(failNextArray);
  expectNoCache();
  spec.embeddedStyle = false;
  ASSERT_TRUE(section.createSectionFile(spec));
  EXPECT_EQ(css.ruleCount(), 0U);
  spec.embeddedStyle = true;
  failNextArray = true;
  EXPECT_FALSE(section.startBuild(spec));
  EXPECT_EQ(section.buildError(), Section::BuildError::OutOfMemory);
  EXPECT_FALSE(section.isBuilding());
  EXPECT_FALSE(section.file.isOpen());
  EXPECT_FALSE(std::filesystem::exists(section.binTmpPath()));
  EXPECT_EQ(css.ruleCount(), 0U);
  spec.embeddedStyle = false;
  Section restored(epub, 0, renderer);
  EXPECT_TRUE(restored.loadSectionFile(spec));
}

}  // namespace
