#include <gtest/gtest.h>

#include "lib/EpdFont/EpdFontFamily.h"

namespace {

const EpdFontData regularData = {};
const EpdFontData boldData = {};
const EpdFontData italicData = {};
const EpdFontData boldItalicData = {};

const EpdFont regular(&regularData);
const EpdFont bold(&boldData);
const EpdFont italic(&italicData);
const EpdFont boldItalic(&boldItalicData);
const EpdFont regularAlias(&regularData);

}  // namespace

TEST(FontFamily, RegularOnlyNeedsSyntheticBold) {
  const EpdFontFamily family(&regular);

  EXPECT_FALSE(family.hasNativeBold(EpdFontFamily::REGULAR));
  EXPECT_FALSE(family.hasNativeBold(EpdFontFamily::BOLD));
  EXPECT_FALSE(family.hasNativeBold(EpdFontFamily::BOLD_ITALIC));
}

TEST(FontFamily, DistinctBoldIsNative) {
  const EpdFontFamily family(&regular, &bold);

  EXPECT_TRUE(family.hasNativeBold(EpdFontFamily::BOLD));
}

TEST(FontFamily, RegularAliasNeedsSyntheticBold) {
  const EpdFontFamily family(&regular, &regularAlias);

  EXPECT_FALSE(family.hasNativeBold(EpdFontFamily::BOLD));
}

TEST(FontFamily, ItalicOnlyNeedsSyntheticBoldItalic) {
  const EpdFontFamily family(&regular, nullptr, &italic);

  EXPECT_FALSE(family.hasNativeBold(EpdFontFamily::BOLD_ITALIC));
}

TEST(FontFamily, DistinctBoldItalicIsNative) {
  const EpdFontFamily family(&regular, nullptr, &italic, &boldItalic);

  EXPECT_TRUE(family.hasNativeBold(EpdFontFamily::BOLD_ITALIC));
}
