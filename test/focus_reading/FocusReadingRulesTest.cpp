#include <EpdFontFamily.h>
#include <Epub/FocusReadingRules.h>
#include <gtest/gtest.h>

TEST(FocusReadingRules, PreservesEnglishBoldPrefixLengths) {
  EXPECT_EQ(focusReading::boldPrefixLength(4), 1u);
  EXPECT_EQ(focusReading::boldPrefixLength(7), 3u);
  EXPECT_EQ(focusReading::boldPrefixLength(30), 9u);
}

TEST(FocusReadingRules, LimitsCjkLeadToIndentedHanParagraphs) {
  constexpr uint32_t HAN = 0x4E2D;  // 中
  EXPECT_TRUE(focusReading::shouldEmphasizeCjkLead(true, true, 20, HAN));
  EXPECT_FALSE(focusReading::shouldEmphasizeCjkLead(false, true, 20, HAN));
  EXPECT_FALSE(focusReading::shouldEmphasizeCjkLead(true, false, 20, HAN));
  EXPECT_FALSE(focusReading::shouldEmphasizeCjkLead(true, true, 0, HAN));
  EXPECT_FALSE(focusReading::shouldEmphasizeCjkLead(true, true, 20, 'A'));
  EXPECT_FALSE(focusReading::shouldEmphasizeCjkLead(true, true, 20, 0x201C));  // “
}

TEST(FocusReadingRules, WrapsBodyBesideLeadForSecondLineOnly) {
  EXPECT_EQ(focusReading::leadInsetForLine(true, 0, 42), 0);
  EXPECT_EQ(focusReading::leadInsetForLine(true, 1, 42), 42);
  EXPECT_EQ(focusReading::leadInsetForLine(true, 2, 42), 0);
  EXPECT_EQ(focusReading::leadInsetForLine(false, 1, 42), 0);
}

TEST(FocusReadingRules, DoublesLeadMetricsExactly) {
  EXPECT_EQ(EpdFontFamily::scaleFocusLeadMetric(12), 24);
  EXPECT_EQ(EpdFontFamily::scaleFocusLeadMetric(13), 26);
  EXPECT_EQ(EpdFontFamily::scaleFocusLeadMetric(-2), -4);
}
