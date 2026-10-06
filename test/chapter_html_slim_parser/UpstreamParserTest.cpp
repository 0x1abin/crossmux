#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/activities/settings/TextSettingsPreview.h"
#include "src/util/ParagraphIndentMigration.h"

#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class

namespace {

class ChapterHtmlSlimParserTest : public ::testing::TestWithParam<const char*> {
 protected:
  std::string filepath =
      (std::filesystem::temp_directory_path() / ("upstream-parser-" + std::to_string(getpid()) + ".xhtml")).string();
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  ChapterHtmlSlimParser parser{nullptr,
                               filepath,
                               renderer,
                               0,
                               1.0f,
                               false,
                               FirstLineIndent::Auto,
                               0,
                               false,  // verticalBottomAlign
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
    std::ofstream(filepath) << "<html/>";
    parser.currentTextBlock =
        std::make_unique<ParsedText>(false, FirstLineIndent::Auto, false, false, BlockStyle{}, true);
  }
  void TearDown() override { std::filesystem::remove(filepath); }
};

TEST_F(ChapterHtmlSlimParserTest, RubySurvivesPartialParagraphExtraction) {
  ParsedText text(false, FirstLineIndent::Auto);
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  text.addWord("c", EpdFontFamily::REGULAR);
  text.setRubyForWordAt(2, "c");
  size_t lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 20,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        EXPECT_TRUE(line->getRubyTexts().empty());
        return true;
      },
      false);
  EXPECT_EQ(lines, 1u);
  const size_t retainedWords = text.size();
  ASSERT_GT(retainedWords, 0u);
  ASSERT_LT(retainedWords, 3u);
  text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    EXPECT_EQ(line->getRubyTexts().size(), retainedWords);
    EXPECT_EQ(line->getRubyTexts().back(), "c");
    for (size_t i = 0; i + 1 < retainedWords; ++i) EXPECT_TRUE(line->getRubyTexts()[i].empty());
    return true;
  });
  EXPECT_EQ(lines, 2u);
}

TEST_F(ChapterHtmlSlimParserTest, UnequalTableCellsAndRubySurvivePageBreaks) {
  parser.viewportWidth = 240;
  parser.viewportHeight = 32;
  parser.tableRowCells.reserve(2);
  std::multiset<std::string> expected;
  for (int column = 0; column < 2; ++column) {
    auto cell = std::make_unique<ParsedText>(false, FirstLineIndent::Auto);
    for (int index = 0; index < (column == 0 ? 30 : 3); ++index) {
      const auto word = std::string(column == 0 ? "left" : "right") + std::to_string(index);
      expected.insert(word);
      cell->addWord(word, EpdFontFamily::REGULAR);
    }
    if (column == 0) cell->setRubyGroupAt(0, 2, "reading");
    parser.tableRowCells.push_back(std::move(cell));
  }
  std::multiset<std::string> actual;
  unsigned pages = 0;
  unsigned rubyLines = 0;
  auto inspect = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ++pages;
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      const auto& block = *line.getBlock().get();
      ASSERT_TRUE(block.valid());
      EXPECT_LE(element->yPos + 16 + block.getRubyShift(12), parser.viewportHeight);
      rubyLines += block.hasRuby();
      for (uint16_t word = 0; word < block.wordCount(); ++word) actual.insert(block.wordText(word));
    }
  };
  parser.completePageFn = inspect;
  parser.finishTableRow();
  ASSERT_NE(parser.currentPage, nullptr);
  inspect(std::move(parser.currentPage), 0, 0, 0);
  EXPECT_GT(pages, 2u);
  EXPECT_EQ(rubyLines, 1u);
  EXPECT_EQ(actual, expected);
  for (const auto& lines : parser.tableCellLines) EXPECT_TRUE(lines.empty());
}

TEST_F(ChapterHtmlSlimParserTest, PageImageDeserializeRejectsMissingImageBlock) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-missing-image-cache.bin";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const int16_t coordinates[] = {0, 0};
    output.write(coordinates, sizeof(coordinates));
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  EXPECT_EQ(PageImage::deserialize(input), nullptr);
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

