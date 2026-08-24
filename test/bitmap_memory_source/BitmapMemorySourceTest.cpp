#include <Bitmap.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
constexpr int kWidth = 8;
constexpr int kHeight = 2;

void appendLE16(std::vector<uint8_t>& bytes, const uint16_t value) {
  bytes.push_back(static_cast<uint8_t>(value));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void appendLE32(std::vector<uint8_t>& bytes, const uint32_t value) {
  bytes.push_back(static_cast<uint8_t>(value));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
  bytes.push_back(static_cast<uint8_t>(value >> 16));
  bytes.push_back(static_cast<uint8_t>(value >> 24));
}

void writeLE32(std::vector<uint8_t>& bytes, const size_t offset, const uint32_t value) {
  ASSERT_LE(offset + 4, bytes.size());
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
  bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> makeBitmap(const uint16_t bpp) {
  const uint32_t colors = 1u << bpp;
  const uint32_t rowBytes = (kWidth * bpp + 31) / 32 * 4;
  const uint32_t pixelOffset = 14 + 40 + colors * 4;
  const uint32_t imageBytes = rowBytes * kHeight;

  std::vector<uint8_t> bytes;
  bytes.reserve(pixelOffset + imageBytes);
  appendLE16(bytes, 0x4D42);
  appendLE32(bytes, pixelOffset + imageBytes);
  appendLE16(bytes, 0);
  appendLE16(bytes, 0);
  appendLE32(bytes, pixelOffset);
  appendLE32(bytes, 40);
  appendLE32(bytes, kWidth);
  appendLE32(bytes, static_cast<uint32_t>(-kHeight));
  appendLE16(bytes, 1);
  appendLE16(bytes, bpp);
  appendLE32(bytes, 0);
  appendLE32(bytes, imageBytes);
  appendLE32(bytes, 2835);
  appendLE32(bytes, 2835);
  appendLE32(bytes, colors);
  appendLE32(bytes, colors);

  for (uint32_t index = 0; index < colors; ++index) {
    const uint8_t gray = static_cast<uint8_t>(index * 255 / (colors - 1));
    bytes.insert(bytes.end(), {gray, gray, gray, 0});
  }
  for (uint32_t row = 0; row < kHeight; ++row) {
    for (uint32_t byte = 0; byte < rowBytes; ++byte) {
      bytes.push_back(static_cast<uint8_t>((row + 1) * 0x31u + byte * 0x17u));
    }
  }
  return bytes;
}

std::vector<uint8_t> decodeRows(Bitmap& bitmap) {
  EXPECT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
  const size_t outputRowBytes = static_cast<size_t>(bitmap.getWidth() + 3) / 4;
  std::vector<uint8_t> decoded(outputRowBytes * bitmap.getHeight());
  std::vector<uint8_t> row(static_cast<size_t>(bitmap.getRowBytes()));
  for (int y = 0; y < bitmap.getHeight(); ++y) {
    EXPECT_EQ(bitmap.readNextRow(decoded.data() + outputRowBytes * y, row.data()), BmpReaderError::Ok);
  }
  return decoded;
}
}  // namespace

TEST(BitmapMemorySource, MatchesFileSourceForPalettedRows) {
  for (const uint16_t bpp : std::array<uint16_t, 3>{1, 2, 4}) {
    const auto bytes = makeBitmap(bpp);
    HalFile file(bytes.data(), bytes.size());
    Bitmap fileBitmap(file);
    Bitmap memoryBitmap(bytes.data(), bytes.size());

    EXPECT_EQ(decodeRows(memoryBitmap), decodeRows(fileBitmap)) << "bpp=" << bpp;
    EXPECT_EQ(memoryBitmap.getBpp(), bpp);
    ASSERT_EQ(memoryBitmap.rewindToData(), BmpReaderError::Ok);

    std::vector<uint8_t> output(static_cast<size_t>(memoryBitmap.getWidth() + 3) / 4);
    std::vector<uint8_t> row(static_cast<size_t>(memoryBitmap.getRowBytes()));
    EXPECT_EQ(memoryBitmap.readNextRow(output.data(), row.data()), BmpReaderError::Ok);
  }
}

TEST(BitmapMemorySource, RejectsTruncatedHeader) {
  auto bytes = makeBitmap(1);
  bytes.resize(20);
  Bitmap bitmap(bytes.data(), bytes.size());
  EXPECT_NE(bitmap.parseHeaders(), BmpReaderError::Ok);
}

TEST(BitmapMemorySource, ReportsTruncatedPixelRow) {
  auto bytes = makeBitmap(2);
  bytes.pop_back();
  Bitmap bitmap(bytes.data(), bytes.size());
  ASSERT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);

  std::vector<uint8_t> output(static_cast<size_t>(bitmap.getWidth() + 3) / 4);
  std::vector<uint8_t> row(static_cast<size_t>(bitmap.getRowBytes()));
  EXPECT_EQ(bitmap.readNextRow(output.data(), row.data()), BmpReaderError::Ok);
  EXPECT_EQ(bitmap.readNextRow(output.data(), row.data()), BmpReaderError::ShortReadRow);
}

TEST(BitmapMemorySource, RejectsPixelOffsetPastBuffer) {
  auto bytes = makeBitmap(4);
  writeLE32(bytes, 10, static_cast<uint32_t>(bytes.size() + 1));
  Bitmap bitmap(bytes.data(), bytes.size());
  EXPECT_EQ(bitmap.parseHeaders(), BmpReaderError::SeekPixelDataFailed);
}
