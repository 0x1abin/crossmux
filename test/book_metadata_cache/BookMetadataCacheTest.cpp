#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "ZipFile.h"

// Counting allocator: heap bytes allocated while `counting` is set and not yet freed.
namespace heap {
bool counting = false;
size_t live = 0;
size_t failArraySize = 0;

struct alignas(std::max_align_t) Header {
  size_t size;
  bool counted;
};

void* allocate(const size_t size) {
  auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
  if (!header) return nullptr;
  header->size = size;
  header->counted = counting;
  if (counting) live += size;
  return header + 1;
}

void release(void* p) {
  if (!p) return;
  auto* header = static_cast<Header*>(p) - 1;
  if (header->counted) live -= header->size;
  std::free(header);
}
}  // namespace heap

void* operator new(const size_t size) {
  void* p = heap::allocate(size);
  if (!p) throw std::bad_alloc();
  return p;
}
void* operator new[](const size_t size) { return operator new(size); }
void* operator new(const size_t size, const std::nothrow_t&) noexcept { return heap::allocate(size); }
void* operator new[](const size_t size, const std::nothrow_t&) noexcept {
  return heap::failArraySize == size ? nullptr : heap::allocate(size);
}
void operator delete(void* p) noexcept { heap::release(p); }
void operator delete[](void* p) noexcept { heap::release(p); }
void operator delete(void* p, size_t) noexcept { heap::release(p); }
void operator delete[](void* p, size_t) noexcept { heap::release(p); }

namespace {

std::string chapterHref(const int i) { return "OEBPS/Text/chapter" + std::to_string(i) + ".xhtml"; }
// Empty chapters, some across the window edge at item 32, give equal running totals.
uint32_t chapterBytes(const int i) {
  return (i % 37 == 5 || (i >= 30 && i < 35)) ? 0 : 1500 + static_cast<uint32_t>(i) * 7919 % 30000;
}

std::string cacheDir() {
  return testing::TempDir() + "book_metadata_cache_" + testing::UnitTest::GetInstance()->current_test_info()->name();
}

bool linearSpine(const int i) { return i % 17 != 3 && i != 31 && i != 32; }

// Writes book.bin through the cache's own writer; returns every chapter's cumulative size.
std::vector<uint32_t> buildBook(const std::string& dir, const int spineCount, const bool mixedLinear = false) {
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  zipEntrySizes.clear();
  std::vector<uint32_t> cumulative;
  for (int i = 0; i < spineCount; i++) {
    zipEntrySizes[chapterHref(i)] = chapterBytes(i);
    cumulative.push_back((i > 0 ? cumulative.back() : 0) + ((!mixedLinear || linearSpine(i)) ? chapterBytes(i) : 0));
  }
  BookMetadataCache cache(dir);
  EXPECT_TRUE(cache.beginWrite() && cache.beginContentOpfPass());
  for (int i = 0; i < spineCount; i++) cache.createSpineEntry(chapterHref(i), !mixedLinear || linearSpine(i));
  EXPECT_TRUE(cache.endContentOpfPass() && cache.beginTocPass());
  for (int i = 0; i < spineCount; i++) cache.createTocEntry("Chapter " + std::to_string(i), chapterHref(i), "", 1);
  EXPECT_TRUE(cache.endTocPass() && cache.endWrite());
  EXPECT_TRUE(cache.buildBookBin("book.epub", {}));
  EXPECT_TRUE(cache.cleanupTmpFiles());
  return cumulative;
}

}  // namespace

uint32_t cumulativeSize(BookMetadataCache& cache, const int i) {
  uint32_t value = 0;
  EXPECT_TRUE(cache.getCumulativeSize(i, value));
  return value;
}

// A loaded book must not hold heap per chapter: 5,000 chapters may hold at most 1 KB more than 30.
TEST(BookMetadataCacheHeap, ResidentHeapDoesNotGrowWithSpineCount) {
  size_t resident[2] = {};
  for (const int spineCount : {30, 5000}) {
    const std::vector<uint32_t> cumulative = buildBook(cacheDir(), spineCount);
    BookMetadataCache cache(cacheDir());
    heap::live = 0;
    heap::counting = true;
    EXPECT_TRUE(cache.load());
    const size_t loaded = heap::live;
    for (int i = 0; i < spineCount; i++) EXPECT_EQ(cumulativeSize(cache, i), cumulative[i]) << "spine " << i;
    EXPECT_EQ(heap::live, loaded) << spineCount << " chapters";
    heap::counting = false;
    resident[spineCount == 5000] = loaded;
  }
  EXPECT_LE(resident[1], resident[0] + 1024) << "30 chapters: " << resident[0] << " B";
}

