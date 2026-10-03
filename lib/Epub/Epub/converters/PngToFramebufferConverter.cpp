#include "PngToFramebufferConverter.h"

#include <BuildScratch.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <PNGdec.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <new>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"

namespace {

// Context struct passed through PNGdec callbacks to avoid global mutable state.
// The draw callback receives this via pDraw->pUser (set by png.decode()).
// The file I/O callbacks receive the HalFile* via pFile->fHandle (set by pngOpen()).
struct PngContext {
  PNG* decoder{nullptr};
  GfxRenderer* renderer{nullptr};
  const RenderConfig* config{nullptr};
  int screenWidth{0};
  int screenHeight{0};

  // Scaling state
  float scale{1.f};
  int srcWidth{0};
  int srcHeight{0};
  int cropLeft{0};
  int cropTop{0};
  int visibleWidth{0};
  int visibleHeight{0};
  int dstWidth{0};
  int dstHeight{0};
  int lastDstY{-1};  // Track last rendered destination Y to avoid duplicates
  PixelCache cache;
  bool caching{false};

  uint8_t* grayLineBuffer{nullptr};
  uint8_t* alphaLineBuffer{nullptr};
  // Bilinear needs the source row below AND above the destination row, but
  // PNGdec streams one scanline at a time. Holding the previous row lets an
  // output row be emitted once the second of its two source rows has arrived,
  // which is why bilinear emission is one interval behind (see nextDstY).
  uint8_t* prevGrayLine{nullptr};
  bool havePrevRow{false};
  int nextDstY{0};  // next destination row to emit in bilinear mode
  // 16.16 source-column step for the bilinear sampler, computed once per image so
  // the inner loop never divides.
  int32_t stepXFP{0};
  uint32_t lastYieldMs{0};  // throttle state for yieldDuringDecode()
};

// PNGdec streams the compressed data, so on this board every read pays a large fixed
// cost (measured ~9.7 ms per call, ~300 calls for a 600 KB image, i.e. most of the
// decode). Reading the whole compressed file into PSRAM once collapses those calls into
// one. The compressed image is a few hundred KB against megabytes of free PSRAM, and
// when the allocation fails this falls back to plain streamed reads.
constexpr uint32_t PNG_SLURP_MAX_BYTES = 2u * 1024u * 1024u;

struct PngFileHandle {
  HalFile* file = nullptr;
  uint8_t* data = nullptr;  // whole compressed file in PSRAM; null when not slurped
  int32_t size = 0;
  int32_t pos = 0;  // logical position, only meaningful once `data` is set
};

// Instrumentation (temporary): how long the one-shot read took. ~10 ms means the per-read
// cost was fixed overhead and slurping wins; seconds means the SD path is throughput-bound
// and slurping buys nothing. Those two need opposite fixes, so this is measured, not assumed.
static uint32_t g_pngSlurpUs = 0;
static uint32_t g_pngSlurpBytes = 0;

void* pngOpenWithHandle(const char* filename, int32_t* size) {
  // PNGdec owns the callback handle until pngCloseWithHandle() deletes it.
  auto* h = new (std::nothrow) PngFileHandle();
  if (!h) {
    LOG_ERR("PNG", "OOM: PNG handle");
    return nullptr;
  }
  h->file = new (std::nothrow) HalFile();
  if (!h->file) {
    LOG_ERR("PNG", "OOM: PNG file handle (%u bytes)", static_cast<unsigned>(sizeof(HalFile)));
    delete h;
    return nullptr;
  }
  if (!Storage.openFileForRead("PNG", std::string(filename), *h->file)) {
    delete h->file;
    delete h;
    return nullptr;
  }
  h->size = h->file->size();
  *size = h->size;

  if (h->size > 0 && static_cast<uint32_t>(h->size) <= PNG_SLURP_MAX_BYTES) {
    auto* buf = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(h->size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf != nullptr) {
      const uint32_t startedUs = micros();
      const int32_t got = h->file->read(buf, h->size);
      g_pngSlurpUs = micros() - startedUs;
      if (got == h->size) {
        h->data = buf;
        h->pos = 0;
        g_pngSlurpBytes = static_cast<uint32_t>(h->size);
        LOG_INF("PNG", "slurped %ld bytes in %lums", static_cast<long>(h->size),
                static_cast<unsigned long>(g_pngSlurpUs / 1000));
      } else {
        LOG_ERR("PNG", "slurp short read %ld/%ld; streaming instead", static_cast<long>(got),
                static_cast<long>(h->size));
        heap_caps_free(buf);
        h->file->seek(0);
      }
    } else {
      LOG_ERR("PNG", "slurp alloc failed (%ld bytes); streaming instead", static_cast<long>(h->size));
    }
  }
  return h;
}

void pngCloseWithHandle(void* handle) {
  auto* h = reinterpret_cast<PngFileHandle*>(handle);
  if (h) {
    if (h->data) heap_caps_free(h->data);
    if (h->file) {
      h->file->close();
      delete h->file;
    }
    delete h;
  }
}

// Instrumentation (temporary): PNG is streamed, so PNGdec pulls compressed bytes
// through this callback as it inflates. Timing it answers whether the decoder is slow
// itself or is stalling on SD reads -- and the call count says whether the reads are
// large enough to be worth an SD transaction.
static uint32_t g_pngReadUs = 0;
static uint32_t g_pngReadCalls = 0;
static uint32_t g_pngReadBytes = 0;

int32_t pngReadWithHandle(PNGFILE* pFile, uint8_t* pBuf, int32_t len) {
  auto* h = reinterpret_cast<PngFileHandle*>(pFile->fHandle);
  if (!h) return 0;
  ++g_pngReadCalls;
  if (h->data != nullptr) {
    // Served from the PSRAM copy: clamp at the end like a file read would.
    int32_t remaining = h->size - h->pos;
    if (remaining <= 0) return 0;
    if (len > remaining) len = remaining;
    memcpy(pBuf, h->data + h->pos, static_cast<size_t>(len));
    h->pos += len;
    g_pngReadBytes += static_cast<uint32_t>(len);
    return len;
  }
  if (!h->file) return 0;
  const uint32_t startedUs = micros();
  const int32_t got = h->file->read(pBuf, len);
  g_pngReadUs += micros() - startedUs;
  if (got > 0) g_pngReadBytes += static_cast<uint32_t>(got);
  return got;
}

int32_t pngSeekWithHandle(PNGFILE* pFile, int32_t pos) {
  auto* h = reinterpret_cast<PngFileHandle*>(pFile->fHandle);
  if (!h) return -1;
  if (h->data != nullptr) {
    // The PSRAM copy has no file cursor; just move the logical position.
    if (pos < 0 || pos > h->size) return -1;
    h->pos = pos;
    return pos;
  }
  if (!h->file) return -1;
  return h->file->seek(pos);
}

// The PNG decoder (PNGdec) is ~42 KB due to internal zlib decompression buffers.
// We heap-allocate it on demand rather than using a static instance, so this memory
// is only consumed while actually decoding/querying PNG images. This is critical on
// the ESP32-C3 where total RAM is ~320 KB.
constexpr size_t PNG_DECODER_SIZE = sizeof(PNG);
constexpr size_t MIN_FREE_HEAP_FOR_PNG = PNG_DECODER_SIZE + 16 * 1024;

bool hasHeapForPngDecoder(const char* operation) {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t maxAlloc = ESP.getMaxAllocHeap();
  if (freeHeap >= MIN_FREE_HEAP_FOR_PNG && maxAlloc >= PNG_DECODER_SIZE) return true;
  LOG_ERR("PNG", "Not enough heap for PNG %s (free=%u need=%u, maxAlloc=%u need=%u)", operation, freeHeap,
          MIN_FREE_HEAP_FOR_PNG, maxAlloc, PNG_DECODER_SIZE);
  return false;
}

// PNGdec keeps TWO scanlines in its internal ucPixels buffer (current + previous)
// and each scanline includes a leading filter byte.
// Required storage is therefore approximately: 2 * (pitch + 1) + alignment slack.
// If PNG_MAX_BUFFERED_PIXELS is smaller than this requirement for a given image,
// PNGdec can overrun its internal buffer before our draw callback executes.
int bytesPerPixelFromType(int pixelType) {
  switch (pixelType) {
    case PNG_PIXEL_TRUECOLOR:
      return 3;
    case PNG_PIXEL_GRAY_ALPHA:
      return 2;
    case PNG_PIXEL_TRUECOLOR_ALPHA:
      return 4;
    case PNG_PIXEL_GRAYSCALE:
    case PNG_PIXEL_INDEXED:
    default:
      return 1;
  }
}

int packedRowBytes(int srcWidth, int bitsPerSample) { return (srcWidth * bitsPerSample + 7) / 8; }

int requiredPngInternalBufferBytes(int srcWidth, int pixelType, int bitsPerSample) {
  // +1 filter byte per scanline, *2 for current+previous lines, +32 for alignment margin.
  int pitch = srcWidth * bytesPerPixelFromType(pixelType);
  if ((pixelType == PNG_PIXEL_GRAYSCALE || pixelType == PNG_PIXEL_INDEXED) && bitsPerSample < 8) {
    pitch = packedRowBytes(srcWidth, bitsPerSample);
  }
  return ((pitch + 1) * 2) + 32;
}

bool isSupportedBitDepth(int pixelType, int bitsPerSample) {
  if (bitsPerSample == 8) return true;
  if (bitsPerSample != 1 && bitsPerSample != 2 && bitsPerSample != 4) return false;
  return pixelType == PNG_PIXEL_GRAYSCALE || pixelType == PNG_PIXEL_INDEXED;
}

uint8_t readPackedSample(const uint8_t* pixels, int x, int bitsPerSample) {
  if (bitsPerSample == 8) return pixels[x];

  const int bitOffset = x * bitsPerSample;
  const int shift = 8 - bitsPerSample - (bitOffset & 7);
  const uint8_t mask = (1U << bitsPerSample) - 1;
  return (pixels[bitOffset >> 3] >> shift) & mask;
}

uint8_t expandSampleToByte(uint8_t sample, int bitsPerSample) {
  if (bitsPerSample == 8) return sample;
  const uint8_t maxSample = (1U << bitsPerSample) - 1;
  return static_cast<uint8_t>((sample * 255U) / maxSample);
}

uint8_t alphaThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

// Convert an entire source line to grayscale and optionally retain alpha.
// Low-bit-depth grayscale/indexed scanlines are packed most-significant sample first.
// For indexed PNGs with tRNS chunk, alpha values are stored at palette[768] onwards.
// Processing the whole line at once improves cache locality and reduces per-pixel overhead.
void convertLineToGray(const uint8_t* pPixels, uint8_t* grayLine, int width, int pixelType, int bitsPerSample,
                       uint8_t* palette, int hasAlpha, uint32_t transparentColor, uint8_t* alphaLine) {
  const auto store = [grayLine, alphaLine](const int x, const uint8_t gray, const uint8_t alpha) {
    grayLine[x] = alphaLine ? gray : static_cast<uint8_t>((gray * alpha + 255u * (255u - alpha)) / 255u);
    if (alphaLine) alphaLine[x] = alpha;
  };

  switch (pixelType) {
    case PNG_PIXEL_GRAYSCALE:
      if (bitsPerSample == 8 && !hasAlpha && !alphaLine) {
        memcpy(grayLine, pPixels, width);
      } else {
        for (int x = 0; x < width; x++) {
          const uint8_t sample = readPackedSample(pPixels, x, bitsPerSample);
          const uint8_t gray = expandSampleToByte(sample, bitsPerSample);
          store(x, gray, hasAlpha && sample == static_cast<uint8_t>(transparentColor) ? 0 : 255);
        }
      }
      break;

    case PNG_PIXEL_TRUECOLOR:
      for (int x = 0; x < width; x++) {
        const uint8_t* p = &pPixels[x * 3];
        const uint8_t gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
        const uint32_t color = (static_cast<uint32_t>(p[0]) << 16) | (static_cast<uint32_t>(p[1]) << 8) | p[2];
        store(x, gray, hasAlpha && color == transparentColor ? 0 : 255);
      }
      break;

    case PNG_PIXEL_INDEXED:
      if (palette) {
        for (int x = 0; x < width; x++) {
          const uint8_t idx = readPackedSample(pPixels, x, bitsPerSample);
          const uint8_t* p = &palette[idx * 3];
          const uint8_t gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
          store(x, gray, hasAlpha ? palette[768 + idx] : 255);
        }
      } else {
        for (int x = 0; x < width; x++) {
          store(x, expandSampleToByte(readPackedSample(pPixels, x, bitsPerSample), bitsPerSample), 255);
        }
      }
      break;

    case PNG_PIXEL_GRAY_ALPHA:
      for (int x = 0; x < width; x++) {
        store(x, pPixels[x * 2], pPixels[x * 2 + 1]);
      }
      break;

    case PNG_PIXEL_TRUECOLOR_ALPHA:
      for (int x = 0; x < width; x++) {
        const uint8_t* p = &pPixels[x * 4];
        const uint8_t gray = static_cast<uint8_t>((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
        store(x, gray, p[3]);
      }
      break;

    default:
      memset(grayLine, 128, width);
      if (alphaLine) memset(alphaLine, 255, width);
      break;
  }
}

// 16.16 fixed point for the bilinear weights (no FPU assumptions).
constexpr int kBilinearOne = 1 << 16;

// Bilinear emission for one destination row. `rowTop`/`rowBot` are the two
// source rows bracketing it (the same pointer when the position lands exactly on
// a row) and `fyFP` is the weight towards `rowBot`. Horizontal sampling blends
// the two neighbouring source columns with the same fixed-point scheme, so a
// scaled image gets a continuous ramp instead of nearest-neighbour stair steps.
void emitBilinearRow(PngContext& ctx, const int dstY, const uint8_t* rowTop, const uint8_t* rowBot, const int fyFP,
                     DirectPixelWriter& pw, const bool writeFramebuffer) {
  const int outY = ctx.config->y + dstY;
  if (outY >= ctx.screenHeight) return;
  if (writeFramebuffer) pw.beginRow(outY);

  // The cache streams to disk one row at a time; a flush failure stops caching
  // for the rest of the decode so a partial band is never written past.
  bool caching = ctx.caching;
  DirectCacheWriter cw;
  if (caching) {
    if (!ctx.cache.advanceTo(dstY)) {
      caching = false;
      ctx.caching = false;
    } else {
      cw.init(ctx.cache.buffer, ctx.cache.bytesPerRow, ctx.cache.bandRows, ctx.cache.originX);
      cw.beginRow(outY, ctx.config->y + ctx.cache.bandStart);
    }
  }

  const bool useDithering = ctx.config->useDithering;
  const int lastX = ctx.visibleWidth > 0 ? ctx.visibleWidth - 1 : 0;
  // Source column advances by a fixed 16.16 step, so no division is needed per
  // pixel (a 64-bit divide per pixel cost more than the interpolation itself).
  const int32_t stepXFP = ctx.stepXFP;
  int32_t srcXFP = 0;

  for (int dstX = 0; dstX < ctx.dstWidth; dstX++) {
    const int outX = ctx.config->x + dstX;
    int sx = srcXFP >> 16;
    int fxFP = srcXFP & 0xFFFF;
    srcXFP += stepXFP;
    if (sx >= lastX) {
      sx = lastX;
      fxFP = 0;
    }
    const int sx1 = (sx + 1 <= lastX) ? sx + 1 : lastX;
    const int x0 = ctx.cropLeft + sx;
    const int x1 = ctx.cropLeft + sx1;

    const int top = (static_cast<int>(rowTop[x0]) * (kBilinearOne - fxFP) + static_cast<int>(rowTop[x1]) * fxFP) >> 16;
    int gray = top;
    if (fyFP != 0) {
      const int bot =
          (static_cast<int>(rowBot[x0]) * (kBilinearOne - fxFP) + static_cast<int>(rowBot[x1]) * fxFP) >> 16;
      gray = (top * (kBilinearOne - fyFP) + bot * fyFP) >> 16;
    }
    if (gray < 0) gray = 0;
    if (gray > 255) gray = 255;
    const uint8_t sample = static_cast<uint8_t>(gray);

    if (outX >= ctx.screenWidth) continue;
    const uint8_t alpha = ctx.alphaLineBuffer ? ctx.alphaLineBuffer[x0] : 255;
    if (alpha < 8 || alpha <= alphaThreshold4x4(outX, outY)) continue;
    uint8_t ditheredGray;
    if (useDithering) {
      ditheredGray = applyBayerDither4Level(sample, outX, outY);
    } else {
      const int level = sample / 85;
      ditheredGray = static_cast<uint8_t>(level > 3 ? 3 : level);
    }
    if (writeFramebuffer) pw.writePixel(outX, ditheredGray, ctx.alphaLineBuffer != nullptr);
    if (caching) cw.writePixel(outX, ditheredGray);
  }
}

// Instrumentation (temporary): PNGdec inflates and unfilters a row before handing it
// to the callback, so timing the callback separates our per-pixel conversion from the
// decoder's own work. The wrapper keeps the callback's name, so the decode call site
// does not change.
static uint32_t g_pngCallbackUs = 0;
static uint32_t g_pngCallbackRows = 0;

static int pngDrawCallbackBody(PNGDRAW* pDraw);

int pngDrawCallback(PNGDRAW* pDraw) {
  const uint32_t startedUs = micros();
  const int rc = pngDrawCallbackBody(pDraw);
  g_pngCallbackUs += micros() - startedUs;
  ++g_pngCallbackRows;
  return rc;
}

static int pngDrawCallbackBody(PNGDRAW* pDraw) {
  PngContext* ctx = reinterpret_cast<PngContext*>(pDraw->pUser);
  if (!ctx || !ctx->config || !ctx->renderer || !ctx->grayLineBuffer) return 0;

  ImageToFramebufferDecoder::yieldDuringDecode(ctx->lastYieldMs);

  int srcY = pDraw->y;
  int srcWidth = ctx->srcWidth;
  if (srcY < ctx->cropTop || srcY >= ctx->cropTop + ctx->visibleHeight) return 1;
  const int visibleSrcY = srcY - ctx->cropTop;

  // Bilinear runs on its own row budget: it emits one source interval behind so
  // an output row can be blended against the two source rows that bracket it.
  // That is why it bypasses the nearest-neighbour row mapping (and its early
  // return) entirely.
  //
  // An unscaled image deliberately stays on the nearest path: at 1:1 the blend
  // weights are degenerate, so bilinear would produce identical pixels for
  // several times the work — and the reader renders a page many times.
  const bool bilinearScale =
      ctx->config->bilinearScaling && (ctx->dstWidth != ctx->visibleWidth || ctx->dstHeight != ctx->visibleHeight);
  if (bilinearScale) {
    // PNGdec parses tRNS while decoding, after open() returns, so query the
    // transparent colour here rather than caching its pre-decode value.
    convertLineToGray(pDraw->pPixels, ctx->grayLineBuffer, srcWidth, pDraw->iPixelType, pDraw->iBpp, pDraw->pPalette,
                      pDraw->iHasAlpha, ctx->decoder ? ctx->decoder->getTransparentColor() : 0, ctx->alphaLineBuffer);

    if (ctx->havePrevRow && ctx->prevGrayLine) {
      // Every destination row whose source position falls in the interval
      // [visibleSrcY - 1, visibleSrcY) is now fully known.
      const int64_t bound =
          (static_cast<int64_t>(visibleSrcY) * ctx->dstHeight + ctx->visibleHeight - 1) / ctx->visibleHeight;
      int last = static_cast<int>(bound);
      if (last > ctx->dstHeight) last = ctx->dstHeight;
      if (last > ctx->nextDstY) {
        const bool writeFramebuffer = ctx->config->output == DecodeOutput::FrameBufferAndCache;
        DirectPixelWriter pw;
        if (writeFramebuffer) pw.init(*ctx->renderer);
        for (int dstY = ctx->nextDstY; dstY < last; dstY++) {
          const int64_t srcPos = (static_cast<int64_t>(dstY) * ctx->visibleHeight << 16) / ctx->dstHeight;
          emitBilinearRow(*ctx, dstY, ctx->prevGrayLine, ctx->grayLineBuffer, static_cast<int>(srcPos & 0xFFFF), pw,
                          writeFramebuffer);
        }
        ctx->nextDstY = last;
      }
    }

    if (ctx->prevGrayLine) {
      memcpy(ctx->prevGrayLine, ctx->grayLineBuffer, static_cast<size_t>(srcWidth));
    }
    ctx->havePrevRow = true;
    return 1;
  }

  // Map source rows with the exact output-height ratio. During downscaling,
  // multiple source rows can select the same output row; during upscaling, one
  // source row must be repeated across every output row in its range. Emitting
  // only the first row of an upscale leaves zero-filled (black) gaps in the
  // streamed pixel cache.
  int firstDstY = (visibleSrcY * ctx->dstHeight) / ctx->visibleHeight;
  int endDstY = firstDstY + 1;
  if (ctx->dstHeight > ctx->visibleHeight) {
    endDstY = ((visibleSrcY + 1) * ctx->dstHeight) / ctx->visibleHeight;
  }

  if (firstDstY <= ctx->lastDstY) firstDstY = ctx->lastDstY + 1;
  if (firstDstY >= endDstY || firstDstY >= ctx->dstHeight) return 1;
  if (endDstY > ctx->dstHeight) endDstY = ctx->dstHeight;

  // PNGdec parses tRNS while decoding, after open() returns, so query the
  // transparent color here rather than caching its pre-decode value.
  const uint32_t transparentColor = ctx->decoder ? ctx->decoder->getTransparentColor() : 0;
  convertLineToGray(pDraw->pPixels, ctx->grayLineBuffer, srcWidth, pDraw->iPixelType, pDraw->iBpp, pDraw->pPalette,
                    pDraw->iHasAlpha, transparentColor, ctx->alphaLineBuffer);

  // Render scaled rows using Bresenham-style integer stepping (no floating-point division)
  int dstWidth = ctx->dstWidth;
  int outXBase = ctx->config->x;
  int screenWidth = ctx->screenWidth;
  bool useDithering = ctx->config->useDithering;
  const bool writeFramebuffer = ctx->config->output == DecodeOutput::FrameBufferAndCache;

  // Pre-compute orientation and render-mode state once per callback.
  DirectPixelWriter pw;
  if (writeFramebuffer) pw.init(*ctx->renderer);

  for (int dstY = firstDstY; dstY < endDstY; dstY++) {
    ctx->lastDstY = dstY;
    int outY = ctx->config->y + dstY;
    if (outY >= ctx->screenHeight) continue;

    if (writeFramebuffer) pw.beginRow(outY);

    // The cache streams to disk one row at a time. Flushing rows below this one
    // (PNGdec delivers scanlines top to bottom) repositions the single-row band.
    // A flush failure stops caching for the rest of the decode so we never write
    // past the band buffer; finalize() then drops the partial file.
    bool caching = ctx->caching;
    DirectCacheWriter cw;
    if (caching) {
      if (!ctx->cache.advanceTo(dstY)) {
        caching = false;
        ctx->caching = false;
      } else {
        cw.init(ctx->cache.buffer, ctx->cache.bytesPerRow, ctx->cache.bandRows, ctx->cache.originX);
        cw.beginRow(outY, ctx->config->y + ctx->cache.bandStart);
      }
    }

    int srcX = ctx->cropLeft;
    int error = 0;

    for (int dstX = 0; dstX < dstWidth; dstX++) {
      int outX = outXBase + dstX;
      if (outX < screenWidth) {
        const uint8_t alpha = ctx->alphaLineBuffer ? ctx->alphaLineBuffer[srcX] : 255;
        if (alpha >= 8 && alpha > alphaThreshold4x4(outX, outY)) {
          uint8_t gray = ctx->grayLineBuffer[srcX];

          uint8_t ditheredGray;
          if (useDithering) {
            ditheredGray = applyBayerDither4Level(gray, outX, outY);
          } else {
            ditheredGray = gray >> 6;
          }
          if (writeFramebuffer) pw.writePixel(outX, ditheredGray, ctx->alphaLineBuffer != nullptr);
          if (caching) cw.writePixel(outX, ditheredGray);
        }
      }

      // Bresenham-style stepping: advance srcX based on ratio visibleWidth/dstWidth
      error += ctx->visibleWidth;
      while (error >= dstWidth) {
        error -= dstWidth;
        srcX++;
      }
    }
  }

  return 1;
}

}  // namespace

bool PngToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  if (!hasHeapForPngDecoder("dimensions")) return false;

  std::unique_ptr<PNG> png(new (std::nothrow) PNG());
  if (!png) {
    LOG_ERR("PNG", "Failed to allocate PNG decoder for dimensions");
    return false;
  }

  int rc = png->open(imagePath.c_str(), pngOpenWithHandle, pngCloseWithHandle, pngReadWithHandle, pngSeekWithHandle,
                     nullptr);
  const ScopedCleanup cleanup{[&png]() { png->close(); }};

  if (rc != 0) {
    LOG_ERR("PNG", "Failed to open PNG for dimensions: %d", rc);
    return false;
  }

  return validateAndStoreDimensions(png->getWidth(), png->getHeight(), out, "PNG");
}

bool PngToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("PNG", "Decoding PNG: %s", imagePath.c_str());

