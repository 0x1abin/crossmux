#include <Epub/FootnoteEntry.h>
#include <gtest/gtest.h>

#include <cstring>
#include <utility>

TEST(FootnoteList, AllocatesOnceAndMovesItsBoundedStorage) {
  FootnoteList footnotes;
  FootnoteEntry* storage = nullptr;

  for (size_t i = 0; i < FootnoteList::MAX_SIZE; ++i) {
    auto* entry = footnotes.append();
    ASSERT_NE(entry, nullptr);
    if (i == 0) storage = footnotes.data();
    EXPECT_EQ(footnotes.data(), storage);
    entry->number[0] = static_cast<char>('A' + i);
    entry->number[1] = '\0';
    memset(entry->href, 'x', sizeof(entry->href) - 1);
    entry->href[sizeof(entry->href) - 1] = '\0';
  }

  EXPECT_EQ(footnotes.size(), FootnoteList::MAX_SIZE);
  EXPECT_EQ(footnotes.append(), nullptr);

  FootnoteList moved(std::move(footnotes));
  EXPECT_TRUE(footnotes.empty());
  EXPECT_EQ(footnotes.data(), nullptr);
  EXPECT_EQ(moved.data(), storage);
  EXPECT_EQ(moved.size(), FootnoteList::MAX_SIZE);
  for (size_t i = 0; i < FootnoteList::MAX_SIZE; ++i) {
    EXPECT_EQ(moved[i].number[0], static_cast<char>('A' + i));
    EXPECT_EQ(strlen(moved[i].href), sizeof(moved[i].href) - 1);
  }

  FootnoteList restored;
  EXPECT_TRUE(restored.resize(2));
  EXPECT_EQ(restored.size(), 2U);
  EXPECT_FALSE(restored.resize(FootnoteList::MAX_SIZE + 1));
}
