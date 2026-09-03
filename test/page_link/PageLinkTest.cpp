#include <Epub/Epub/PageLink.h>
#include <gtest/gtest.h>

TEST(PageLinkTest, HitAreaIncludesFingerSlopAndMinimumWidth) {
  PageLink link;
  link.x = 100;
  link.y = 50;
  link.width = 8;
  link.height = 20;

  EXPECT_TRUE(link.contains(90, 44, 6, 28));
  EXPECT_TRUE(link.contains(117, 75, 6, 28));
  EXPECT_FALSE(link.contains(89, 50, 6, 28));
  EXPECT_FALSE(link.contains(118, 50, 6, 28));
  EXPECT_FALSE(link.contains(100, 76, 6, 28));
}

TEST(PageLinkTest, BoundedListMovesItsSingleAllocation) {
  PageLinkList links;
  ASSERT_NE(links.append(), nullptr);
  links[0].x = 42;

  PageLinkList moved = std::move(links);
  EXPECT_TRUE(links.empty());
  ASSERT_EQ(moved.size(), 1U);
  EXPECT_EQ(moved[0].x, 42);
}
