#include <GfxRenderer.h>
#include <GifToFramebufferConverter.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

class GifFramebufferTest : public ::testing::Test {
 protected:
  std::filesystem::path directory;
  std::string imagePath;
  GifToFramebufferConverter decoder;
  GfxRenderer renderer;

  void SetUp() override {
    directory =
        std::filesystem::temp_directory_path() /
        (std::string("crosspoint-gif-framebuffer-") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::create_directories(directory);
    imagePath = (directory / "pattern.gif").string();
    // Original Pillow-encoded 8x4 gray pattern used by GifBmpTest, including transparency.
    const std::string hex =
        "47494638396108000400810000000000555555aaaaaaffffff21f90401000003002c000000000800040000081500010410304020c181"
        "051102206830614286030302003b";
    std::ofstream image(imagePath, std::ios::binary);
    for (size_t i = 0; i < hex.size(); i += 2) image.put(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  }

  void TearDown() override { std::filesystem::remove_all(directory); }

  RenderConfig config(DecodeOutput output, int width = 8, int height = 4) const {
    RenderConfig result{};
    result.x = 3;
    result.y = 5;
    result.maxWidth = width;
    result.maxHeight = height;
    result.useExactDimensions = true;
    result.useDithering = false;
    result.output = output;
    result.cachePath = (directory / "pattern.pxc").string();
    return result;
  }

  std::vector<uint8_t> readCache(const RenderConfig& config) const {
    std::ifstream file(config.cachePath, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
};

TEST_F(GifFramebufferTest, CacheOnlyWithLentFramebufferMatchesNormalDecodeInEveryOrientationAndScale) {
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::LandscapeClockwise, GfxRenderer::PortraitInverted,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    renderer.orientation = orientation;
    for (const auto [width, height] : {std::pair{8, 4}, std::pair{4, 2}, std::pair{16, 8}}) {
      SCOPED_TRACE(::testing::Message() << "orientation=" << orientation << " size=" << width << "x" << height);
      renderer.framebuffer = nullptr;
      renderer.writeTargetRequests = 0;
      auto cacheOnly = config(DecodeOutput::CacheOnly, width, height);
      ASSERT_TRUE(decoder.decodeToFramebuffer(imagePath, renderer, cacheOnly));
      EXPECT_EQ(renderer.writeTargetRequests, 0);
      const auto cached = readCache(cacheOnly);
      ASSERT_EQ(cached.size(), 4u + static_cast<size_t>((width + 3) / 4 * height));
      EXPECT_EQ(cached[0], width);
      EXPECT_EQ(cached[1], 0);
      EXPECT_EQ(cached[2], height);
      EXPECT_EQ(cached[3], 0);

      std::array<uint8_t, GfxRenderer::WIDTH / 8 * GfxRenderer::HEIGHT> framebuffer;
      framebuffer.fill(0xFF);
      renderer.framebuffer = framebuffer.data();
      auto normal = config(DecodeOutput::FrameBufferAndCache, width, height);
      ASSERT_TRUE(decoder.decodeToFramebuffer(imagePath, renderer, normal));
      EXPECT_GT(renderer.writeTargetRequests, 0);
      EXPECT_TRUE(std::any_of(framebuffer.begin(), framebuffer.end(), [](uint8_t byte) { return byte != 0xFF; }));
      EXPECT_EQ(readCache(normal), cached);
    }
  }
}

TEST_F(GifFramebufferTest, CacheOnlyPreservesAnAvailableFramebuffer) {
  std::array<uint8_t, GfxRenderer::WIDTH / 8 * GfxRenderer::HEIGHT> framebuffer;
  framebuffer.fill(0xA5);
  const auto before = framebuffer;
  renderer.framebuffer = framebuffer.data();
  ASSERT_TRUE(decoder.decodeToFramebuffer(imagePath, renderer, config(DecodeOutput::CacheOnly)));
  EXPECT_EQ(renderer.writeTargetRequests, 0);
  EXPECT_EQ(framebuffer, before);
}

TEST_F(GifFramebufferTest, FramebufferRenderingWithoutACacheStillDraws) {
  std::array<uint8_t, GfxRenderer::WIDTH / 8 * GfxRenderer::HEIGHT> framebuffer;
  framebuffer.fill(0xFF);
  renderer.framebuffer = framebuffer.data();
  auto normal = config(DecodeOutput::FrameBufferAndCache);
  normal.cachePath.clear();
  ASSERT_TRUE(decoder.decodeToFramebuffer(imagePath, renderer, normal));
  EXPECT_TRUE(std::any_of(framebuffer.begin(), framebuffer.end(), [](uint8_t byte) { return byte != 0xFF; }));
}

}  // namespace
