#include "AirPageImageRenderer.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <optional>

#include "Epub/blocks/ImageBlock.h"
#include "components/themes/BaseTheme.h"

namespace airpage {

namespace {

bool renderBmpPass(const GfxRenderer& renderer, const Rect& bounds, const SelectedImage& selected) {
  HalFile file;
  if (!Storage.openFileForRead("AIRP", selected.path, file)) return false;
  Bitmap bitmap(file, /*dithering=*/false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  if (bitmap.getWidth() != selected.image.width || bitmap.getHeight() != selected.image.height) return false;

  renderer.drawBitmap(bitmap, bounds.x, bounds.y, bounds.width, bounds.height, 0, 0);
  return true;
}

bool renderPass(GfxRenderer& renderer, const Rect& bounds, const SelectedImage& selected, ImageBlock* jpegBlock) {
  switch (selected.image.format) {
    case ImageFormat::None:
      return false;
    case ImageFormat::Bmp:
      return renderBmpPass(renderer, bounds, selected);
    case ImageFormat::Jpeg:
      return jpegBlock && jpegBlock->render(renderer, bounds.x, bounds.y, ImageBlock::PixelCachePolicy::Stream);
  }
  return false;
}

}  // namespace

void AirPageImageRenderer::resetSessionFailures() { ImageBlock::clearSessionRenderFailures(); }

void AirPageImageRenderer::releaseSessionResources() { ImageBlock::releaseRenderCache(); }

Rect AirPageImageRenderer::fittedBounds(const Rect& viewport, const ImageInfo& image) {
  if (viewport.width <= 0 || viewport.height <= 0 || image.width <= 0 || image.height <= 0) return Rect{};

  int width = image.width;
  int height = image.height;
  if (width > viewport.width || height > viewport.height) {
    const int64_t widthLimitedHeight = static_cast<int64_t>(image.height) * viewport.width / image.width;
    if (widthLimitedHeight <= viewport.height) {
      width = viewport.width;
      height = static_cast<int>(std::max<int64_t>(1, widthLimitedHeight));
    } else {
      height = viewport.height;
      width =
          static_cast<int>(std::max<int64_t>(1, static_cast<int64_t>(image.width) * viewport.height / image.height));
    }
  }

  return Rect{viewport.x + (viewport.width - width) / 2, viewport.y + (viewport.height - height) / 2, width, height};
}

bool AirPageImageRenderer::render(GfxRenderer& renderer, const Rect& viewport, const SelectedImage& selected) {
  const Rect bounds = fittedBounds(viewport, selected.image);
  if (bounds.width <= 0 || bounds.height <= 0) return false;

  struct RenderCleanup {
    GfxRenderer& renderer;
    ~RenderCleanup() {
      renderer.setRenderMode(GfxRenderer::BW);
      ImageBlock::releaseRenderCache();
    }
  } cleanup{renderer};

  std::optional<ImageBlock> jpegBlock;
  if (selected.image.format == ImageFormat::Jpeg) {
    jpegBlock.emplace(selected.path, "", static_cast<int16_t>(bounds.width), static_cast<int16_t>(bounds.height));
  }
  ImageBlock* jpeg = jpegBlock ? &*jpegBlock : nullptr;

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  if (!renderPass(renderer, bounds, selected, jpeg)) return false;

  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  renderer.setRenderMode(GfxRenderer::BW);
  if (!renderPass(renderer, bounds, selected, jpeg)) return false;
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  if (!selected.image.hasGrayscale) return true;

  const auto abortGrayscale = [&renderer] {
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    renderer.cleanupGrayscaleWithFrameBuffer();
  };

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderer.clearScreen(0x00);
  if (!renderPass(renderer, bounds, selected, jpeg)) {
    abortGrayscale();
    return false;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.clearScreen(0x00);
  if (!renderPass(renderer, bounds, selected, jpeg)) {
    abortGrayscale();
    return false;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.displayGrayBuffer();

  renderer.clearScreen();
  if (!renderPass(renderer, bounds, selected, jpeg)) {
    abortGrayscale();
    return false;
  }
  renderer.cleanupGrayscaleWithFrameBuffer();
  return true;
}

}  // namespace airpage