// book.bin cut short inside a window: the window answers the total before it, never less.
TEST(BookMetadataCacheSizes, TruncatedWindowFallsBackToTheTotalBeforeIt) {
  const std::vector<uint32_t> cumulative = buildBook(cacheDir(), 100);
  const std::string path = cacheDir() + "/book.bin";
  std::ifstream in(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const size_t href = bytes.find(chapterHref(64));
  ASSERT_NE(href, std::string::npos);
  BookMetadataCache cache(cacheDir());
  ASSERT_TRUE(cache.load());
  EXPECT_EQ(cumulativeSize(cache, 0), cumulative[0]);
  std::filesystem::resize_file(path, href + chapterHref(64).size());  // just before item 64's size
  EXPECT_EQ(cumulativeSize(cache, 63), cumulative[63]);
  uint32_t unchanged = 123;
  EXPECT_FALSE(cache.getCumulativeSize(64, unchanged));
  EXPECT_EQ(unchanged, 123u);
  // A lookup into that window tries the refill once and keeps the fallback; the next lookup retries.
  for (int attempt = 0; attempt < 2; attempt++) {
    const int opens = Storage.readOpens;
    EXPECT_EQ(cache.getSpineIndexForSize(cumulative[70]), -1);
    EXPECT_EQ(Storage.readOpens - opens, 1) << "attempt " << attempt;
  }
}

// The lookup answers what a scan of every item answers, for every boundary, and reads at most one window.
TEST(BookMetadataCacheSizes, SpineIndexForSizeMatchesLinearScan) {
  for (const int spineCount : {100, 5000}) {
    const std::vector<uint32_t> cumulative = buildBook(cacheDir(), spineCount);
    BookMetadataCache scanned(cacheDir());
    BookMetadataCache cache(cacheDir());
    ASSERT_TRUE(scanned.load() && cache.load());
    std::vector<uint32_t> totals;
    for (int i = 0; i < spineCount; i++) totals.push_back(cumulativeSize(scanned, i));
    const uint32_t bookSize = cumulative.back();
    std::vector<uint32_t> targets = {0, bookSize - 1, bookSize, bookSize + 1, 0};
    for (const uint32_t total : cumulative) targets.insert(targets.end(), {total - 1, total, total + 1});
    for (const uint32_t size : targets) {
      int expected = -1;
      for (int i = 0; i < spineCount && expected < 0; i++) {
        if (totals[i] >= size) expected = i;
      }
      const int opens = Storage.readOpens;
      EXPECT_EQ(cache.getSpineIndexForSize(size), expected) << spineCount << " items, size " << size;
      EXPECT_LE(Storage.readOpens - opens, 1) << spineCount << " items, size " << size;
    }
  }
}

TEST(BookMetadataCacheSizes, LinearFlagsAndZeroWeightSurviveWindowBoundaries) {
  const auto cumulative = buildBook(cacheDir(), 100, true);
  BookMetadataCache cache(cacheDir());
  ASSERT_TRUE(cache.load());
  for (int i = 0; i < 100; ++i) {
    bool linear = !linearSpine(i);
    ASSERT_TRUE(cache.getLinear(i, linear));
    EXPECT_EQ(linear, linearSpine(i)) << i;
    EXPECT_EQ(cache.getSpineEntry(i).linear, linearSpine(i)) << i;
    EXPECT_EQ(cumulativeSize(cache, i), cumulative[i]) << i;
  }
  for (uint32_t target : cumulative) {
    const auto first = std::lower_bound(cumulative.begin(), cumulative.end(), target);
    EXPECT_EQ(cache.getSpineIndexForSize(target), first - cumulative.begin());
  }
}

TEST(BookMetadataCacheSizes, OptionalSparseIndexAllocationFailureKeepsExactSizesAndLinearFlags) {
  const auto cumulative = buildBook(cacheDir(), 100, true);
  BookMetadataCache cache(cacheDir());
  heap::failArraySize = 4 * sizeof(uint32_t);  // ceil(100 / 32) sparse entries
  const bool loaded = cache.load();
  heap::failArraySize = 0;
  ASSERT_TRUE(loaded);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(cumulativeSize(cache, i), cumulative[i]);
    bool linear = false;
    ASSERT_TRUE(cache.getLinear(i, linear));
    EXPECT_EQ(linear, linearSpine(i));
    const auto first = std::lower_bound(cumulative.begin(), cumulative.end(), cumulative[i]);
    EXPECT_EQ(cache.getSpineIndexForSize(cumulative[i]), first - cumulative.begin());
  }
  EXPECT_EQ(cache.getSpineIndexForSize(cumulative.back() + 1), -1);
}

TEST(BookMetadataCacheSizes, InvalidLinearByteCannotPublishCache) {
  buildBook(cacheDir(), 40, true);
  const std::string path = cacheDir() + "/book.bin";
  std::ifstream in(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), {});
  const size_t href = bytes.find(chapterHref(32));
  ASSERT_NE(href, std::string::npos);
  std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
  file.seekp(href + chapterHref(32).size() + sizeof(uint32_t) + sizeof(int16_t));
  file.put(2);
  file.close();
  BookMetadataCache cache(cacheDir());
  EXPECT_FALSE(cache.load());
  EXPECT_FALSE(cache.isLoaded());
}
