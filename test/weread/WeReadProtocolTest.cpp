#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "WeReadClient.h"
#include "WeReadProtocol.h"

namespace WeReadClient {

struct OperationTestPeer {
  static Operation::Event chapterResponseRetryEvent(const uint8_t attempts) {
    return Operation::chapterResponseRetryEvent(attempts);
  }
  static bool chapterResponseRetryRestartsReader() {
    return Operation::chapterResponseRetryPhase() == Operation::Phase::FetchReader;
  }
  static bool shouldRetryPaidPreview(const bool paid, const bool plainText, const bool hasXhtmlTag) {
    return Operation::shouldRetryPaidPreview(paid, plainText, hasXhtmlTag);
  }
};

}  // namespace WeReadClient

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

TEST(WeReadProtocol, SeparatesAuthenticationAndRetryableChapterResponses) {
  using WeReadProtocol::ChapterResponse;
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(200, false), ChapterResponse::Content);
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(200, true), ChapterResponse::Retryable);
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(401, false), ChapterResponse::AuthenticationRequired);
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(403, false), ChapterResponse::Retryable);
  EXPECT_EQ(WeReadProtocol::classifyChapterResponse(500, false), ChapterResponse::Error);
}

TEST(WeReadProtocol, MatchesOnlyAnExactEmptyJsonObject) {
  constexpr uint8_t empty[] = " \r\n{ }\t";
  constexpr uint8_t nested[] = "{\"metadata\":{}}";
  EXPECT_TRUE(WeReadProtocol::isEmptyJsonObject(empty, sizeof(empty) - 1));
  EXPECT_FALSE(WeReadProtocol::isEmptyJsonObject(nested, sizeof(nested) - 1));
  EXPECT_FALSE(WeReadProtocol::isEmptyJsonObject(nullptr, 0));
}

TEST(WeReadProtocol, MaintainsBoundedRuntimeCookies) {
  char header[128] = {};
  ASSERT_TRUE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_vid", 6, "1", 1));
  ASSERT_TRUE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_skey", 7, "two", 3));
  ASSERT_TRUE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_extra", 8, "abc", 3));
  EXPECT_STREQ(header, "wr_vid=1; wr_skey=two; wr_extra=abc");

  ASSERT_TRUE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_skey", 7, "rotated", 7));
  EXPECT_STREQ(header, "wr_vid=1; wr_skey=rotated; wr_extra=abc");

  ASSERT_TRUE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_skey", 7, "", 0));
  EXPECT_STREQ(header, "wr_vid=1; wr_extra=abc");
  ASSERT_TRUE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_vid", 6, "", 0));
  EXPECT_STREQ(header, "wr_extra=abc");
  ASSERT_TRUE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_extra", 8, "", 0));
  EXPECT_STREQ(header, "");
}

TEST(WeReadProtocol, RejectsUnsafeOrOversizedRuntimeCookiesWithoutMutation) {
  char header[24] = "wr_vid=1";
  EXPECT_FALSE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "session", 7, "x", 1));
  EXPECT_STREQ(header, "wr_vid=1");
  EXPECT_FALSE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_bad name", 11, "x", 1));
  EXPECT_STREQ(header, "wr_vid=1");
  EXPECT_FALSE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_bad", 6, "x;y", 3));
  EXPECT_STREQ(header, "wr_vid=1");
  EXPECT_FALSE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_bad", 6, "x\r\nInjected", 11));
  EXPECT_STREQ(header, "wr_vid=1");
  EXPECT_FALSE(WeReadProtocol::mergeRuntimeCookie(header, sizeof(header), "wr_overflow", 11, "0123456789", 10));
  EXPECT_STREQ(header, "wr_vid=1");
}

TEST(WeReadProtocol, ExtractsPsvtsAcrossEveryChunkBoundary) {
  constexpr char html[] = R"(<script>window.__INITIAL_STATE__={"other":1,"psvts" : "abc_DEF-123"};</script>)";
  for (size_t split = 0; split <= sizeof(html) - 1; ++split) {
    char psvts[32];
    WeReadProtocol::PsvtsExtractor extractor(psvts, sizeof(psvts));
    ASSERT_TRUE(extractor.reset());
    ASSERT_TRUE(extractor.feed(reinterpret_cast<const uint8_t*>(html), split));
    ASSERT_TRUE(extractor.feed(reinterpret_cast<const uint8_t*>(html) + split, sizeof(html) - 1 - split));
    ASSERT_TRUE(extractor.complete()) << "split=" << split;
    EXPECT_STREQ(psvts, "abc_DEF-123");
  }
}

