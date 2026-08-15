#include <gtest/gtest.h>

#include <cstdint>

#include "TxtParagraph.h"

namespace {

txt_paragraph::LineInfo analyze(const char* text, const size_t length) {
  return txt_paragraph::analyzeLine(reinterpret_cast<const uint8_t*>(text), length);
}

}  // namespace

TEST(TxtParagraph, ClassifiesBlankIndentedAndPlainLines) {
  using txt_paragraph::LineKind;

  EXPECT_EQ(analyze("", 0).kind, LineKind::Blank);
  EXPECT_EQ(analyze(" \t\r", 3).kind, LineKind::Blank);

  constexpr char fullwidthIndent[] = "\xE3\x80\x80\xE3\x80\x80\xE6\xAD\xA3\xE6\x96\x87";
  const auto fullwidth = analyze(fullwidthIndent, sizeof(fullwidthIndent) - 1);
  EXPECT_EQ(fullwidth.kind, LineKind::Indented);
  EXPECT_EQ(fullwidth.contentOffset, 6U);

  constexpr char mixedIndent[] = " \t\xE3\x80\x80text";
  const auto mixed = analyze(mixedIndent, sizeof(mixedIndent) - 1);
  EXPECT_EQ(mixed.kind, LineKind::Indented);
  EXPECT_EQ(mixed.contentOffset, 5U);

  constexpr char plain[] = "\xE6\xAD\xA3\xE6\x96\x87";
  const auto unindented = analyze(plain, sizeof(plain) - 1);
  EXPECT_EQ(unindented.kind, LineKind::Plain);
  EXPECT_EQ(unindented.contentOffset, 0U);
}

TEST(TxtParagraph, IndentsOnlyParagraphStarts) {
  using txt_paragraph::LineKind;

  txt_paragraph::State fromFileStart(true);
  EXPECT_TRUE(fromFileStart.consume(LineKind::Plain));
  EXPECT_FALSE(fromFileStart.consume(LineKind::Plain));
  fromFileStart.noteBlankLine();
  EXPECT_TRUE(fromFileStart.consume(LineKind::Plain));
  EXPECT_FALSE(fromFileStart.consume(LineKind::Plain));

  txt_paragraph::State continuation(false);
  EXPECT_FALSE(continuation.consume(LineKind::Plain));
  EXPECT_TRUE(continuation.consume(LineKind::Indented));
  EXPECT_FALSE(continuation.consume(LineKind::Plain));
}
