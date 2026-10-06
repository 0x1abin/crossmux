#pragma once

#include <HalStorage.h>
#include <expat.h>

#include <array>
#include <climits>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Epub/FootnoteEntry.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssStyle.h"

class Page;
class GfxRenderer;
class Epub;

#define MAX_WORD_SIZE 200

class ChapterHtmlSlimParser {
  std::shared_ptr<Epub> epub;
  const std::string& filepath;
  GfxRenderer& renderer;
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)> completePageFn;
  std::function<void()> popupFn;  // Popup callback
  bool imagePopupFired = false;   // popupFn fired for the first image probe (single-shot)
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  // buffer for building up words from characters, will auto break if longer than this
  // leave one char at end for null pointer
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  bool nextWordContinues = false;  // true when next flushed word attaches to previous (inline element boundary)
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  // Ruby text state
  bool inRuby = false;
  int rubyStartWordIndex = -1;
  bool collectingRubyText = false;
  std::string rubyTextBuffer;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  // Bottom of the last text line actually pushed onto the current page (the
  // page-layout cursor, minus any trailing paragraph spacing appended by
  // endParagraph). Used by applyVerticalBottomAlign so a page's last line can
  // be justified flush to the content bottom without treating the trailing
  // paragraph gap as already-consumed space.
  int16_t currentPageContentBottom = 0;
  // Height of the most recent text line pushed onto the current page (mirror of
  // addLineToPage's local). applyVerticalBottomAlign uses it as the safety cap
  // for how much a single inter-line gap may grow when justifying a page.
  int16_t currentLineHeight = 0;
  // For the page currently being assembled: one [start,end) index range into
  // currentPage->links per text line (the links added by that line). Kept in
  // lockstep with the PageLine push order so vertical bottom-align can shift a
  // line's links by the same delta as the line itself.
  std::vector<std::pair<uint16_t, uint16_t>> pendingPageLinkRanges;
  int fontId;
  float lineCompression;
  uint8_t extraParagraphSpacing;  // 0=off, 1..5=0.5x/0.75x/1x/1.25x/1.5x line height
  uint8_t firstLineIndent;
  uint8_t paragraphIndentSpaces = 3;
  int8_t characterSpacing = 0;
  uint8_t wordSpacingPercent = 100;
  uint8_t paragraphAlignment;
  bool verticalBottomAlign;  // redistribute non-final pages so the last line reaches the content bottom
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  const CssParser* cssParser;
  bool embeddedStyle;
  bool collectTouchLinks;
  uint8_t imageRendering;
  std::string contentBase;
  std::string imageBasePath;
  int imageCounter = 0;

  // Style tracking (replaces depth-based approach)
  struct StyleStackEntry {
    int depth = 0;
    bool hasBold = false, bold = false;
    bool hasItalic = false, italic = false;
    bool hasTextDecoration = false;
    CssTextDecoration textDecoration = CssTextDecoration::None;
    bool hasDirection = false;
    CssTextDirection direction = CssTextDirection::Ltr;
    bool setsParagraphDirection = false;
    bool hasTextAlign = false;
    CssTextAlign textAlign = CssTextAlign::Left;
    bool hasSup = false, sup = false;
    bool hasSub = false, sub = false;
  };
  std::vector<StyleStackEntry> inlineStyleStack;
  std::vector<BlockStyle> blockStyleStack;  // accumulated block styles from open ancestor elements
  CssStyle currentCssStyle;
  bool effectiveBold = false;
  bool effectiveItalic = false;
  CssTextDecoration effectiveTextDecoration = CssTextDecoration::None;
  bool effectiveDirectionDefined = false;
  CssTextDirection effectiveDirection = CssTextDirection::Ltr;
  bool effectiveTextAlignDefined = false;
  CssTextAlign effectiveTextAlign = CssTextAlign::Left;
  bool effectiveSup = false;
  bool effectiveSub = false;
  static constexpr size_t MAX_GRID_TABLE_COLUMNS = 4;
  static constexpr size_t MAX_GRID_TABLE_CELL_WORDS = 32;
  static constexpr size_t MAX_GRID_TABLE_CELL_BYTES = 512;
  int tableDepth = 0;
  bool insideTableCell = false;
  bool tableRowStacked = false;
  bool tableRowRtl = false;
  uint16_t tableRowsSpannedRemaining = 0;
  size_t tableCellTextBytes = 0;
  std::vector<std::unique_ptr<ParsedText>> tableRowCells;
  std::array<std::vector<std::unique_ptr<TextBlock>>, MAX_GRID_TABLE_COLUMNS> tableCellLines;
  std::vector<uint32_t> tableLineVisibleOffsets;
  bool listItemBulletOnly = false;  // true when currentTextBlock has only the <li> bullet

  // Tracks the innermost open <ul>/<ol> so <li> knows whether to number itself,
  // bullet itself, or (list-style-type: none) emit no marker at all. Pushed on
  // <ul>/<ol> open, popped on close, so nested lists restart their own counter
  // without disturbing the parent list's.
  struct ListContext {
    bool ordered = false;    // true for <ol>, false for <ul>
    bool styleNone = false;  // true when list-style-type: none is set on this list
    int counter = 0;         // incremented before each direct <li>; used as its number when ordered
    int depth = 0;           // parser depth at open time; matches the depth seen in endElement
                             // for the same tag, so a hidden nested list's close can't pop
                             // an outer list's context
  };
  std::vector<ListContext> listStack;

  // Anchor-to-page mapping: tracks which page each HTML id attribute lands on
  int completedPageCount = 0;
  std::vector<std::pair<std::string, uint16_t>> anchorData;
  std::string pendingAnchorId;  // deferred until after previous text block is flushed
  bool txtChapterBoundaries = false;
  std::vector<std::string> tocAnchors;  // the list of anchors that are TOC chapter boundaries
  uint16_t xpathParagraphIndex = 0;
  uint16_t xpathListItemIndex = 0;
  // Canonical reading-position counter: zero-based Unicode codepoints in visible
  // <body> text. Token offsets flow through line breaking so every completed page
  // records the first source character it renders.
  uint32_t visibleTextOffset = 0;
  uint32_t partWordVisibleOffset = 0;
  uint32_t currentPageVisibleOffset = 0;
  bool currentPageVisibleOffsetSet = false;
  bool allocationFailed_ = false;
  bool ioFailed_ = false;
  bool parseActive_ = false;
  void stopParsing();
  void failAllocation(const char* stage);
  bool checkMemory();
  bool insideBody = false;
  bool htmlEnded_ = false;
  bool syntheticCharacterData = false;
  uint16_t nonVisibleTextDepth = 0;

  // Footnote link tracking
  bool insideFootnoteLink = false;
  int footnoteLinkDepth = -1;
  uint8_t currentFootnoteLinkId = 0;
  FootnoteEntry currentFootnote = {};
  int currentFootnoteLinkTextLen = 0;
  std::vector<std::pair<int, FootnoteEntry>> pendingFootnotes;  // <wordIndex, entry>
  int wordsExtractedInBlock = 0;
  // Latched when a ParsedText could not be created (OOM). Together with
  // ParsedText::hadDroppedWords() this turns layout OOM into ParseStatus::Error
  // so the section build fails readably instead of emitting pages with holes.
  bool layoutOom = false;

  // Resumable parse state. The one-shot parseAndBuildPages() drives these
  // internally; the incremental section builder drives them across render ticks
  // so a large single chapter can yield between pages instead of blocking the UI
  // until the whole thing is laid out. parseFile_ and the expat parser stay alive
  // for the lifetime of the parse so it can be paused and resumed at buffer
  // boundaries.
  XML_Parser xmlParser_ = nullptr;
  HalFile parseFile_;
  uint32_t parseStartTime_ = 0;

  void updateEffectiveInlineStyle();
  void startNewTextBlock(const BlockStyle& blockStyle);
  void flushPendingAnchor();
  void flushPartWordBuffer();
  void softFlushTextBlock();
  void fallbackTableRowToStacked();
  void closeTableCell();
  void finishTableRow();
  void addTableRowSeparator();
  void setCurrentPageVisibleOffset(uint32_t offset);
  bool allocatePage();
  void makePages();
  static EpdFontFamily::Style fontStyleForTextDecoration(CssTextDecoration decoration);
  static void applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyVerticalAlignToEntry(StyleStackEntry& entry, const CssStyle& css);
  void pushTableTextStyleEntry(const CssStyle& cssStyle);
  void pushDecorationStyleEntry(CssTextDecoration defaultDecoration, const CssStyle& cssStyle);
  void emitHorizontalRule(const BlockStyle& blockStyle);
  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL defaultHandlerExpand(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  explicit ChapterHtmlSlimParser(
      std::shared_ptr<Epub> epub, const std::string& filepath, GfxRenderer& renderer, const int fontId,
      const float lineCompression, const uint8_t extraParagraphSpacing, const uint8_t firstLineIndent,
      const uint8_t paragraphAlignment, const bool verticalBottomAlign, const uint16_t viewportWidth,
      const uint16_t viewportHeight, const bool hyphenationEnabled, const bool focusReadingEnabled,
      const std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t)>& completePageFn,
      const bool embeddedStyle, const std::string& contentBase, const std::string& imageBasePath,
      const uint8_t imageRendering = 0, std::vector<std::string> tocAnchors = {},
      const std::function<void()>& popupFn = nullptr, const CssParser* cssParser = nullptr,
      const bool collectTouchLinks = false)

      : epub(epub),
        filepath(filepath),
        renderer(renderer),
        fontId(fontId),
        lineCompression(lineCompression),
        extraParagraphSpacing(extraParagraphSpacing),
        firstLineIndent(firstLineIndent),
        paragraphAlignment(paragraphAlignment),
        verticalBottomAlign(verticalBottomAlign),
        viewportWidth(viewportWidth),
        viewportHeight(viewportHeight),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        completePageFn(completePageFn),
        popupFn(popupFn),
        cssParser(embeddedStyle ? cssParser : nullptr),
        embeddedStyle(embeddedStyle),
        collectTouchLinks(collectTouchLinks),
        imageRendering(imageRendering),
        contentBase(contentBase),
        imageBasePath(imageBasePath),
        tocAnchors(std::move(tocAnchors)) {}

  ~ChapterHtmlSlimParser();
  void setTextSpacing(const int8_t character, const uint8_t wordPercent) {
    characterSpacing = character;
    wordSpacingPercent = wordPercent;
  }
  void setTxtChapterBoundaries(bool enabled) { txtChapterBoundaries = enabled; }
  void setParagraphIndentSpaces(const uint8_t spaces) { paragraphIndentSpaces = spaces; }

  // One-shot parse: builds every page before returning (begin + step* + finish).
  bool parseAndBuildPages();

  // Resumable parse, for the incremental section builder. Drive as:
  //   if (!beginParse()) fail;
  //   More: keep going / yield; Done: finishParse(); Error / OutOfMemory: abortParse().
  // Pages are emitted via completePageFn as they complete during parseStep(), so
  // the caller can stop once enough pages are built and resume on a later tick.
  enum class ParseStatus { More, Done, Error, OutOfMemory };
  bool allocationFailed() const { return allocationFailed_; }
  bool ioFailed() const { return ioFailed_; }
  bool hasFailed() const { return allocationFailed_ || ioFailed_; }
  void failIo() {
    ioFailed_ = true;
    stopParsing();
  }
  bool beginParse();
  ParseStatus parseStep();
  bool finishParse();  // flush the trailing page and tear down; returns true
  void abortParse();   // tear down without flushing (error / abandon)

  bool addLineToPage(std::unique_ptr<TextBlock> line, uint32_t visibleOffset);
  const std::vector<std::pair<std::string, uint16_t>>& getAnchors() const { return anchorData; }

  // Emit the current page through completePageFn. `doBottomAlign`
  // redistributes text-only pages so the first line stays at the top and the
  // last line reaches viewportHeight, shifting the page's links in step. The
  // chapter's final page is emitted with this flag off so it stays top-aligned.
  void completeCurrentPage(bool doBottomAlign);
  void applyVerticalBottomAlign(Page* page);

  // Byte progress of the in-flight parse, used to estimate a still-building section's total page
  // count (a giant single-spine book never fully lays out, so its real count is unknown). Valid
  // between beginParse() and finishParse()/abortParse().
  size_t parseBytesConsumed() { return parseFile_ ? parseFile_.position() : 0; }
  size_t parseTotalBytes() { return parseFile_ ? parseFile_.size() : 0; }
};