TEST_F(ChapterHtmlSlimParserTest, ParagraphWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  ASSERT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::startElement(&parser, "p", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, HeaderWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  ASSERT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::startElement(&parser, "h1", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, SpanWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  ASSERT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Before ", 7);
  ChapterHtmlSlimParser::startElement(&parser, "span", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);
  ChapterHtmlSlimParser::endElement(&parser, "span");
  ChapterHtmlSlimParser::characterData(&parser, " After ", 7);

  ASSERT_EQ(parser.currentTextBlock->size(), 2);
  ASSERT_EQ(parser.currentTextBlock->wordAt(0), "Before");
  ASSERT_EQ(parser.currentTextBlock->wordAt(1), "After");
}

TEST_F(ChapterHtmlSlimParserTest, DivWithHiddenAttributeContentShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  ASSERT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::startElement(&parser, "div", attributes);
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

}  // namespace

TEST(TextSpacingLayout, TrackingSeparatesCjkTokensAndScalesWordSpaces) {
  GfxRenderer renderer;
  for (bool hyphenation : {false, true}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, FirstLineIndent::Auto, hyphenation, false, style);
    text.addWord("一二三", EpdFontFamily::REGULAR);
    text.addWord("四五", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(
        renderer, 0, 200,
        [&](std::unique_ptr<TextBlock> line, auto) {
          ++lines;
          EXPECT_EQ(line->wordCount(), 5);
          EXPECT_EQ(line->wordXpos(0), 0);
          EXPECT_EQ(line->wordXpos(1), 7);  // 8 px glyph, -1 px tracking
          EXPECT_EQ(line->wordXpos(2), 14);
          EXPECT_EQ(line->wordXpos(3), 28);  // 8 px glyph plus 150% of a 4 px space, no tracking
          EXPECT_EQ(line->wordXpos(4), 35);
          return true;
        },
        true, -1, 150);
    EXPECT_EQ(lines, 1u);
  }
  EXPECT_EQ(renderer.getTextAdvanceX(0, "ab", EpdFontFamily::REGULAR), 16);
  EXPECT_EQ(renderer.getSpaceWidth(0, EpdFontFamily::REGULAR), 4);
}

TEST(TextSpacingLayout, WordSpacingChangesWrapThreshold) {
  GfxRenderer renderer;
  for (uint8_t percent : {50, 100, 125, 200}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, FirstLineIndent::Auto, false, false, style);
    text.addWord("ab", EpdFontFamily::REGULAR);
    text.addWord("cd", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(
        renderer, 0, 36,
        [&](std::unique_ptr<TextBlock>, auto) {
          ++lines;
          return true;
        },
        true, 0, percent);
    EXPECT_EQ(lines, percent > 100 ? 2u : 1u);  // 16 + 16 + scaled 4 px space
  }
}

TEST(TextSpacingLayout, CachedPageRestoresSpacing) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textIndentDefined = true;
  ParsedText text(false, FirstLineIndent::Auto, false, false, style);
  text.addWord("一二三", EpdFontFamily::REGULAR);
  text.addWord("四五", EpdFontFamily::REGULAR);
  const auto path = (std::filesystem::temp_directory_path() / "crosspoint-text-spacing.bin").string();
  unsigned lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 200,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        Page page;
        page.elements.push_back(std::make_unique<PageLine>(std::move(line), 4, 12));
        const auto* original = static_cast<const PageLine&>(*page.elements[0]).getBlock().get();
        {
          HalFile file;
          EXPECT_TRUE(file.open(path.c_str(), "wb"));
          EXPECT_TRUE(page.serialize(file));
        }
        HalFile file;
        EXPECT_TRUE(file.open(path.c_str(), "rb"));
        auto cachedPage = Page::deserialize(file);
        EXPECT_NE(cachedPage, nullptr);
        EXPECT_EQ(cachedPage->elements.size(), 1);
        const auto* cached = static_cast<const PageLine&>(*cachedPage->elements[0]).getBlock().get();
        EXPECT_NE(cached, nullptr);
        EXPECT_EQ(cached->getBlockStyle().characterSpacing, -2);
        EXPECT_EQ(cached->wordCount(), 5);
        EXPECT_EQ(cached->wordXpos(3) - cached->wordXpos(2), 10);  // 8 + half-width space
        EXPECT_EQ(file.position(), file.size());
        EXPECT_EQ(cached->wordCount(), original->wordCount());
        for (uint16_t i = 0; i < original->wordCount(); ++i) EXPECT_EQ(cached->wordXpos(i), original->wordXpos(i));
        return true;
      },
      true, -2, 50);
  EXPECT_EQ(lines, 1u);
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, ParserAppliesTextSpacingToParagraphs) {
  parser.setTextSpacing(-1, 150);
  ASSERT_TRUE(parser.beginParse());
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  const std::string text = "\xe4\xb8\x80\xe4\xba\x8c\xe4\xb8\x89 \xe5\x9b\x9b\xe4\xba\x94";  // 一二三 四五
  ChapterHtmlSlimParser::characterData(&parser, text.c_str(), static_cast<int>(text.size()));
  ChapterHtmlSlimParser::endElement(&parser, "p");
  parser.makePages();
  ASSERT_NE(parser.currentPage, nullptr);
  unsigned lines = 0;
  for (const auto& element : parser.currentPage->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = *static_cast<const PageLine&>(*element).getBlock().get();
    ++lines;
    ASSERT_EQ(block.wordCount(), 5);
    EXPECT_EQ(block.getBlockStyle().characterSpacing, -1);
    EXPECT_EQ(block.wordXpos(1) - block.wordXpos(0), 7);   // 8 px glyph, -1 px tracking
    EXPECT_EQ(block.wordXpos(3) - block.wordXpos(2), 14);  // glyph plus 150% of a 4 px space
  }
  EXPECT_EQ(lines, 1u);
}

