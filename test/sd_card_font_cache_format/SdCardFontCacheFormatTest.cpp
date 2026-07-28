#include <gtest/gtest.h>

#include <cstring>
#include <limits>

#include "EpdFont/SdCardFontCacheFormat.h"

namespace {

sd_card_font_cache_format::Header makeValidHeader() {
  using namespace sd_card_font_cache_format;
  Header header{};
  memcpy(header.magic, MAGIC, sizeof(header.magic));
  header.version = VERSION;
  header.headerSize = sizeof(header);
  header.payloadSize = 1024;
  strcpy(header.sourcePath, "/.fonts/Test/Test_14.cpfont");
  header.headerCrc = headerCrc(header);
  return header;
}

}  // namespace

TEST(SdCardFontCacheFormat, ValidatesCommittedHeaderFields) {
  using namespace sd_card_font_cache_format;

  const Header valid = makeValidHeader();
  EXPECT_TRUE(isHeaderValid(valid, 2048));

  Header header = valid;
  constexpr char legacyMagic[8] = "CPOTAF1";
  memcpy(header.magic, legacyMagic, sizeof(header.magic));
  header.headerCrc = headerCrc(header);
  EXPECT_FALSE(isHeaderValid(header, 2048));

  header = valid;
  header.magic[0] ^= 1;
  header.headerCrc = headerCrc(header);
  EXPECT_FALSE(isHeaderValid(header, 2048));

  header = valid;
  header.version++;
  header.headerCrc = headerCrc(header);
  EXPECT_FALSE(isHeaderValid(header, 2048));

  header = valid;
  header.headerSize--;
  header.headerCrc = headerCrc(header);
  EXPECT_FALSE(isHeaderValid(header, 2048));

  header = valid;
  header.payloadSize = 2049;
  header.headerCrc = headerCrc(header);
  EXPECT_FALSE(isHeaderValid(header, 2048));

  header = valid;
  header.payloadSize = 0;
  header.headerCrc = headerCrc(header);
  EXPECT_FALSE(isHeaderValid(header, 2048));

  header = valid;
  memset(header.sourcePath, 'x', sizeof(header.sourcePath));
  header.headerCrc = headerCrc(header);
  EXPECT_FALSE(isHeaderValid(header, 2048));

  header = valid;
  header.sourcePath[1] ^= 1;
  EXPECT_FALSE(isHeaderValid(header, 2048));
}

TEST(SdCardFontCacheFormat, BoundsReadsToCommittedPayload) {
  using sd_card_font_cache_format::containsPayloadRange;

  constexpr size_t payloadSize = 1024;
  EXPECT_TRUE(containsPayloadRange(payloadSize, 0, payloadSize));
  EXPECT_TRUE(containsPayloadRange(payloadSize, payloadSize, 0));
  EXPECT_FALSE(containsPayloadRange(payloadSize, payloadSize, 1));
  EXPECT_FALSE(containsPayloadRange(payloadSize, payloadSize + 1, 0));
  EXPECT_FALSE(containsPayloadRange(payloadSize, 1, payloadSize));
  EXPECT_FALSE(containsPayloadRange(payloadSize, 1, std::numeric_limits<size_t>::max()));
}
