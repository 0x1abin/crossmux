#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>

#include <array>
#include <cassert>
#include <cstdlib>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

#include "Epub/blocks/ImageBlock.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/PngToFramebufferConverter.h"

extern HalDisplay display;
const uint8_t* imageBlockNativePixels();

namespace {
// A recording heap boundary models the device trace: a retained PXC allocation
// leaves less than PNGdec's contiguous requirement. Its real delete restores it.
std::array<void*, 16> pixelCacheAllocations{};
void publishHeapLimit() {
  bool pinned = false;
  for (const auto* allocation : pixelCacheAllocations) pinned |= allocation != nullptr;
  setenv("CROSSPOINT_SIM_MAX_ALLOC_HEAP", pinned ? "25600" : "4000000", 1);
}
}  // namespace

namespace {
void recordAllocation(void* allocation, size_t bytes) {
  if (!allocation || bytes != 128 * 16 / 4) return;
  for (auto& slot : pixelCacheAllocations) {
    if (slot) continue;
    slot = allocation;
    publishHeapLimit();
    return;
  }
  std::abort();
}
void recordFree(void* allocation) {
  if (!allocation) return;
  for (auto& slot : pixelCacheAllocations) {
    if (slot != allocation) continue;
    slot = nullptr;
    publishHeapLimit();
    return;
  }
}
}  // namespace
#if defined(__APPLE__)
extern "C" void* calloc(size_t count, size_t bytes) {
  void* allocation = malloc_zone_calloc(malloc_default_zone(), count, bytes);
  recordAllocation(allocation, count * bytes);
  return allocation;
}
extern "C" void free(void* allocation) {
  if (!allocation) return;
  recordFree(allocation);
  malloc_zone_free(malloc_zone_from_ptr(allocation), allocation);
}
#else
extern "C" void* __real_calloc(size_t, size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_calloc(size_t count, size_t bytes) {
  void* allocation = __real_calloc(count, bytes);
  recordAllocation(allocation, count * bytes);
  return allocation;
}
extern "C" void __wrap_free(void* allocation) {
  recordFree(allocation);
  __real_free(allocation);
}
#endif

// Only PNG participates in this fixture; keep the real decoder and storage,
// without pulling unrelated GIF registration into the native rendering test.
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) {
  static PngToFramebufferConverter decoder;
  return &decoder;
}

int main(int argc, char** argv) {
  assert(argc == 2);
  setenv("CROSSPOINT_SIM_SD", argv[1], 1);
  setenv("CROSSPOINT_SIM_FREE_HEAP", "200000", 1);
  publishHeapLimit();
  GfxRenderer renderer(display);
  renderer.begin();
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  ImageBlock image("/ramp.png", "", 128, 16);
  ImageBlock::clearSessionRenderFailures();
  ImageRenderError error = ImageRenderError::Failed;
  // First build the four-level disk cache, then actually retain its RAM slot.
  assert(image.render(renderer, 0, 0, ImageBlock::PixelCachePolicy::LoadIntoRam, &error));
  assert(image.render(renderer, 0, 0, ImageBlock::PixelCachePolicy::LoadIntoRam, &error));
  assert(renderer.beginGrayscale16());
  // Native must discard the obsolete PXC slot before allocating PNGdec.
  assert(image.render(renderer, 0, 0, ImageBlock::PixelCachePolicy::LoadIntoRam, &error));
  const auto* frame = imageBlockNativePixels();
  for (int level = 0; level < 16; ++level) {
    const int x = level * 8 + 4;
    const size_t pixel = 8 * 128 + x;
    assert(((frame[pixel / 2] >> ((pixel & 1) * 4)) & 15) == level);
  }
  renderer.cancelGrayscale16();
  ImageBlock::releaseRenderCache();

  // A transient decoder heap shortage must not suppress all later attempts.
  assert(renderer.beginGrayscale16());
  setenv("CROSSPOINT_SIM_MAX_ALLOC_HEAP", "25600", 1);
  assert(!image.render(renderer, 0, 0, ImageBlock::PixelCachePolicy::LoadIntoRam, &error));
  assert(error == ImageRenderError::OutOfMemory);
  publishHeapLimit();
  assert(image.render(renderer, 0, 0, ImageBlock::PixelCachePolicy::LoadIntoRam, &error));
  renderer.cancelGrayscale16();
}
