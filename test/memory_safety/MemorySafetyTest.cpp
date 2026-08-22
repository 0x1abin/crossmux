#include <BuildScratch.h>
#include <InflateReader.h>
#include <Serialization.h>
#include <ZipParsing.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/converters/ImageDimsProbe.h"
#include "EyeShape.h"

extern "C" uint32_t uzlib_adler32(const void*, unsigned int, const uint32_t previous) { return previous; }
extern "C" uint32_t uzlib_crc32(const void*, unsigned int, const uint32_t previous) { return previous; }

namespace {

template <size_t N>
void writeLe32(std::array<uint8_t, N>& data, const size_t offset, const uint32_t value) {
  for (size_t i = 0; i < 4; ++i) data[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

template <size_t N>
void writeLe16(std::array<uint8_t, N>& data, const size_t offset, const uint16_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

TEST(SerializationSafety, TruncatedPodDoesNotModifyOutput) {
  std::istringstream input(std::string("\x01\x02", 2));
  uint32_t value = 0xAABBCCDD;
  EXPECT_FALSE(serialization::readPod(input, value));
  EXPECT_EQ(value, 0xAABBCCDDu);
}

TEST(SerializationSafety, RejectsTruncatedAndOversizedStrings) {
  std::string output = "unchanged";
  std::istringstream truncated(std::string("\x04\0\0\0ab", 6));
  EXPECT_FALSE(serialization::readString(truncated, output, 16));
  EXPECT_EQ(output, "unchanged");

  std::istringstream oversized(std::string("\x11\0\0\0", 4));
  EXPECT_FALSE(serialization::readString(oversized, output, 16));
  EXPECT_EQ(output, "unchanged");
}

TEST(ZipParsingSafety, FindsEocdAtEveryByteAlignment) {
  for (size_t offset = 0; offset < 4; ++offset) {
    std::array<uint8_t, 32> data{};
    writeLe32(data, offset, 0x06054b50);
    data[offset + 8] = 3;
    data[offset + 10] = 3;
    writeLe32(data, offset + 16, 7);
    zipParsing::EocdFields fields;
    ASSERT_TRUE(zipParsing::findEocd(data.data(), offset + 22, 64, fields));
    EXPECT_EQ(fields.totalEntries, 3);
    EXPECT_EQ(fields.centralDirOffset, 7u);
  }
}

TEST(ZipParsingSafety, RejectsOverflowAndRangesPastEof) {
  size_t outputSize = 0;
  EXPECT_FALSE(zipParsing::checkedOutputSize(UINT32_MAX, true, outputSize));
  EXPECT_TRUE(zipParsing::rangeWithin(90, 10, 100));
  EXPECT_FALSE(zipParsing::rangeWithin(90, 11, 100));
  EXPECT_FALSE(zipParsing::rangeWithin(UINT64_MAX, 1, 100));
}

TEST(ZipParsingSafety, SkipsInvalidTrailingSignature) {
  std::array<uint8_t, 64> data{};
  writeLe32(data, 0, 0x06054b50);
  writeLe32(data, 16, 0);
  writeLe32(data, 32, 0x06054b50);  // Later false candidate with an impossible comment.
  writeLe16(data, 52, 0xFFFF);

  zipParsing::EocdFields fields;
  EXPECT_TRUE(zipParsing::findEocd(data.data(), data.size(), data.size(), fields));
  EXPECT_EQ(fields.centralDirOffset, 0u);
  EXPECT_FALSE(zipParsing::findEocd(data.data(), data.size(), data.size() - 1, fields));
}

TEST(ZipImageProbe, OneShotInflateFindsHeadersWithoutAStreamingRing) {
  constexpr uint8_t PNG_DEFLATE[] = {0xeb, 0x0c, 0xf0, 0x73, 0xe7, 0xe5, 0x92, 0xe2, 0x62, 0x60, 0x60, 0xe0, 0xf5,
                                     0xf4, 0x70, 0x09, 0x62, 0x60, 0x60, 0x74, 0x00, 0xb2, 0x3f, 0x00, 0x00};
  InflateReader inflate;
  ASSERT_TRUE(inflate.init(false));
  inflate.setSource(PNG_DEFLATE, sizeof(PNG_DEFLATE));

  std::array<uint8_t, 64> prefix{};
  size_t produced = 0;
  EXPECT_EQ(inflate.readAtMost(prefix.data(), prefix.size(), &produced), InflateStatus::Done);

  ImageDimsProbe probe;
  EXPECT_LT(probe.write(prefix.data(), produced), produced);
  ImageDimensions dimensions{};
  ASSERT_TRUE(probe.getDimensions(dimensions));
  EXPECT_EQ(dimensions.width, 320);
  EXPECT_EQ(dimensions.height, 240);
}

TEST(ZipImageProbe, FindsJpegFrameWithinSixteenKilobytePrefix) {
  constexpr size_t PREFIX_LIMIT = 16 * 1024;
  std::vector<uint8_t> jpeg(PREFIX_LIMIT, 0);
  jpeg[0] = 0xff;
  jpeg[1] = 0xd8;
  jpeg[2] = 0xff;
  jpeg[3] = 0xe1;
  const uint16_t appLength = static_cast<uint16_t>(PREFIX_LIMIT - 13);
  jpeg[4] = static_cast<uint8_t>(appLength >> 8);
  jpeg[5] = static_cast<uint8_t>(appLength);
  const size_t sof = 4 + appLength;
  jpeg[sof] = 0xff;
  jpeg[sof + 1] = 0xc0;
  jpeg[sof + 2] = 0x00;
  jpeg[sof + 3] = 0x07;
  jpeg[sof + 4] = 0x08;
  jpeg[sof + 5] = 0x00;
  jpeg[sof + 6] = 0xf0;
  jpeg[sof + 7] = 0x01;
  jpeg[sof + 8] = 0x40;

  ImageDimsProbe probe;
  EXPECT_LT(probe.write(jpeg.data(), jpeg.size()), jpeg.size());
  ImageDimensions dimensions{};
  ASSERT_TRUE(probe.getDimensions(dimensions));
  EXPECT_EQ(dimensions.width, 320);
  EXPECT_EQ(dimensions.height, 240);
}

TEST(ZipImageProbe, RejectsTruncatedDeflateStream) {
  constexpr uint8_t TRUNCATED_DEFLATE[] = {0xeb, 0x0c, 0xf0};
  InflateReader inflate;
  ASSERT_TRUE(inflate.init(false));
  inflate.setSource(TRUNCATED_DEFLATE, sizeof(TRUNCATED_DEFLATE));

  std::array<uint8_t, 64> prefix{};
  size_t produced = 0;
  EXPECT_EQ(inflate.readAtMost(prefix.data(), prefix.size(), &produced), InflateStatus::Error);
}

TEST(BuildScratch, ClaimIsExclusiveAndReusable) {
  alignas(std::max_align_t) std::array<uint8_t, 128> storage{};
  buildscratch::reclaim();
  buildscratch::lend(storage.data(), storage.size());

  uint8_t* first = buildscratch::claim(64);
  ASSERT_EQ(first, storage.data());
  EXPECT_EQ(buildscratch::claim(64), nullptr);
  buildscratch::release(first);
  EXPECT_EQ(buildscratch::claim(storage.size()), storage.data());

  buildscratch::release(storage.data());
  buildscratch::reclaim();
  EXPECT_EQ(buildscratch::claim(1), nullptr);
}

TEST(AvatarMemorySafety, EyeGenerationIsDeterministicAndBounded) {
  avatar::PointF upperA[avatar::MAX_EYELID_POINTS]{};
  avatar::PointF lowerA[avatar::MAX_EYELID_POINTS]{};
  avatar::PointF upperB[avatar::MAX_EYELID_POINTS]{};
  avatar::PointF lowerB[avatar::MAX_EYELID_POINTS]{};
  avatar::Polyline aUpper{upperA, 0, avatar::MAX_EYELID_POINTS, 1, false};
  avatar::Polyline aLower{lowerA, 0, avatar::MAX_EYELID_POINTS, 1, false};
  avatar::Polyline bUpper{upperB, 0, avatar::MAX_EYELID_POINTS, 1, false};
  avatar::Polyline bLower{lowerB, 0, avatar::MAX_EYELID_POINTS, 1, false};
  avatar::Rng rngA(12345);
  avatar::Rng rngB(12345);
  const auto paramsA = avatar::generateEyeParameters(rngA, 80.0f);
  const auto paramsB = avatar::generateEyeParameters(rngB, 80.0f);
  avatar::generateOneEye(rngA, paramsA, 80.0f, aUpper, aLower);
  avatar::generateOneEye(rngB, paramsB, 80.0f, bUpper, bLower);

  ASSERT_EQ(aUpper.count, avatar::MAX_EYELID_POINTS);
  ASSERT_EQ(aLower.count, avatar::MAX_EYELID_POINTS);
  for (size_t i = 0; i < avatar::MAX_EYELID_POINTS; ++i) {
    EXPECT_FLOAT_EQ(aUpper.points[i].x, bUpper.points[i].x);
    EXPECT_FLOAT_EQ(aUpper.points[i].y, bUpper.points[i].y);
    EXPECT_FLOAT_EQ(aLower.points[i].x, bLower.points[i].x);
    EXPECT_FLOAT_EQ(aLower.points[i].y, bLower.points[i].y);
  }
}

}  // namespace