  const bool cacheOnly = config.output == DecodeOutput::CacheOnly;
  if (cacheOnly && config.cachePath.empty()) {
    LOG_ERR("PNG", "Cache-only decode requires a cache path");
    return false;
  }

  uint8_t* decoderScratch = cacheOnly ? buildscratch::claim(PNG_DECODER_SIZE) : nullptr;
  if (!decoderScratch && !hasHeapForPngDecoder("decode")) return false;

  std::unique_ptr<PNG> heapPng;
  PNG* png = nullptr;
  if (decoderScratch) {
    LOG_DBG("PNG", "Using framebuffer scratch for cache-only decode");
    png = ::new (static_cast<void*>(decoderScratch)) PNG();
  } else {
    heapPng = makeUniqueNoThrow<PNG>();
    png = heapPng.get();
  }
  if (!png) {
    LOG_ERR("PNG", "Failed to allocate PNG decoder");
    return false;
  }
  const ScopedCleanup releaseDecoderScratch{[png, decoderScratch]() {
    if (!decoderScratch) return;
    png->~PNG();
    buildscratch::release(decoderScratch);
  }};
  const ScopedCleanup closePng{[png]() { png->close(); }};

  PngContext ctx;
  ctx.decoder = png;
  ctx.renderer = &renderer;
  ctx.config = &config;
  ctx.screenWidth = renderer.getScreenWidth();
  ctx.screenHeight = renderer.getScreenHeight();

