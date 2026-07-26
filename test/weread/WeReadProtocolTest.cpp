#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "WeReadProtocol.h"

namespace {

bool goldenMd5(const uint8_t* data, size_t len, char out[33]) {
  const std::string input(reinterpret_cast<const char*>(data), len);
  struct Golden {
    const char* input;
    const char* hash;
  };
  static constexpr Golden kGolden[] = {
      {"1234567890", "e807f1fcf82d132f9bb018ca6738a19f"},
      {"e80329f0775bcd15g010", "9e5c3ccf063207b7c85797955f2bd0c9"},
      {"abc-中文", "db24ca875a6790a754019ae5a9382600"},
      {"db24200146162632de4b8ade69687", "7c6eaf864abf7e6c83ba794ffb2cb21e"},
      {"1784923368", "2fe953a6b84475f7c8bc1796685fce7c"},
      {"2fe327c07aa393b0g018", "8b496e696c13bf2be9fc0eaf12895eac"},
  };
  for (const auto& golden : kGolden) {
    if (input == golden.input) {
      memcpy(out, golden.hash, 33);
      return true;
    }
  }
  return false;
}

bool appendBytes(void* ctx, const uint8_t* data, size_t len) {
  auto* out = static_cast<std::vector<uint8_t>*>(ctx);
  out->insert(out->end(), data, data + len);
  return true;
}

}  // namespace

TEST(WeReadProtocol, ClassifiesUnavailableChapterResponses) {
  using WeReadProtocol::ChapterResponse;
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(200, false), ChapterResponse::Content);
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(200, true), ChapterResponse::Unavailable);
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(401, false), ChapterResponse::Unavailable);
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(403, false), ChapterResponse::Unavailable);
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(500, false), ChapterResponse::Error);
}

TEST(WeReadProtocol, EncodesNumericAndUtf8Ids) {
  char encoded[128];
  ASSERT_TRUE(WeReadProtocol::encodeId("1234567890", goldenMd5, encoded, sizeof(encoded)));
  EXPECT_STREQ(encoded, "e80329f0775bcd15g0109e5");

  ASSERT_TRUE(WeReadProtocol::encodeId("abc-中文", goldenMd5, encoded, sizeof(encoded)));
  EXPECT_STREQ(encoded, "db24200146162632de4b8ade696877c6");

  ASSERT_TRUE(WeReadProtocol::encodeId("1784923368", goldenMd5, encoded, sizeof(encoded)));
  EXPECT_STREQ(encoded, "2fe327c07aa393b0g0188b4");
}

TEST(WeReadProtocol, RejectsMd5FailureAndShortOutput) {
  char encoded[128];
  EXPECT_FALSE(WeReadProtocol::encodeId("not-a-golden-value", goldenMd5, encoded, sizeof(encoded)));
  EXPECT_FALSE(WeReadProtocol::encodeId("1234567890", goldenMd5, encoded, 16));
}

TEST(WeReadProtocol, RejectsMismatchedAndMalformedMd5) {
  constexpr char expected[] = "e807f1fcf82d132f9bb018ca6738a19f";
  EXPECT_TRUE(WeReadProtocol::matchesMd5(expected, 32, "E807F1FCF82D132F9BB018CA6738A19F", 32));
  EXPECT_FALSE(WeReadProtocol::matchesMd5(expected, 32, "e807f1fcf82d132f9bb018ca6738a190", 32));
  EXPECT_FALSE(WeReadProtocol::matchesMd5(expected, 32, "e807f1fcf82d132f9bb018ca6738a19z", 32));
  EXPECT_FALSE(WeReadProtocol::matchesMd5(expected, 31, expected, 32));
}

TEST(WeReadProtocol, SignsKnownQueries) {
  char signature[24];
  ASSERT_TRUE(WeReadProtocol::signQuery("b=abc&c=def&ct=1700000000&pc=ghi&prevChapter=false&ps=jkl&r=81&sc=1&st=0",
                                        signature, sizeof(signature)));
  EXPECT_STREQ(signature, "784d746a");

  ASSERT_TRUE(WeReadProtocol::signQuery("a=1&b=hello%20world", signature, sizeof(signature)));
  EXPECT_STREQ(signature, "2a2e5d8e");
}

TEST(WeReadProtocol, DecodesUnicodeEscapesIncludingSurrogates) {
  constexpr char input[] = "A\\u4E2D\\uD83D\\uDE00";
  char decoded[32];
  WeReadProtocol::decodeJsonString(input, strlen(input), decoded, sizeof(decoded));
  EXPECT_STREQ(decoded, "A中😀");

  char bounded[5];
  EXPECT_EQ(WeReadProtocol::decodeJsonString("\\u4E2D\\u6587", 12, bounded, sizeof(bounded)), 3u);
  EXPECT_STREQ(bounded, "中");

  char malformed[16];
  WeReadProtocol::decodeJsonString("\\uD83Dx\\uDE00", 13, malformed, sizeof(malformed));
  EXPECT_STREQ(malformed, "�x�");
}

TEST(WeReadProtocol, ComputesSwapPairs) {
  constexpr char encoded[] = "abcdefghijklmnopqrstuvwxyz";
  uint32_t positions[10] = {};
  const size_t count = WeReadProtocol::swapPositions(
      strlen(encoded), reinterpret_cast<const uint8_t*>(encoded + strlen(encoded) - 3), 3, positions);
  const uint32_t expected[] = {12, 2, 2, 3, 12, 2, 20, 15, 12, 2};
  ASSERT_EQ(count, std::size(expected));
  for (size_t i = 0; i < count; ++i) EXPECT_EQ(positions[i], expected[i]);
}

TEST(WeReadProtocol, Base64UrlDecodesAcrossEveryBoundary) {
  constexpr char encoded[] = "SGVsbG8sIOS4lueVjCE";
  constexpr char expected[] = "Hello, 世界!";
  for (size_t chunk = 1; chunk <= strlen(encoded); ++chunk) {
    std::vector<uint8_t> decoded;
    WeReadProtocol::Base64UrlDecoder decoder(appendBytes, &decoded);
    for (size_t offset = 0; offset < strlen(encoded); offset += chunk) {
      ASSERT_TRUE(
          decoder.feed(reinterpret_cast<const uint8_t*>(encoded + offset), std::min(chunk, strlen(encoded) - offset)));
    }
    ASSERT_TRUE(decoder.finish());
    EXPECT_EQ(std::string(decoded.begin(), decoded.end()), expected);
  }

  std::vector<uint8_t> decoded;
  WeReadProtocol::Base64UrlDecoder invalid(appendBytes, &decoded);
  ASSERT_TRUE(invalid.feed(reinterpret_cast<const uint8_t*>("A"), 1));
  EXPECT_FALSE(invalid.finish());
}

TEST(WeReadProtocol, Crc32MatchesStandardVector) {
  constexpr char input[] = "123456789";
  uint32_t crc = 0xFFFFFFFF;
  crc = WeReadProtocol::crc32Update(crc, reinterpret_cast<const uint8_t*>(input), strlen(input));
  EXPECT_EQ(crc ^ 0xFFFFFFFF, 0xCBF43926u);
}
