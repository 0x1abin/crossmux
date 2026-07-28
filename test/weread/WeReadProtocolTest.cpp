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
  static Operation::Event detailCompletionEvent(const bool coverPending) {
    return Operation::detailCompletionEvent(coverPending);
  }
  static bool detailCoverPending(const bool hasBmp, const bool hasSource, const bool hasUrl) {
    return Operation::detailCoverPending(hasBmp, hasSource, hasUrl);
  }
  static bool chapterResponseRetryRestartsReader() {
    return Operation::chapterResponseRetryPhase() == Operation::Phase::FetchReader;
  }
  static bool shouldRetryPaidPreview(const bool paid, const bool plainText, const bool hasXhtmlTag) {
    return Operation::shouldRetryPaidPreview(paid, plainText, hasXhtmlTag);
  }
  static bool imageAttemptPending(const uint8_t attempts) { return Operation::imageAttemptPending(attempts); }
  static bool imageRedirectAllowed(const uint8_t redirects) { return Operation::imageRedirectAllowed(redirects); }
  static bool validChapterRange(const uint32_t first, const uint32_t last, const uint32_t count) {
    return Operation::validChapterRange(first, last, count);
  }
  static uint32_t chapterRangeCount(const uint32_t first, const uint32_t last, const uint32_t count) {
    return Operation::chapterRangeCount(first, last, count);
  }
  static bool wholeChapterRange(const uint32_t first, const uint32_t last, const uint32_t count) {
    return Operation::wholeChapterRange(first, last, count);
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

TEST(WeReadProtocol, DecodesLongJsonStringsAcrossEveryChunkBoundary) {
  constexpr char encoded[] = "开头\\u4E2D\\u6587-\\uD83D\\uDE03-结尾";
  constexpr char expected[] = "开头中文-😃-结尾";
  for (size_t split = 0; split <= sizeof(encoded) - 1; ++split) {
    std::vector<uint8_t> output;
    WeReadProtocol::JsonStringDecoder decoder(appendBytes, &output);
    decoder.reset();
    ASSERT_TRUE(decoder.feed(encoded, split));
    ASSERT_TRUE(decoder.feed(encoded + split, sizeof(encoded) - 1 - split));
    ASSERT_TRUE(decoder.finish()) << "split=" << split;
    EXPECT_EQ(std::string(output.begin(), output.end()), expected);
  }
}

TEST(WeReadProtocol, ReplacesMalformedOrIncompleteUnicodeEscapesSafely) {
  std::vector<uint8_t> output;
  WeReadProtocol::JsonStringDecoder decoder(appendBytes, &output);
  decoder.reset();
  constexpr char encoded[] = "\\uD800x\\uDC00\\u12";
  ASSERT_TRUE(decoder.feed(encoded, sizeof(encoded) - 1));
  ASSERT_TRUE(decoder.finish());
  EXPECT_EQ(std::string(output.begin(), output.end()), "�x�\\u12");
}

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

TEST(WeReadProtocol, ParsesBoundedShelfTimestampsOrFallsBackToZero) {
  EXPECT_EQ(WeReadProtocol::parseUint32OrZero("1784923368", 10), 1784923368U);
  EXPECT_EQ(WeReadProtocol::parseUint32OrZero("4294967295", 10), UINT32_MAX);
  EXPECT_EQ(WeReadProtocol::parseUint32OrZero("", 0), 0U);
  EXPECT_EQ(WeReadProtocol::parseUint32OrZero("-1", 2), 0U);
  EXPECT_EQ(WeReadProtocol::parseUint32OrZero("12x", 3), 0U);
  EXPECT_EQ(WeReadProtocol::parseUint32OrZero("4294967296", 10), 0U);
}

TEST(WeReadProtocol, UsesOnlyARealReaderCursor) {
  EXPECT_FALSE(WeReadProtocol::hasUsablePclts(nullptr));
  EXPECT_FALSE(WeReadProtocol::hasUsablePclts(""));
  EXPECT_FALSE(WeReadProtocol::hasUsablePclts("0"));
  EXPECT_TRUE(WeReadProtocol::hasUsablePclts("chapter-token_1"));
}

TEST(WeReadProtocol, ParsesNestedRemoteProgressAcrossEveryChunkBoundary) {
  constexpr char json[] =
      R"({"ignored":{"bookId":"other-book","progress":99},"payload":{"bookId":"book-1","progress":"40.5","chapterUid":"chapter-2","metadata":{"progress":77},"chapterOffset":150}})";
  for (size_t split = 0; split <= sizeof(json) - 1; ++split) {
    WeReadProtocol::RemoteProgressParser parser("book-1");
    ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(json), split));
    ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(json) + split, sizeof(json) - 1 - split));
    ASSERT_TRUE(parser.complete()) << "split=" << split;
    EXPECT_EQ(parser.errorCode(), 0);
    EXPECT_FLOAT_EQ(parser.progress().percent, 40.5f);
    EXPECT_STREQ(parser.progress().chapterUid, "chapter-2");
    EXPECT_EQ(parser.progress().chapterOffset, 150U);
    EXPECT_TRUE(parser.progress().hasChapterOffset);
  }
}