TEST(KoreanLayout, HangulWordsStayWholeAndWrapAtSpaces) {
  GfxRenderer renderer;
  {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, FirstLineIndent::Auto, false, false, style);
    text.addWord("가나다", EpdFontFamily::REGULAR);
    text.addWord("라마", EpdFontFamily::REGULAR);
    text.addWord("3개를", EpdFontFamily::REGULAR);
    text.addWord("iPhone을", EpdFontFamily::REGULAR);
    std::vector<std::vector<std::string>> lines;
    text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
      auto& words = lines.emplace_back();
      for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
      return true;
    });
    // 가나다 라마 is 24 + 4 + 16 px; adding 3개를 would need 72 px, and no break exists inside it.
    const std::vector<std::vector<std::string>> expected{{"가나다", "라마"}, {"3개를"}, {"iPhone을"}};
    EXPECT_EQ(lines, expected);
  }
}

TEST(KoreanLayout, JustifiedHangulStretchesOnlyWordSpaces) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, FirstLineIndent::Auto, false, false, style);
  for (const char* word : {"가나", "다라", "마바", "사아"}) text.addWord(word, EpdFontFamily::REGULAR);
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
    if (lines++ != 0) return true;
    // 3 x 16 px words + 2 x 4 px spaces leave 4 px, split across the two spaces only.
    EXPECT_EQ(line->wordCount(), 3);
    EXPECT_EQ(line->wordXpos(0), 0);
    EXPECT_EQ(line->wordXpos(1), 22);
    EXPECT_EQ(line->wordXpos(2), 44);
    return true;
  });
  EXPECT_EQ(lines, 2u);
}

TEST(KoreanLayout, HangulGluedAcrossInlineStyleIsUnbreakable) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, FirstLineIndent::Auto, false, false, style);
  text.addWord("가나", EpdFontFamily::REGULAR);
  text.addWord("한국", EpdFontFamily::REGULAR);
  text.addWord("어", EpdFontFamily::BOLD, false, /*attachToPrevious=*/true);
  std::vector<std::vector<std::string>> lines;
  text.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock> line, auto) {
    auto& words = lines.emplace_back();
    for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
    return true;
  });
  // 가나 한국 fits in 36 px, but 어 is glued to 한국, so the whole word moves down.
  const std::vector<std::vector<std::string>> expected{{"가나"}, {"한국", "어"}};
  EXPECT_EQ(lines, expected);
}

TEST_F(ChapterHtmlSlimParserTest, PassesIndentSettingsToNewTextBlock) {
  for (bool extraSpacing : {false, true}) {
    parser.extraParagraphSpacing = extraSpacing;
    parser.currentTextBlock.reset();
    parser.setParagraphIndentSpaces(5);
    parser.startNewTextBlock(BlockStyle());
    ASSERT_NE(parser.currentTextBlock, nullptr);
    EXPECT_EQ(parser.currentTextBlock->paragraphIndentSpaces, 5);
  }
}

