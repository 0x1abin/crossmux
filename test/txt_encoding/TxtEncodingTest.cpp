#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>

#include "TxtEncoding.h"

TEST(TxtEncoding, DetectsAsciiUtf8AndGbkWithoutAcceptingGb18030) {
  using txt_encoding::Encoding;

  const uint8_t ascii[] = {'T', 'X', 'T'};
  const uint8_t utf8[] = {0xE4, 0xB8, 0xAD, 0xE6, 0x96, 0x87};  // 中文
  const uint8_t gbk[] = {0xD6, 0xD0, 0xCE, 0xC4};               // 中文
  const uint8_t gbkWithTrailingLead[] = {0xD6, 0xD0, 0xCE};
  const uint8_t gb18030[] = {0x81, 0x30, 0x81, 0x30};

  EXPECT_EQ(txt_encoding::detect(ascii, sizeof(ascii), true), Encoding::Unknown);
  EXPECT_EQ(txt_encoding::detect(utf8, sizeof(utf8), true), Encoding::Utf8);
  EXPECT_EQ(txt_encoding::detect(gbk, sizeof(gbk), true), Encoding::Gbk);
  EXPECT_EQ(txt_encoding::detect(gbkWithTrailingLead, sizeof(gbkWithTrailingLead), false), Encoding::Gbk);
  EXPECT_EQ(txt_encoding::detect(gb18030, sizeof(gb18030), true), Encoding::Utf8);
}

TEST(TxtEncoding, DefersDetectionWhenAChunkEndsWithOnlyAMultibyteLead) {
  using txt_encoding::Encoding;

  std::array<uint8_t, 8192> utf8Boundary{};
  utf8Boundary.fill('A');
  utf8Boundary.back() = 0xE4;
  EXPECT_EQ(txt_encoding::detect(utf8Boundary.data(), utf8Boundary.size(), false), Encoding::Unknown);

  const uint8_t utf8[] = {0xE4, 0xB8, 0xAD};
  EXPECT_EQ(txt_encoding::detect(utf8, sizeof(utf8), false), Encoding::Utf8);

  std::array<uint8_t, 8192> gbkBoundary{};
  gbkBoundary.fill('A');
  gbkBoundary.back() = 0xD6;
  EXPECT_EQ(txt_encoding::detect(gbkBoundary.data(), gbkBoundary.size(), false), Encoding::Unknown);

  const uint8_t gbk[] = {0xD6, 0xD0};
  EXPECT_EQ(txt_encoding::detect(gbk, sizeof(gbk), false), Encoding::Gbk);
}

TEST(TxtEncoding, TranscodesInPlaceAndPreservesRawOffsets) {
  std::array<uint8_t, 64> buffer{};
  const uint8_t source[] = {0xB1, 0xBE, 0xCA, 0xE9, 0xD3, 0xC9, 'T', 'X', 'T'};  // 本书由TXT
  std::memcpy(buffer.data(), source, sizeof(source));

  const auto result = txt_encoding::transcodeGbkInPlace(buffer.data(), sizeof(source), buffer.size(), true);
  EXPECT_EQ(result.rawLength, sizeof(source));
  EXPECT_EQ(result.utf8Length, std::strlen("本书由TXT"));
  EXPECT_EQ(std::string(reinterpret_cast<char*>(buffer.data()), result.utf8Length), "本书由TXT");
  EXPECT_EQ(txt_encoding::gbkSourceLength(buffer.data(), result.utf8Length), sizeof(source));
}

TEST(TxtEncoding, DefersATrailingLeadByteToTheNextChunk) {
  std::array<uint8_t, 32> buffer{0xD6, 0xD0, 0xCE};
  const auto result = txt_encoding::transcodeGbkInPlace(buffer.data(), 3, buffer.size(), false);

  EXPECT_EQ(result.rawLength, 2U);
  EXPECT_EQ(std::string(reinterpret_cast<char*>(buffer.data()), result.utf8Length), "中");
  EXPECT_EQ(txt_encoding::gbkSourceLength(buffer.data(), result.utf8Length), 2U);
}

TEST(TxtEncoding, HandlesGbkCharactersWithTwoByteUtf8Output) {
  std::array<uint8_t, 16> buffer{0xA1, 0xA4, 'A'};  // ·A
  const auto result = txt_encoding::transcodeGbkInPlace(buffer.data(), 3, buffer.size(), true);

  EXPECT_EQ(result.rawLength, 3U);
  EXPECT_EQ(result.utf8Length, std::strlen("·A"));
  EXPECT_EQ(std::string(reinterpret_cast<char*>(buffer.data()), result.utf8Length), "·A");
  EXPECT_EQ(txt_encoding::gbkSourceLength(buffer.data(), result.utf8Length), 3U);
}

TEST(TxtEncoding, FitsTheMaximumGbkSourceChunkInTheSharedBuffer) {
  constexpr size_t rawLength = 8192 * 2 / 5;
  std::array<uint8_t, 8193> buffer{};
  for (size_t i = 0; i < rawLength; i += 2) {
    buffer[i] = 0xD6;
    buffer[i + 1] = 0xD0;
  }

  const auto result = txt_encoding::transcodeGbkInPlace(buffer.data(), rawLength, buffer.size(), false);
  EXPECT_EQ(result.rawLength, rawLength);
  EXPECT_EQ(result.utf8Length, rawLength / 2 * 3);
  EXPECT_EQ(txt_encoding::gbkSourceLength(buffer.data(), result.utf8Length), rawLength);
}