TEST(WeReadProtocol, RejectsInvalidOrMismatchedRemoteProgressAndFallsBackToRawPercent) {
  WeReadProtocol::RemoteProgressParser parser("book-1");
  constexpr char invalid[] = R"({"bookId":"book-1","progress":101,"chapterOffset":"bad"})";
  ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(invalid), sizeof(invalid) - 1));
  EXPECT_FALSE(parser.complete());

  ASSERT_TRUE(parser.reset());
  constexpr char mismatched[] = R"({"bookId":"other-book","progress":20})";
  ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(mismatched), sizeof(mismatched) - 1));
  EXPECT_FALSE(parser.complete());

  ASSERT_TRUE(parser.reset());
  constexpr char raw[] = R"({"data":{"readingProgress":33}})";
  for (size_t i = 0; i < sizeof(raw) - 1; ++i) {
    ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(raw) + i, 1));
  }
  ASSERT_TRUE(parser.complete());
  EXPECT_FLOAT_EQ(parser.progress().percent, 33.0f);
  EXPECT_STREQ(parser.progress().chapterUid, "");
  EXPECT_FALSE(parser.progress().hasChapterOffset);

  ASSERT_TRUE(parser.reset());
  constexpr char nestedMetadata[] = R"({"progress":40,"metadata":{"progress":77}})";
  ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(nestedMetadata), sizeof(nestedMetadata) - 1));
  ASSERT_TRUE(parser.complete());
  EXPECT_FLOAT_EQ(parser.progress().percent, 40.0f);

  ASSERT_TRUE(parser.reset());
  constexpr char truncated[] = R"({"progress":12)";
  ASSERT_TRUE(parser.feed(reinterpret_cast<const uint8_t*>(truncated), sizeof(truncated) - 1));
  EXPECT_FALSE(parser.complete());
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

TEST(WeReadProtocol, ExtractsImageAttributesRegardlessOfCaseOrderAndQuotes) {
  char source[512];
  char alt[256];
  ASSERT_TRUE(WeReadProtocol::extractImageAttributes(
      "IMG class='content' ALT='插图 &amp; 说明' data-x=\"1\" SRC=\"//cdn.example/a.JPEG?x=1&amp;y=2\" /", source,
      sizeof(source), alt, sizeof(alt)));
  EXPECT_STREQ(source, "//cdn.example/a.JPEG?x=1&amp;y=2");
  EXPECT_STREQ(alt, "插图 &amp; 说明");

  ASSERT_TRUE(WeReadProtocol::extractImageAttributes("img src='https://cdn.example/a.png' alt=\"封面\"", source,
                                                     sizeof(source), alt, sizeof(alt)));
  EXPECT_STREQ(source, "https://cdn.example/a.png");
  EXPECT_STREQ(alt, "封面");
  EXPECT_FALSE(WeReadProtocol::extractImageAttributes("image src='https://cdn.example/a.png'", source, sizeof(source),
                                                      alt, sizeof(alt)));
  EXPECT_FALSE(WeReadProtocol::extractImageAttributes("img src=https://cdn.example/a.png", source, sizeof(source), alt,
                                                      sizeof(alt)));
  EXPECT_TRUE(WeReadProtocol::extractImageAttributes("img loading=lazy src='https://cdn.example/a.png'", source,
                                                     sizeof(source), alt, sizeof(alt)));
}