TEST(ParagraphIndentation, PreservesTriStateAndConfiguresOnlyWesternIndent) {
  GfxRenderer renderer;
  for (const uint8_t mode : {FirstLineIndent::Auto, FirstLineIndent::Indent, FirstLineIndent::NoIndent}) {
    for (const uint8_t spaces : {0, 3, 5}) {
      for (const int cssIndent : {-6, 0, 13}) {
        for (const bool cjk : {false, true}) {
          BlockStyle style;
          style.alignment = CssTextAlign::Left;
          style.textIndentDefined = true;
          style.textIndent = cssIndent;
          ParsedText text(0, mode, false, false, style, false, spaces);
          text.addWord(cjk ? "中文" : "word", EpdFontFamily::REGULAR);
          bool sawLine = false;
          text.layoutAndExtractLines(renderer, 0, 400, [&](std::unique_ptr<TextBlock> line, auto) {
            const auto inspect = [&] {
              sawLine = true;
              const int forced = cjk ? 2 * renderer.getTextAdvanceX(0, "我", EpdFontFamily::REGULAR) : 4 * spaces;
              const int expected = mode == FirstLineIndent::Auto     ? cssIndent
                                   : mode == FirstLineIndent::Indent ? forced
                                                                     : 0;
              EXPECT_EQ(line->wordXpos(0), expected);
            };
            inspect();
            return true;
          });
          EXPECT_TRUE(sawLine);
        }
      }
    }
  }
}

TEST(ParagraphIndentation, PreservesAlignmentEligibilityAndScaledSpaceRounding) {
  GfxRenderer renderer;
  for (const auto alignment : {CssTextAlign::Left, CssTextAlign::Center}) {
    for (const uint8_t spaces : {0, 3, 5}) {
      BlockStyle style;
      style.alignment = alignment;
      ParsedText text(0, FirstLineIndent::Indent, false, false, style, false, spaces);
      text.addWord("word", EpdFontFamily::REGULAR);
      text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
        const auto inspect = [&] { EXPECT_EQ(line->wordXpos(0), alignment == CssTextAlign::Left ? 4 * spaces : 84); };
        inspect();
        return true;
      });
    }
  }
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  ParsedText text(0, FirstLineIndent::Indent, false, false, style, false, 3);
  text.addWord("word", EpdFontFamily::REGULAR);
  text.layoutAndExtractLines(
      renderer, 0, 200,
      [&](std::unique_ptr<TextBlock> line, auto) {
        const auto inspect = [&] { EXPECT_EQ(line->wordXpos(0), 9); };
        inspect();
        return true;
      },
      true, 0, 75);
}

TEST(ParagraphIndentation, ReducesOnlyFirstLineAvailableWidth) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  for (const uint8_t spaces : {0, 3, 5}) {
    ParsedText text(0, FirstLineIndent::Indent, false, false, style, false, spaces);
    text.addWord("ab", EpdFontFamily::REGULAR);
    text.addWord("cd", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock>, auto) {
      const auto inspect = [&] { ++lines; };
      inspect();
      return true;
    });
    EXPECT_EQ(lines, spaces == 0 ? 1u : 2u);
  }
}

TEST(ParagraphIndentation, PreviewKeyTracksTriStateAndWidth) {
  textsettings::PreviewKey base;
  EXPECT_EQ(base.paragraphIndentSpaces, 3);
  auto changed = base;
  changed.paragraphIndentSpaces = 0;
  EXPECT_NE(base, changed);
  changed = base;
  changed.paragraphIndentSpaces = 5;
  EXPECT_NE(base, changed);
  changed = base;
  changed.firstLineIndent = FirstLineIndent::Indent;
  EXPECT_NE(base, changed);
}

TEST(ParagraphIndentation, MissingWidthDefaultsToThreeAndClampsSavedWidths) {
  EXPECT_EQ(migrateParagraphIndentSpaces(false, 0), 3);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 0), 0);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 3), 3);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 5), 5);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, -1), 0);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 300), 5);
}

TEST_F(ChapterHtmlSlimParserTest, TxtChaptersStreamWithoutRetainingTheToc) {
  constexpr int chapters = 5000;
  {
    std::ofstream file(filepath);
    file << "<html><body>\n";
    for (int index = 0; index < chapters; ++index)
      file << "<div id=\"txt-" << index * 16 << "\"></div>C" << index << " text<br/>";
    file << "</body></html>";
  }
  parser.setTxtChapterBoundaries(true);
  parser.viewportWidth = 800;
  parser.viewportHeight = 128;
  int pages = 0;
  parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    int chapterTitles = 0;
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& text = *static_cast<const PageLine*>(element.get())->getBlock();
      for (uint16_t index = 0; index < text.wordCount(); ++index)
        if (text.wordText(index)[0] == 'C') ++chapterTitles;
    }
    EXPECT_EQ(chapterTitles, 1);
    ++pages;
  };
  ASSERT_TRUE(parser.parseAndBuildPages());
  EXPECT_EQ(pages, chapters);
  EXPECT_TRUE(parser.tocAnchors.empty());
  EXPECT_TRUE(parser.getAnchors().empty());
}