TEST(WeReadProtocol, RejectsMissingInvalidAndOversizedPsvts) {
  constexpr char missing[] = R"({"other":"abc"})";
  constexpr char invalid[] = R"({"psvts":"abc.def"})";
  constexpr char oversized[] = R"({"psvts":"abcdefgh"})";

  char output[16];
  WeReadProtocol::PsvtsExtractor extractor(output, sizeof(output));
  ASSERT_TRUE(extractor.reset());
  ASSERT_TRUE(extractor.feed(reinterpret_cast<const uint8_t*>(missing), sizeof(missing) - 1));
  EXPECT_FALSE(extractor.complete());

  ASSERT_TRUE(extractor.reset());
  ASSERT_TRUE(extractor.feed(reinterpret_cast<const uint8_t*>(invalid), sizeof(invalid) - 1));
  EXPECT_FALSE(extractor.complete());
  EXPECT_STREQ(output, "");

  char shortOutput[8];
  WeReadProtocol::PsvtsExtractor shortExtractor(shortOutput, sizeof(shortOutput));
  ASSERT_TRUE(shortExtractor.reset());
  ASSERT_TRUE(shortExtractor.feed(reinterpret_cast<const uint8_t*>(oversized), sizeof(oversized) - 1));
  EXPECT_FALSE(shortExtractor.complete());
  EXPECT_STREQ(shortOutput, "");
}

TEST(WeReadProtocol, DetectsAllowedXhtmlTagsAcrossEveryChunkBoundary) {
  constexpr char xhtml[] = "preview text<div class=\"chapter\">full text</div>";
  for (size_t split = 0; split <= sizeof(xhtml) - 1; ++split) {
    WeReadProtocol::XhtmlTagProbe probe;
    ASSERT_TRUE(probe.reset());
    ASSERT_TRUE(probe.feed(reinterpret_cast<const uint8_t*>(xhtml), split));
    ASSERT_TRUE(probe.feed(reinterpret_cast<const uint8_t*>(xhtml) + split, sizeof(xhtml) - 1 - split));
    EXPECT_TRUE(probe.complete()) << "split=" << split;
  }

  WeReadProtocol::XhtmlTagProbe plain;
  ASSERT_TRUE(plain.reset());
  constexpr char preview[] = "only a preview...";
  ASSERT_TRUE(plain.feed(reinterpret_cast<const uint8_t*>(preview), sizeof(preview) - 1));
  EXPECT_FALSE(plain.complete());

  WeReadProtocol::XhtmlTagProbe ignored;
  ASSERT_TRUE(ignored.reset());
  constexpr char nonContent[] = "<html><head><script>ignored</script></head><body>text</body></html>";
  ASSERT_TRUE(ignored.feed(reinterpret_cast<const uint8_t*>(nonContent), sizeof(nonContent) - 1));
  EXPECT_FALSE(ignored.complete());
}

TEST(WeReadClientState, RetryableChapterResponsesNeverSignalCompletion) {
  using Event = WeReadClient::Operation::Event;
  EXPECT_EQ(WeReadClient::OperationTestPeer::chapterResponseRetryEvent(1), Event::None);
  EXPECT_EQ(WeReadClient::OperationTestPeer::chapterResponseRetryEvent(2), Event::None);
  EXPECT_EQ(WeReadClient::OperationTestPeer::chapterResponseRetryEvent(3), Event::Failed);
  EXPECT_NE(WeReadClient::OperationTestPeer::chapterResponseRetryEvent(1), Event::ChapterComplete);
  EXPECT_NE(WeReadClient::OperationTestPeer::chapterResponseRetryEvent(3), Event::ChapterComplete);
  EXPECT_TRUE(WeReadClient::OperationTestPeer::chapterResponseRetryRestartsReader());
  EXPECT_TRUE(WeReadClient::OperationTestPeer::shouldRetryPaidPreview(true, false, false));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::shouldRetryPaidPreview(false, false, false));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::shouldRetryPaidPreview(true, true, false));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::shouldRetryPaidPreview(true, false, true));
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
