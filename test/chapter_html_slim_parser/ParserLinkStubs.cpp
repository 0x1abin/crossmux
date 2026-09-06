#include <Epub/Page.h>
#include <Epub/TokenBoundary.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>

const char* lookupHtmlEntity(const char*, size_t) { return nullptr; }

#include <BidiUtils.h>

bool isExplicitHyphen(uint32_t) { return false; }
bool isSoftHyphen(uint32_t) { return false; }

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string&, bool) { return {}; }
void Hyphenator::setPreferredLanguage(const std::string&) {}

namespace BidiUtils {
bool startsWithRtl(const char*, int) { return false; }
bool computeVisualWordOrder(const std::vector<std::string>& words, bool, std::vector<uint16_t>& order) {
  order.resize(words.size());
  for (size_t index = 0; index < words.size(); ++index) order[index] = static_cast<uint16_t>(index);
  return true;
}
}  // namespace BidiUtils

std::vector<std::string> laidOutWords;
bool invalidateNextTextBlock = false;

TextBlock::TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>&,
                     const std::vector<EpdFontFamily::Style>&, const std::vector<uint8_t>&,
                     const std::vector<uint16_t>&, const BlockStyle& blockStyle, std::vector<std::string> rubyTexts,
                     std::vector<LinkSpan> linkSpans)
    : blockStyle(blockStyle), rubyTexts(std::move(rubyTexts)), linkSpans(std::move(linkSpans)) {
  numWords = words.size();
  isValid = !std::exchange(invalidateNextTextBlock, false);
  laidOutWords.insert(laidOutWords.end(), words.begin(), words.end());
}

bool TextBlock::hasRuby() const { return false; }

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}

bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageToFramebufferDecoder::validateAndStoreDimensions(int64_t, int64_t, ImageDimensions&, const char*) {
  return false;
}

std::vector<std::string> collectedFootnotes;
void Page::addFootnote(const char*, const char* href) { collectedFootnotes.emplace_back(href); }
bool Page::addLink(const char*, int16_t, int16_t, int16_t, int16_t) { return true; }

// Section tests exercise the real transaction/LUT paths; Page cache geometry
// has its own tests using the real serializer in footnote_list_test.
bool failPageSerialization = false;
bool Page::serialize(HalFile& file) const { return !failPageSerialization && file.write(uint8_t{0}) == 1; }
std::unique_ptr<Page> Page::deserialize(HalFile& file, bool) {
  uint8_t value = 0;
  if (file.read(&value, 1) != 1) return nullptr;
  return std::make_unique<Page>();
}

void PageLine::render(GfxRenderer&, int, int, int) {}
bool PageLine::serialize(HalFile&) { return false; }

void PageImage::render(GfxRenderer&, int, int, int) {}
void PageImage::renderPlaceholder(GfxRenderer&, int, int) const {}
bool PageImage::serialize(HalFile&) { return false; }

void PageHorizontalRule::render(GfxRenderer&, int, int, int) {}
bool PageHorizontalRule::serialize(HalFile&) { return false; }
