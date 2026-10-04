#include "AirPageImageRenderer.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <optional>

#include "Epub/blocks/ImageBlock.h"
#include "Epub/converters/JpegToFramebufferConverter.h"
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

bool renderPass(GfxRenderer& renderer, const Rect& bounds, const SelectedImage& selected, ImageBlock* jpegBlock,
                ImageRenderError* error) {
  switch (selected.image.format) {
    case ImageFormat::None:
      return false;
    case ImageFormat::Bmp:
      return renderBmpPass(renderer, bounds, selected);
    case ImageFormat::Jpeg:
      return jpegBlock && jpegBlock->render(renderer, bounds.x, bounds.y, ImageBlock::PixelCachePolicy::Stream, error);
  }
  return false;
}

}  // namespace

void AirPageImageRenderer::resetSessionFailures() { ImageBlock::clearSessionRenderFailures(); }

void AirPageImageRenderer::releaseSessionResources() { ImageBlock::releaseRenderCache(); }

void AirPageImageRenderer::cleanScreen(GfxRenderer& renderer) {
  renderer.cancelGrayscale16();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  renderer.requestNextRefresh(HalDisplay::FAST_REFRESH);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

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

AirPageImageRenderer::Result AirPageImageRenderer::render(GfxRenderer& renderer, const Rect& viewport,
                                                          const SelectedImage& selected,
                                                          const bool cleanBeforeDisplay) {
  ImageRenderError error = ImageRenderError::Failed;
  const auto failure = [&error] {
    return error == ImageRenderError::OutOfMemory ? Result::OutOfMemory : Result::Failed;
  };
  const Rect bounds = fittedBounds(viewport, selected.image);
  if (bounds.width <= 0 || bounds.height <= 0) return failure();

  struct RenderCleanup {
    GfxRenderer& renderer;
    ~RenderCleanup() {
      renderer.cancelGrayscale16();
      renderer.setRenderMode(GfxRenderer::BW);
      ImageBlock::releaseRenderCache();
    }
  } cleanup{renderer};

  if (renderer.getGrayscaleLevels() == 16) {
    if (cleanBeforeDisplay) cleanScreen(renderer);
    if (!renderer.beginGrayscale16()) return failure();
    bool decoded = false;
    switch (selected.image.format) {
      case ImageFormat::None:
        return failure();
      case ImageFormat::Bmp: {
        HalFile file;
        if (!Storage.openFileForRead("AIRP", selected.path, file)) return failure();
        Bitmap bitmap(file, false);
        if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() != selected.image.width ||
            bitmap.getHeight() != selected.image.height)
          return failure();
        decoded = renderer.drawBitmapGrayscale16(bitmap, bounds.x, bounds.y, bounds.width, bounds.height);
        break;
      }
      case ImageFormat::Jpeg: {
        RenderConfig config;
        config.x = bounds.x;
        config.y = bounds.y;
        config.maxWidth = bounds.width;
        config.maxHeight = bounds.height;
        config.useExactDimensions = true;
        config.useDithering = false;
        config.output = DecodeOutput::NativeGrayscale16;
        config.error = &error;
        JpegToFramebufferConverter converter;
        decoded = converter.decodeToFramebuffer(selected.path, renderer, config);
        break;
      }
    }
    if (!decoded) return failure();
    return renderer.commitGrayscale16() ? Result::Success : Result::Failed;
  }

  std::optional<ImageBlock> jpegBlock;
  if (selected.image.format == ImageFormat::Jpeg) {
    jpegBlock.emplace(selected.path, "", static_cast<int16_t>(bounds.width), static_cast<int16_t>(bounds.height));
  }
  ImageBlock* jpeg = jpegBlock ? &*jpegBlock : nullptr;

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  if (!renderPass(renderer, bounds, selected, jpeg, &error)) return failure();

  if (cleanBeforeDisplay) {
    cleanScreen(renderer);
  } else {
    renderer.clearScreen();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }

  renderer.setRenderMode(GfxRenderer::BW);
  if (!renderPass(renderer, bounds, selected, jpeg, &error)) return failure();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);

  if (!selected.image.hasGrayscale) return Result::Success;

  const auto abortGrayscale = [&renderer] {
    renderer.setRenderMode(GfxRenderer::BW);
    renderer.clearScreen();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    renderer.cleanupGrayscaleWithFrameBuffer();
  };

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  renderer.clearScreen(0x00);
  if (!renderPass(renderer, bounds, selected, jpeg, &error)) {
    abortGrayscale();
    return failure();
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer.clearScreen(0x00);
  if (!renderPass(renderer, bounds, selected, jpeg, &error)) {
    abortGrayscale();
    return failure();
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.displayGrayBuffer();

  renderer.clearScreen();
  if (!renderPass(renderer, bounds, selected, jpeg, &error)) {
    abortGrayscale();
    return failure();
  }
  renderer.cleanupGrayscaleWithFrameBuffer();
  return Result::Success;
}

}  // namespace airpage
