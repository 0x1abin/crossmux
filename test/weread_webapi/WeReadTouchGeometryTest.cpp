#include <gtest/gtest.h>

#include "activities/apps/weread/WeReadTouchGeometry.h"

TEST(WeReadTouchGeometry, MapsPortraitGridAndRejectsGaps) {
  const Rect content{0, 0, 480, 700};
  const WeReadShelfGridLayout layout{
      .columns = 3,
      .rows = 3,
      .itemsPerPage = 9,
      .coverWidth = 100,
      .coverHeight = 140,
      .itemHeight = 160,
      .columnGap = 10,
      .rowGap = 20,
      .availableX = 20,
      .availableWidth = 440,
  };

  EXPECT_EQ(weReadShelfIndexFromPoint(content, layout, 0, 8, 80, 90), 0);
  EXPECT_EQ(weReadShelfIndexFromPoint(content, layout, 0, 8, 135, 450), 6);
  EXPECT_EQ(weReadShelfIndexFromPoint(content, layout, 0, 8, 245, 450), 7);
  EXPECT_EQ(weReadShelfIndexFromPoint(content, layout, 0, 8, 185, 100), -1);
  EXPECT_EQ(weReadShelfIndexFromPoint(content, layout, 0, 8, 10, 10), -1);
}

TEST(WeReadTouchGeometry, MapsLandscapePageOffsetAndPartialRow) {
  const Rect content{0, 0, 800, 420};
  const WeReadShelfGridLayout layout{
      .columns = 5,
      .rows = 2,
      .itemsPerPage = 10,
      .coverWidth = 100,
      .coverHeight = 140,
      .itemHeight = 160,
      .columnGap = 20,
      .rowGap = 20,
      .availableX = 20,
      .availableWidth = 760,
  };

  EXPECT_EQ(weReadShelfIndexFromPoint(content, layout, 10, 12, 290, 130), 10);
  EXPECT_EQ(weReadShelfIndexFromPoint(content, layout, 10, 12, 410, 130), 11);
  EXPECT_EQ(weReadShelfIndexFromPoint(content, layout, 10, 12, 530, 130), -1);
  EXPECT_EQ(weReadShelfIndexFromPoint(content, layout, 0, 0, 290, 130), -1);
}