  int rc = png->open(imagePath.c_str(), pngOpenWithHandle, pngCloseWithHandle, pngReadWithHandle, pngSeekWithHandle,
                     pngDrawCallback);
  if (rc != PNG_SUCCESS) {
    LOG_ERR("PNG", "Failed to open PNG: %d", rc);
    return false;
  }

  ImageDimensions sourceDimensions;
  if (!validateAndStoreDimensions(png->getWidth(), png->getHeight(), sourceDimensions, "PNG")) return false;

  // Calculate output dimensions
  ctx.srcWidth = sourceDimensions.width;
  ctx.srcHeight = sourceDimensions.height;
  ctx.cropLeft = static_cast<int>(ctx.srcWidth * std::clamp(config.sourceCropX, 0.0f, 0.99f) / 2.0f);
  ctx.cropTop = static_cast<int>(ctx.srcHeight * std::clamp(config.sourceCropY, 0.0f, 0.99f) / 2.0f);
  ctx.visibleWidth = ctx.srcWidth - 2 * ctx.cropLeft;
  ctx.visibleHeight = ctx.srcHeight - 2 * ctx.cropTop;
  if (ctx.visibleWidth <= 0 || ctx.visibleHeight <= 0) {
    LOG_ERR("PNG", "PNG crop leaves no visible pixels");
    return false;
  }

  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    // Use exact dimensions as specified (avoids rounding mismatches with pre-calculated sizes)
    ctx.dstWidth = config.maxWidth;
    ctx.dstHeight = config.maxHeight;
    ctx.scale = (float)ctx.dstWidth / ctx.visibleWidth;
  } else {
    // Calculate scale factor to fit within maxWidth/maxHeight
    float scaleX = (float)config.maxWidth / ctx.visibleWidth;
    float scaleY = (float)config.maxHeight / ctx.visibleHeight;
    ctx.scale = (scaleX < scaleY) ? scaleX : scaleY;
    if (ctx.scale > 1.0f) ctx.scale = 1.0f;  // Don't upscale

    ctx.dstWidth = (int)(ctx.visibleWidth * ctx.scale);
    ctx.dstHeight = (int)(ctx.visibleHeight * ctx.scale);
  }
  ctx.lastDstY = -1;  // Reset row tracking
  // 16.16 source-column step for the bilinear sampler. visibleWidth fits well
  // inside int32 after the shift (<= 32767 << 16), and the sampler clamps at the
  // last column anyway.
  ctx.stepXFP =
      ctx.dstWidth > 0 ? static_cast<int32_t>((static_cast<int64_t>(ctx.visibleWidth) << 16) / ctx.dstWidth) : 0;

  const int pixelType = png->getPixelType();
  const int bitsPerSample = png->getBpp();
  LOG_DBG("PNG", "PNG %dx%d (visible %dx%d) -> %dx%d (scale %.2f), type: %d, bpp: %d", ctx.srcWidth, ctx.srcHeight,
          ctx.visibleWidth, ctx.visibleHeight, ctx.dstWidth, ctx.dstHeight, ctx.scale, pixelType, bitsPerSample);

  const int requiredInternal = requiredPngInternalBufferBytes(ctx.srcWidth, pixelType, bitsPerSample);
  if (requiredInternal > PNG_MAX_BUFFERED_PIXELS) {
    LOG_ERR(
        "PNG",
        "PNG row buffer too small: need %d bytes for width=%d type=%d bpp=%d, configured PNG_MAX_BUFFERED_PIXELS=%d",
        requiredInternal, ctx.srcWidth, pixelType, bitsPerSample, PNG_MAX_BUFFERED_PIXELS);
    LOG_ERR("PNG", "Aborting decode to avoid PNGdec internal buffer overflow");
    return false;
  }

  if (!isSupportedBitDepth(pixelType, bitsPerSample)) {
    warnUnsupportedFeature(
        "bit depth (" + std::to_string(bitsPerSample) + "bpp) for pixel type " + std::to_string(pixelType), imagePath);
    return false;
  }

  // The converter expands each source row to 8-bit grayscale before dithering,
  // so this scratch buffer is sized by source pixels even when PNGdec reads a
  // packed 1/2/4-bit row internally.
  constexpr size_t MAX_GRAY_LINE_BUFFER_BYTES = PNG_MAX_BUFFERED_PIXELS / 2;
  const size_t grayBufSize = static_cast<size_t>(ctx.srcWidth);
  if (grayBufSize > MAX_GRAY_LINE_BUFFER_BYTES) {
    LOG_ERR("PNG", "Expanded gray row too wide: need %u bytes for width=%d, max=%u", static_cast<unsigned>(grayBufSize),
            ctx.srcWidth, static_cast<unsigned>(MAX_GRAY_LINE_BUFFER_BYTES));
    return false;
  }

  // tRNS chunks are parsed during decode(), so hasAlpha() is not reliable yet
  // for color-key and indexed transparency. Reserve the alpha line whenever
  // the caller requests preservation; opaque pixels simply store alpha 255.
  const bool retainAlpha = config.preserveAlpha;
  const bool bilinearMode = config.bilinearScaling;
  // Bilinear holds one extra source row (the previous scanline) so an output row
  // can be blended vertically; nearest needs only the current one.
  const size_t alphaAndCurrent = grayBufSize * (retainAlpha ? 2u : 1u);
  const size_t lineBufferBytes = alphaAndCurrent + (bilinearMode ? grayBufSize : 0u);
  auto lineBuffers = makeUniqueNoThrow<uint8_t[]>(lineBufferBytes);
  if (!lineBuffers) {
    LOG_ERR("PNG", "Failed to allocate PNG line buffers (%u bytes)", static_cast<unsigned>(lineBufferBytes));
    return false;
  }
  ctx.grayLineBuffer = lineBuffers.get();
  ctx.alphaLineBuffer = retainAlpha ? ctx.grayLineBuffer + grayBufSize : nullptr;
  ctx.prevGrayLine = bilinearMode ? ctx.grayLineBuffer + alphaAndCurrent : nullptr;
  ctx.havePrevRow = false;
  ctx.nextDstY = 0;

  // Stream the pixel cache to disk. PNGdec delivers source scanlines top to
  // bottom and we emit at most one (downscaled) output row per callback, so the
  // band only needs a single row. Streaming keeps the working set tiny, so
  // unlike the old full-image buffer it neither competes with the ~44KB decoder
  // nor forces larger images to skip caching - which previously meant a full
  // re-decode on every one of an image page's ~14 render passes.
  ctx.caching = !config.preserveAlpha && !config.cachePath.empty();
  if (ctx.caching) {
    if (!ctx.cache.begin(config.cachePath, ctx.dstWidth, ctx.dstHeight, config.x, config.y, 1)) {
      LOG_ERR("PNG", "Failed to start cache stream%s", cacheOnly ? "" : ", continuing without caching");
      ctx.caching = false;
      if (cacheOnly) return false;
    }
  }

  unsigned long decodeStart = millis();
  ctx.lastYieldMs = decodeStart;
  g_pngCallbackUs = 0;
  g_pngCallbackRows = 0;
  g_pngReadUs = 0;
  g_pngReadCalls = 0;
  g_pngReadBytes = 0;
  rc = png->decode(&ctx, 0);
  unsigned long decodeTime = millis() - decodeStart;

  // Instrumentation (temporary): `total` is the whole decode, `callback` is our own
  // per-pixel conversion, and `read` is time blocked inside the SD read the decoder
  // pulls its compressed bytes through. `decoder` is what is left, i.e. inflate and
  // unfilter proper. Each bucket needs a different fix, so they are kept apart.
  {
    const unsigned long callbackMs = g_pngCallbackUs / 1000;
    const unsigned long readMs = g_pngReadUs / 1000;
    const unsigned long accounted = callbackMs + readMs;
    LOG_INF("PNG",
            "decode split: total=%lums callback=%lums read=%lums(%lu calls, %lu bytes) decoder=%lums rows=%lu "
            "slurp=%lums/%lu bytes",
            decodeTime, callbackMs, readMs, static_cast<unsigned long>(g_pngReadCalls),
            static_cast<unsigned long>(g_pngReadBytes), decodeTime > accounted ? decodeTime - accounted : 0UL,
            static_cast<unsigned long>(g_pngCallbackRows), static_cast<unsigned long>(g_pngSlurpUs / 1000),
            static_cast<unsigned long>(g_pngSlurpBytes));
  }

  // Bilinear emits one source interval behind, so the last destination rows are
  // still outstanding: they sit on the final source row and have no row below,
  // so flush them against that row alone (vertical weight 0 keeps the horizontal
  // interpolation).
  if (rc == PNG_SUCCESS && config.bilinearScaling && ctx.havePrevRow && ctx.prevGrayLine) {
    const bool writeFramebuffer = config.output == DecodeOutput::FrameBufferAndCache;
    DirectPixelWriter pw;
    if (writeFramebuffer) pw.init(renderer);
    for (int dstY = ctx.nextDstY; dstY < ctx.dstHeight; dstY++) {
      emitBilinearRow(ctx, dstY, ctx.prevGrayLine, ctx.prevGrayLine, 0, pw, writeFramebuffer);
    }
    ctx.nextDstY = ctx.dstHeight;
  }

  ctx.grayLineBuffer = nullptr;
  ctx.alphaLineBuffer = nullptr;

  if (rc != PNG_SUCCESS) {
    LOG_ERR("PNG", "Decode failed: %d", rc);
    if (ctx.caching || cacheOnly) ctx.cache.abort();
    return false;
  }

  LOG_DBG("PNG", "PNG decoding complete - render time: %lu ms", decodeTime);

  // Finalize the streamed cache (caching may have been cleared on a flush error).
  if (cacheOnly) {
    if (!ctx.caching) {
      ctx.cache.abort();
      return false;
    }
    return ctx.cache.finalize();
  }
  if (ctx.caching) ctx.cache.finalize();

  return true;
}

bool PngToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasPngExtension(extension);
}
