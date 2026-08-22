#include <gtest/gtest.h>

#include "util/ReadingBackground.h"

TEST(ReadingBackground, CalculatesFourFrameOffsetsAndLength) {
  constexpr uint32_t frameSize = 48000;
  EXPECT_EQ(sizeof(readingBackground::CacheHeader), 18);
  EXPECT_EQ(readingBackground::frameOffset(0, frameSize), sizeof(readingBackground::CacheHeader));
  EXPECT_EQ(readingBackground::frameOffset(3, frameSize), sizeof(readingBackground::CacheHeader) + 3 * frameSize);
  EXPECT_EQ(readingBackground::cacheFileSize(frameSize), sizeof(readingBackground::CacheHeader) + 4 * frameSize);
  EXPECT_TRUE(readingBackground::isValidOrientation(3));
  EXPECT_FALSE(readingBackground::isValidOrientation(4));
}

TEST(ReadingBackground, ValidatesHeaderVersionDimensionsAndExactLength) {
  readingBackground::CacheHeader header;
  header.displayWidth = 480;
  header.displayHeight = 800;
  header.frameSize = 48000;
  const auto size = readingBackground::cacheFileSize(header.frameSize);
  EXPECT_TRUE(readingBackground::isValidHeader(header, 480, 800, 48000, size));

  header.version++;
  EXPECT_FALSE(readingBackground::isValidHeader(header, 480, 800, 48000, size));
  header.version = readingBackground::CACHE_VERSION;
  header.reserved = 1;
  EXPECT_FALSE(readingBackground::isValidHeader(header, 480, 800, 48000, size));
  header.reserved = 0;
  EXPECT_FALSE(readingBackground::isValidHeader(header, 800, 480, 48000, size));
  EXPECT_FALSE(readingBackground::isValidHeader(header, 480, 800, 48000, size - 1));
}
