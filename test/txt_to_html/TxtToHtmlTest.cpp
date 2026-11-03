#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include "Print.h"
#include "TxtToHtml.h"

namespace {

class StringPrint : public Print {
 public:
  std::string str;
  size_t write(uint8_t b) override {
    str.push_back(static_cast<char>(b));
    return 1;
  }
  size_t write(const uint8_t* buffer, size_t size) override {
    str.append(reinterpret_cast<const char*>(buffer), size);
    return size;
  }
};

const std::string kHeader =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<!-- TXT_CACHE_VERSION: 2 -->\n"
    "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\n";
const std::string kFooter = "\n</body>\n</html>\n";

std::string convert(std::string_view content) {
  StringPrint out;
  EXPECT_TRUE(TxtToHtml::stream("test.txt", content, out));
  return out.str;
}

TEST(TxtToHtmlTest, LeadingSpacesIndentation) {
  EXPECT_EQ(convert("  two spaces"), kHeader + "&#160;&#160;two spaces" + kFooter);
  EXPECT_EQ(convert("    four spaces"), kHeader + "&#160;&#160;&#160;&#160;four spaces" + kFooter);
  EXPECT_EQ(convert("Line 1\n   three spaces"), kHeader + "Line 1<br />&#160;&#160;&#160;three spaces" + kFooter);
}

TEST(TxtToHtmlTest, MidLineConsecutiveSpaces) {
  EXPECT_EQ(convert("One space"), kHeader + "One space" + kFooter);
  EXPECT_EQ(convert("Two  spaces"), kHeader + "Two&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Three   spaces"), kHeader + "Three&#160;&#160; spaces" + kFooter);
  EXPECT_EQ(convert("Four    spaces"), kHeader + "Four&#160;&#160;&#160; spaces" + kFooter);
}

TEST(TxtToHtmlTest, PreservesCacheVersionTagsForBothFormats) {
  StringPrint out;
  ASSERT_TRUE(TxtToHtml::stream("test.MD", "text", out));
  EXPECT_EQ(out.str,
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- MD_CACHE_VERSION: 2 -->\n"
            "<!DOCTYPE html>\n<html>\n<head><title>test</title></head>\n<body>\ntext" +
                kFooter);
  EXPECT_NE(out.str.find(TxtToHtml::cacheVersionTag("test.MD")), std::string::npos);
  EXPECT_NE(convert("text").find(TxtToHtml::cacheVersionTag("test.txt")), std::string::npos);
}

}  // namespace

namespace {
struct ChunkReader {
  std::string_view text;
  size_t pos = 0;
  size_t chunk = 1;
  static int read(void* context, uint8_t* data, size_t capacity) {
    auto& reader = *static_cast<ChunkReader*>(context);
    const size_t size = std::min({capacity, reader.chunk, reader.text.size() - reader.pos});
    memcpy(data, reader.text.data() + reader.pos, size);
    reader.pos += size;
    return static_cast<int>(size);
  }
};

TEST(TxtToHtmlStream, EncodingAcrossSingleByteAndBlockBoundaries) {
  for (const size_t chunk : {1U, 7U, 8192U}) {
    const std::string utf8 = std::string(8191, 'a') + "中文\n<&>";
    ChunkReader reader{utf8, 0, chunk};
    StringPrint out;
    ASSERT_TRUE(TxtToHtml::stream("test.txt", &reader, ChunkReader::read, out));
    EXPECT_NE(out.str.find(std::string(8191, 'a') + "中文<br />&lt;&amp;&gt;"), std::string::npos);
    const std::string gbk = std::string(8191, 'a') + "\xD6\xD0\xCE\xC4\r\n";
    ChunkReader gbkReader{gbk, 0, chunk};
    StringPrint gbkOut;
    TxtToHtml::Options options;
    options.encoding = txt_encoding::Encoding::Gbk;
    ASSERT_TRUE(TxtToHtml::stream("test.txt", &gbkReader, ChunkReader::read, gbkOut, options));
    EXPECT_NE(gbkOut.str.find(std::string(8191, 'a') + "中文<br />"), std::string::npos);
  }
}

TEST(TxtToHtmlStream, RejectsTruncationReadAndWriteFailures) {
  StringPrint invalid;
  EXPECT_FALSE(TxtToHtml::stream("test.txt", "\xE4\xB8", invalid));
  StringPrint failed;
  EXPECT_FALSE(TxtToHtml::stream("test.txt", nullptr, [](void*, uint8_t*, size_t) { return -1; }, failed));
  class FullPrint : public Print {
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }
  } full;
  EXPECT_FALSE(TxtToHtml::stream("test.txt", "hello", full));
}

TEST(TxtToHtmlStream, ConvertsLargeLongLineWithoutHoldingBookOrChapters) {
  struct LargeReader {
    size_t remaining = 32 * 1024 * 1024;
    size_t maximumRequest = 0;
  } reader;
  class CountingPrint : public Print {
   public:
    size_t count = 0;
    size_t write(uint8_t) override {
      ++count;
      return 1;
    }
    size_t write(const uint8_t*, size_t size) override {
      count += size;
      return size;
    }
  } out;
  ASSERT_TRUE(TxtToHtml::stream(
      "test.txt", &reader,
      [](void* context, uint8_t* data, size_t capacity) {
        auto& reader = *static_cast<LargeReader*>(context);
        reader.maximumRequest = std::max(reader.maximumRequest, capacity);
        const size_t size = std::min(reader.remaining, capacity);
        memset(data, 'a', size);
        reader.remaining -= size;
        return static_cast<int>(size);
      },
      out));
  EXPECT_EQ(reader.maximumRequest, 8192U);
  EXPECT_EQ(out.count, 32 * 1024 * 1024 + kHeader.size() + kFooter.size());
}

TEST(TxtToHtmlStream, EmitsSourceMappingAndChapterAnchorsInSourceOrder) {
  struct Context {
    unsigned chapter = 0;
    uint32_t lastSource = 0;
    uint32_t lastVisible = 0;
    bool first = true;
  } context;
  const std::string book = "intro  \r\nChapter 1\n中文 end";
  ChunkReader reader{book};
  StringPrint out;
  TxtToHtml::Options options;
  options.context = &context;
  options.chapterOffset = [](void* p) { return static_cast<Context*>(p)->chapter == 0 ? 9U : UINT32_MAX; };
  options.nextChapter = [](void* p) {
    ++static_cast<Context*>(p)->chapter;
    return true;
  };
  options.map = [](void* p, uint32_t source, uint32_t visible, uint8_t, uint8_t) {
    auto& context = *static_cast<Context*>(p);
    if (!context.first) {
      EXPECT_GE(source, context.lastSource);
      EXPECT_GE(visible, context.lastVisible);
    }
    context.first = false;
    context.lastSource = source;
    context.lastVisible = visible;
    return true;
  };
  ASSERT_TRUE(TxtToHtml::stream("test.txt", &reader, ChunkReader::read, out, options));
  EXPECT_NE(out.str.find("<div id=\"txt-9\"></div>Chapter 1"), std::string::npos);
  EXPECT_EQ(context.chapter, 1U);
  EXPECT_EQ(context.lastSource, book.size());
  EXPECT_EQ(context.lastVisible, 1U + 5U + 9U + 2U + 4U);
}
}  // namespace
