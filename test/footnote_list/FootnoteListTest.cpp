#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <Serialization.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <utility>

namespace {

bool failNextArrayAllocation = false;
size_t lastArrayAllocationSize = 0;

}  // namespace

void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
  lastArrayAllocationSize = size;
  if (std::exchange(failNextArrayAllocation, false)) return nullptr;
  try {
    return ::operator new[](size);
  } catch (...) {
    return nullptr;
  }
}

// Page.cpp's unrelated render paths are not exercised by these cache tests.
void TextBlock::render(const GfxRenderer&, int, int, int) const {}
bool TextBlock::serialize(HalFile&) const { return false; }
std::unique_ptr<TextBlock> TextBlock::deserialize(HalFile&) { return nullptr; }
void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
bool ImageBlock::serialize(HalFile&) { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile&) { return nullptr; }
bool ImageBlock::needsDecode() const { return false; }
bool ImageBlock::ensureExtracted() { return false; }
bool ImageBlock::cacheDecodedImage(GfxRenderer&, int, int) { return false; }
void GfxRenderer::drawLine(int, int, int, int, int, bool) const {}

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

TEST(FootnoteList, RestoresExactCapacityWithoutGrowing) {
  FootnoteList restored;
  lastArrayAllocationSize = 0;
  const bool resized = restored.resize(2);
  const size_t restoredAllocationSize = lastArrayAllocationSize;
  ASSERT_TRUE(resized);
  ASSERT_GT(restoredAllocationSize, 0U);
  EXPECT_EQ(restored.size(), 2U);
  EXPECT_EQ(restored.append(), nullptr);
  EXPECT_FALSE(restored.resize(3));
  EXPECT_TRUE(restored.resize(0));
  EXPECT_EQ(restored.data(), nullptr);

  lastArrayAllocationSize = 0;
  auto* first = restored.append();
  const size_t layoutAllocationSize = lastArrayAllocationSize;
  ASSERT_NE(first, nullptr);
  EXPECT_GT(layoutAllocationSize, restoredAllocationSize);
}

class PageFootnoteOomTest : public ::testing::Test {
 protected:
  void SetUp() override {
    static std::atomic<unsigned> serial{0};
    root_ = std::filesystem::temp_directory_path() / ("crossmux-footnote-oom-" + std::to_string(serial++));
    std::error_code error;
    std::filesystem::remove_all(root_, error);
    ASSERT_TRUE(std::filesystem::create_directories(root_, error));
    ASSERT_FALSE(error);
    ASSERT_EQ(setenv("CROSSPOINT_SIM_SD", root_.c_str(), 1), 0);
    ASSERT_TRUE(Storage.begin());
  }

  void TearDown() override {
    failNextArrayAllocation = false;
    unsetenv("CROSSPOINT_SIM_SD");
    std::error_code error;
    std::filesystem::remove_all(root_, error);
  }

  std::filesystem::path root_;
};

TEST_F(PageFootnoteOomTest, KeepsBodyAndSkipsFootnotesWhenAllocationFails) {
  HalFile output;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/page.bin", output));

  const uint16_t elementCount = 1;
  const uint8_t tag = TAG_PageHorizontalRule;
  const int16_t x = 10;
  const int16_t y = 20;
  const uint16_t width = 100;
  const uint8_t thickness = 2;
  serialization::writePod(output, elementCount);
  serialization::writePod(output, tag);
  serialization::writePod(output, x);
  serialization::writePod(output, y);
  serialization::writePod(output, width);
  serialization::writePod(output, thickness);

  const uint16_t footnoteCount = 2;
  serialization::writePod(output, footnoteCount);
  FootnoteEntry footnotes[footnoteCount];
  for (uint16_t i = 0; i < footnoteCount; ++i) {
    footnotes[i].number[0] = static_cast<char>('1' + i);
    footnotes[i].number[1] = '\0';
    memset(footnotes[i].href, 'a' + i, sizeof(footnotes[i].href) - 1);
    footnotes[i].href[sizeof(footnotes[i].href) - 1] = '\0';
    ASSERT_EQ(output.write(footnotes[i].number, sizeof(footnotes[i].number)), sizeof(footnotes[i].number));
    ASSERT_EQ(output.write(footnotes[i].href, sizeof(footnotes[i].href)), sizeof(footnotes[i].href));
  }
  const size_t markerPosition = output.position();
  const uint32_t marker = 0x1234ABCD;
  serialization::writePod(output, marker);
  output.close();

  HalFile input;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/page.bin", input));
  failNextArrayAllocation = true;
  auto page = Page::deserialize(input);
  const bool allocationFailureConsumed = !std::exchange(failNextArrayAllocation, false);

  ASSERT_TRUE(allocationFailureConsumed);
  ASSERT_NE(page, nullptr);
  ASSERT_EQ(page->elements.size(), 1U);
  EXPECT_EQ(page->elements[0]->getTag(), TAG_PageHorizontalRule);
  EXPECT_TRUE(page->footnotes.empty());
  EXPECT_EQ(input.position(), markerPosition);
  uint32_t restoredMarker = 0;
  ASSERT_TRUE(serialization::readPod(input, restoredMarker));
  EXPECT_EQ(restoredMarker, marker);
}
