#include <Epub/Epub/PageLink.h>
#include <gtest/gtest.h>

#include <new>
#include <utility>

namespace {
bool failNextAllocation = false;
size_t allocatedBytes = 0;
}  // namespace

void* operator new[](const size_t size, const std::nothrow_t&) noexcept {
  allocatedBytes = size;
  if (std::exchange(failNextAllocation, false)) return nullptr;
  try {
    return ::operator new[](size);
  } catch (...) {
    return nullptr;
  }
}

TEST(PageLinkTest, GrowsFromOneToBoundWithoutLosingEntries) {
  PageLinkList links;
  for (size_t i = 0; i < PageLinkList::MAX_SIZE; ++i) {
    allocatedBytes = 0;
    auto* entry = links.append();
    const auto bytes = allocatedBytes;
    ASSERT_NE(entry, nullptr);
    entry->x = i;
    const bool grows = i == 0 || (i & (i - 1)) == 0;
    EXPECT_EQ(bytes, grows ? sizeof(PageLink) * (i == 0 ? 1 : i * 2) : 0);
    for (size_t j = 0; j <= i; ++j) EXPECT_EQ(links[j].x, j);
  }
  EXPECT_EQ(links.append(), nullptr);
}

TEST(PageLinkTest, FailedGrowthPreservesLinksAndStopsAllocatingUntilClear) {
  PageLinkList links;
  ASSERT_NE(links.append(), nullptr);
  links[0].x = 42;
  failNextAllocation = true;
  auto* next = links.append();
  ASSERT_EQ(next, nullptr);
  ASSERT_TRUE(links.allocationFailed());
  EXPECT_EQ(links.size(), 1U);
  EXPECT_EQ(links[0].x, 42);
  PageLinkList moved;
  moved = std::move(links);
  allocatedBytes = 0;
  EXPECT_EQ(moved.append(), nullptr);
  EXPECT_EQ(allocatedBytes, 0U);
  EXPECT_EQ(moved[0].x, 42);
  moved.clear();
  EXPECT_NE(moved.append(), nullptr);
}

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