TEST(WeReadProtocol, NormalizesOnlyBoundedHttpsJpegAndPngUrls) {
  using WeReadProtocol::ImageType;
  char normalized[512];
  EXPECT_EQ(WeReadProtocol::normalizeImageUrl("//cdn.example/a.JPEG?x=1&amp;y=2", normalized, sizeof(normalized)),
            ImageType::Jpeg);
  EXPECT_STREQ(normalized, "https://cdn.example/a.JPEG?x=1&y=2");
  EXPECT_EQ(WeReadProtocol::normalizeImageUrl("HTTPS://cdn.example/path/a.PNG#ignored", normalized, sizeof(normalized)),
            ImageType::Png);
  EXPECT_STREQ(normalized, "https://cdn.example/path/a.PNG");

  EXPECT_EQ(WeReadProtocol::normalizeImageUrl("/images/a.jpg", normalized, sizeof(normalized)), ImageType::None);
  EXPECT_EQ(WeReadProtocol::normalizeImageUrl("http://cdn.example/a.jpg", normalized, sizeof(normalized)),
            ImageType::None);
  EXPECT_EQ(WeReadProtocol::normalizeImageUrl("https://cdn.example/a.gif", normalized, sizeof(normalized)),
            ImageType::None);
  EXPECT_EQ(WeReadProtocol::normalizeImageUrl("https://user@cdn.example/a.jpg", normalized, sizeof(normalized)),
            ImageType::None);
  EXPECT_EQ(WeReadProtocol::normalizeImageUrl("https://cdn.example/a.jpg\nX: y", normalized, sizeof(normalized)),
            ImageType::None);
  const std::string tooLong = "https://cdn.example/" + std::string(600, 'a') + ".png";
  EXPECT_EQ(WeReadProtocol::normalizeImageUrl(tooLong.c_str(), normalized, sizeof(normalized)), ImageType::None);
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

TEST(WeReadProtocol, ExtractsNamedReaderContextFields) {
  constexpr char html[] = R"({"pclts":"chapter-token_1","token":"reader-token_2"})";
  for (const auto* key : {"pclts", "token"}) {
    char output[32];
    WeReadProtocol::PsvtsExtractor extractor(output, sizeof(output), key);
    ASSERT_TRUE(extractor.reset());
    for (size_t i = 0; i < sizeof(html) - 1; ++i) {
      ASSERT_TRUE(extractor.feed(reinterpret_cast<const uint8_t*>(html) + i, 1));
    }
    ASSERT_TRUE(extractor.complete()) << key;
    EXPECT_STREQ(output, strcmp(key, "pclts") == 0 ? "chapter-token_1" : "reader-token_2");
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

TEST(WeReadClientState, ExposesDetailBeforePendingCover) {
  using Event = WeReadClient::Operation::Event;
  EXPECT_EQ(WeReadClient::OperationTestPeer::detailCompletionEvent(true), Event::DetailReady);
  EXPECT_EQ(WeReadClient::OperationTestPeer::detailCompletionEvent(false), Event::Complete);
}

TEST(WeReadClientState, RefreshesMissingOriginalCoverFromCachedDetail) {
  EXPECT_TRUE(WeReadClient::OperationTestPeer::detailCoverPending(true, false, true));
  EXPECT_TRUE(WeReadClient::OperationTestPeer::detailCoverPending(false, false, true));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::detailCoverPending(true, true, true));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::detailCoverPending(true, false, false));
}

TEST(WeReadClientState, ThrottlesImageProgressAndBoundsRetries) {
  const WeReadClient::DownloadOptions options;
  EXPECT_EQ(options.imagePolicy, WeReadStore::ImagePolicy::Embed);
  EXPECT_EQ(options.chapterScope, WeReadClient::DownloadOptions::ChapterScope::WholeBook);
  EXPECT_EQ(WeReadClient::Operation::progressDecile(0, 100), 0U);
  EXPECT_EQ(WeReadClient::Operation::progressDecile(9, 100), 0U);
  EXPECT_EQ(WeReadClient::Operation::progressDecile(10, 100), 1U);
  EXPECT_EQ(WeReadClient::Operation::progressDecile(100, 100), 10U);
  EXPECT_EQ(WeReadClient::Operation::progressDecile(0, 0), 0U);
  EXPECT_TRUE(WeReadClient::OperationTestPeer::imageAttemptPending(1));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::imageAttemptPending(2));
  EXPECT_TRUE(WeReadClient::OperationTestPeer::imageRedirectAllowed(4));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::imageRedirectAllowed(5));
}

TEST(WeReadClientState, ValidatesInclusiveChapterRanges) {
  EXPECT_TRUE(WeReadClient::OperationTestPeer::validChapterRange(0, 0, 1));
  EXPECT_TRUE(WeReadClient::OperationTestPeer::validChapterRange(3, 7, 10));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::validChapterRange(7, 3, 10));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::validChapterRange(0, 10, 10));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::validChapterRange(0, 0, 0));
  EXPECT_EQ(WeReadClient::OperationTestPeer::chapterRangeCount(3, 7, 10), 5U);
  EXPECT_EQ(WeReadClient::OperationTestPeer::chapterRangeCount(7, 3, 10), 0U);
  EXPECT_TRUE(WeReadClient::OperationTestPeer::wholeChapterRange(0, 9, 10));
  EXPECT_FALSE(WeReadClient::OperationTestPeer::wholeChapterRange(1, 9, 10));
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
