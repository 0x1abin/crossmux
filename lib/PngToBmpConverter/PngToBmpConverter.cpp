#include "PngToBmpConverter.h"

#include <HalDisplay.h>
#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>

#include "Bitmap.h"
#include "BitmapHelpers.h"
#include "PngHelpers.h"

// ============================================================================
// IMAGE PROCESSING OPTIONS - Same as JpegToBmpConverter for consistency
// ============================================================================
constexpr bool USE_8BIT_OUTPUT = false;
constexpr bool USE_ATKINSON = true;
constexpr bool USE_FLOYD_STEINBERG = false;
constexpr bool USE_PRESCALE = true;
// ============================================================================

// BMP writing helpers (same as JpegToBmpConverter)
inline void write16(Print& out, const uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

inline void write32(Print& out, const uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

inline void write32Signed(Print& out, const int32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

// Paeth predictor function per PNG spec
inline uint8_t paethPredictor(uint8_t a, uint8_t b, uint8_t c) {
  int p = static_cast<int>(a) + b - c;
  int pa = p > a ? p - a : a - p;
  int pb = p > b ? p - b : b - p;
  int pc = p > c ? p - c : c - p;
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

namespace {
// PNG constants
uint8_t PNG_SIGNATURE[8] = {137, 80, 78, 71, 13, 10, 26, 10};

// PNG color types
enum PngColorType : uint8_t {
  PNG_COLOR_GRAYSCALE = 0,
  PNG_COLOR_RGB = 2,
  PNG_COLOR_PALETTE = 3,
  PNG_COLOR_GRAYSCALE_ALPHA = 4,
  PNG_COLOR_RGBA = 6,
};

// PNG filter types
enum PngFilter : uint8_t {
  PNG_FILTER_NONE = 0,
  PNG_FILTER_SUB = 1,
  PNG_FILTER_UP = 2,
  PNG_FILTER_AVERAGE = 3,
  PNG_FILTER_PAETH = 4,
};

void yieldDuringDecode(uint8_t& rowsSinceYield) {
  if (++rowsSinceYield < 8) return;
  rowsSinceYield = 0;
  vTaskDelay(1);
}

// Read a big-endian 32-bit value from file
bool readBE32(HalFile& file, uint32_t& value) {
  uint8_t buf[4];
  if (file.read(buf, 4) != 4) return false;
  value = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
          (static_cast<uint32_t>(buf[2]) << 8) | buf[3];
  return true;
}

bool readBE16(HalFile& file, uint16_t& value) {
  uint8_t buf[2];
  if (file.read(buf, 2) != 2) return false;
  value = static_cast<uint16_t>(buf[0]) << 8 | buf[1];
  return true;
}

bool skipChunkDataAndCrc(HalFile& file, const uint32_t length) { return file.seekCur(length) && file.seekCur(4); }

void writeBmpHeader8bit(Print& bmpOut, const int width, const int height) {
  const int bytesPerRow = (width + 3) / 4 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t paletteSize = 256 * 4;
  const uint32_t fileSize = 14 + 40 + paletteSize + imageSize;

  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);
  write32(bmpOut, 14 + 40 + paletteSize);

  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1);
  write16(bmpOut, 8);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 256);
  write32(bmpOut, 256);

  for (int i = 0; i < 256; i++) {
    bmpOut.write(static_cast<uint8_t>(i));
    bmpOut.write(static_cast<uint8_t>(i));
    bmpOut.write(static_cast<uint8_t>(i));
    bmpOut.write(static_cast<uint8_t>(0));
  }
}

void writeBmpHeader1bit(Print& bmpOut, const int width, const int height) {
  const int bytesPerRow = (width + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 62 + imageSize;

  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);
  write32(bmpOut, 62);

  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1);
  write16(bmpOut, 1);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 2);
  write32(bmpOut, 2);

  uint8_t palette[8] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  for (const uint8_t i : palette) {
    bmpOut.write(i);
  }
}

void writeBmpHeader2bit(Print& bmpOut, const int width, const int height) {
  const int bytesPerRow = (width * 2 + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 70 + imageSize;

  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, fileSize);
  write32(bmpOut, 0);
  write32(bmpOut, 70);

  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1);
  write16(bmpOut, 2);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, 4);
  write32(bmpOut, 4);

  uint8_t palette[16] = {0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x00,
                         0xAA, 0xAA, 0xAA, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  for (const uint8_t i : palette) {
    bmpOut.write(i);
  }
}

void writeTransparentBmpHeader4bit(Print& bmpOut, const int width, const int height) {
  const int bytesPerRow = (width + 7) / 8 * 4;
  const int imageSize = bytesPerRow * height;
  constexpr uint32_t paletteSize = 5 * 4;
  const uint32_t pixelOffset = 14 + 40 + paletteSize;

  bmpOut.write('B');
  bmpOut.write('M');
  write32(bmpOut, pixelOffset + imageSize);
  write16(bmpOut, Bitmap::TRANSPARENT_OVERLAY_MARKER);
  write16(bmpOut, Bitmap::TRANSPARENT_OVERLAY_VERSION);
  write32(bmpOut, pixelOffset);

  write32(bmpOut, 40);
  write32Signed(bmpOut, width);
  write32Signed(bmpOut, -height);
  write16(bmpOut, 1);
  write16(bmpOut, 4);
  write32(bmpOut, 0);
  write32(bmpOut, imageSize);
  write32(bmpOut, 2835);
  write32(bmpOut, 2835);
  write32(bmpOut, Bitmap::TRANSPARENT_PALETTE_INDEX + 1);
  write32(bmpOut, Bitmap::TRANSPARENT_PALETTE_INDEX + 1);

  constexpr uint8_t palette[] = {0x00, 0x00, 0x00, 0x00, 0x55, 0x55, 0x55, 0x00, 0xAA, 0xAA,
                                 0xAA, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  bmpOut.write(palette, sizeof(palette));
}
}  // namespace

// Context for streaming PNG decompression
struct PngDecodeContext {
  InflateStream reader;
  HalFile* file;

  // PNG image properties
  uint32_t width;
  uint32_t height;
  uint8_t bitDepth;
  uint8_t colorType;
  uint8_t bytesPerPixel;  // after expanding sub-byte depths
  uint32_t rawRowBytes;   // bytes per raw row (without filter byte)

  // Scanline buffers
  uint8_t* currentRow;   // current defiltered scanline
  uint8_t* previousRow;  // previous defiltered scanline

  // Chunk reading state
  uint32_t chunkBytesRemaining;  // bytes left in current IDAT chunk
  bool idatFinished;             // no more IDAT chunks

  // File read buffer for feeding the inflate stream
  uint8_t readBuf[2048];

  // Palette for indexed color (type 3)
  uint8_t palette[256 * 3];
  uint8_t paletteAlpha[256];
  int paletteSize;
  bool hasTransparentGray;
  bool hasTransparentRgb;
  uint16_t transparentGray;
  uint16_t transparentRed;
  uint16_t transparentGreen;
  uint16_t transparentBlue;
};

// Read the next IDAT chunk header, skipping non-IDAT chunks
// Returns true if an IDAT chunk was found
static bool findNextIdatChunk(PngDecodeContext& ctx) {
  while (true) {
    uint32_t chunkLen;
    if (!readBE32(*ctx.file, chunkLen)) return false;

    uint8_t chunkType[4];
    if (ctx.file->read(chunkType, 4) != 4) return false;

    if (memcmp(chunkType, "IDAT", 4) == 0) {
      ctx.chunkBytesRemaining = chunkLen;
      return true;
    }

    // Skip this chunk's data + 4-byte CRC
    // Use seek to skip efficiently
    if (!skipChunkDataAndCrc(*ctx.file, chunkLen)) return false;

    // If we hit IEND, there are no more chunks
    if (memcmp(chunkType, "IEND", 4) == 0) {
      return false;
    }
  }
}

// Fill callback: reads the next batch of IDAT data from the file
static size_t pngIdatFillCallback(void* vctx, const uint8_t** data) {
  auto* ctx = static_cast<PngDecodeContext*>(vctx);

  if (ctx->idatFinished) return 0;

  // Skip 4-byte CRC and find next IDAT chunk when current chunk is exhausted
  while (ctx->chunkBytesRemaining == 0) {
    if (!ctx->file->seekCur(4)) {  // skip 4-byte CRC of previous IDAT
      ctx->idatFinished = true;
      return 0;
    }
    if (!findNextIdatChunk(*ctx)) {
      ctx->idatFinished = true;
      return 0;
    }
  }

  // Read from current IDAT chunk into the read buffer
  size_t toRead = sizeof(ctx->readBuf);
  if (toRead > ctx->chunkBytesRemaining) toRead = ctx->chunkBytesRemaining;

  const int bytesRead = ctx->file->read(ctx->readBuf, toRead);
  if (bytesRead <= 0) {
    ctx->idatFinished = true;
    return 0;
  }

  ctx->chunkBytesRemaining -= bytesRead;
  *data = ctx->readBuf;
  return static_cast<size_t>(bytesRead);
}

// Decode one scanline: decompress filter byte + raw bytes, then unfilter
static bool decodeScanline(PngDecodeContext& ctx) {
  // Decompress filter byte
  uint8_t filterType;
  if (!ctx.reader.read(&filterType, 1)) return false;

  // Decompress raw row data into currentRow
  if (!ctx.reader.read(ctx.currentRow, ctx.rawRowBytes)) return false;

  // Apply reverse filter
  const int bpp = ctx.bytesPerPixel;

  switch (filterType) {
    case PNG_FILTER_NONE:
      break;

    case PNG_FILTER_SUB:
      for (uint32_t i = bpp; i < ctx.rawRowBytes; i++) {
        ctx.currentRow[i] += ctx.currentRow[i - bpp];
      }
      break;

    case PNG_FILTER_UP:
      for (uint32_t i = 0; i < ctx.rawRowBytes; i++) {
        ctx.currentRow[i] += ctx.previousRow[i];
      }
      break;

    case PNG_FILTER_AVERAGE:
      for (uint32_t i = 0; i < ctx.rawRowBytes; i++) {
        uint8_t a = (i >= static_cast<uint32_t>(bpp)) ? ctx.currentRow[i - bpp] : 0;
        uint8_t b = ctx.previousRow[i];
        ctx.currentRow[i] += (a + b) / 2;
      }
      break;

    case PNG_FILTER_PAETH:
      for (uint32_t i = 0; i < ctx.rawRowBytes; i++) {
        uint8_t a = (i >= static_cast<uint32_t>(bpp)) ? ctx.currentRow[i - bpp] : 0;
        uint8_t b = ctx.previousRow[i];
        uint8_t c = (i >= static_cast<uint32_t>(bpp)) ? ctx.previousRow[i - bpp] : 0;
        ctx.currentRow[i] += paethPredictor(a, b, c);
      }
      break;

    default:
      LOG_ERR("PNG", "Unknown filter type: %d", filterType);
      return false;
  }

  return true;
}

// Batch-convert an entire scanline to grayscale.
// Branches once on colorType/bitDepth, then runs a tight loop for the whole row.
static void convertScanlineToGray(const PngDecodeContext& ctx, uint8_t* grayRow) {
  const uint8_t* src = ctx.currentRow;
  const uint32_t w = ctx.width;

  switch (ctx.colorType) {
    case PNG_COLOR_GRAYSCALE:
      if (ctx.bitDepth == 8) {
        if (!ctx.hasTransparentGray) {
          memcpy(grayRow, src, w);
        } else {
          for (uint32_t x = 0; x < w; x++) grayRow[x] = src[x] == ctx.transparentGray ? 255 : src[x];
        }
      } else if (ctx.bitDepth == 16) {
        for (uint32_t x = 0; x < w; x++) {
          const uint16_t sample = static_cast<uint16_t>(src[x * 2]) << 8 | src[x * 2 + 1];
          grayRow[x] = ctx.hasTransparentGray && sample == ctx.transparentGray ? 255 : src[x * 2];
        }
      } else {
        const int ppb = 8 / ctx.bitDepth;
        const uint8_t mask = (1 << ctx.bitDepth) - 1;
        for (uint32_t x = 0; x < w; x++) {
          int shift = (ppb - 1 - (x % ppb)) * ctx.bitDepth;
          const uint8_t sample = src[x / ppb] >> shift & mask;
          grayRow[x] = ctx.hasTransparentGray && sample == ctx.transparentGray ? 255 : sample * 255 / mask;
        }
      }
      break;

    case PNG_COLOR_RGB:
      if (ctx.bitDepth == 8) {
        // Fast path: most common EPUB cover format
        for (uint32_t x = 0; x < w; x++) {
          const uint8_t* p = src + x * 3;
          grayRow[x] = ctx.hasTransparentRgb && p[0] == ctx.transparentRed && p[1] == ctx.transparentGreen &&
                               p[2] == ctx.transparentBlue
                           ? 255
                           : (p[0] * 25 + p[1] * 50 + p[2] * 25) / 100;
        }
      } else {
        for (uint32_t x = 0; x < w; x++) {
          const uint8_t* p = src + x * 6;
          const uint16_t red = static_cast<uint16_t>(p[0]) << 8 | p[1];
          const uint16_t green = static_cast<uint16_t>(p[2]) << 8 | p[3];
          const uint16_t blue = static_cast<uint16_t>(p[4]) << 8 | p[5];
          grayRow[x] = ctx.hasTransparentRgb && red == ctx.transparentRed && green == ctx.transparentGreen &&
                               blue == ctx.transparentBlue
                           ? 255
                           : (p[0] * 25 + p[2] * 50 + p[4] * 25) / 100;
        }
      }
      break;

    case PNG_COLOR_PALETTE: {
      const int ppb = 8 / ctx.bitDepth;
      const uint8_t mask = (1 << ctx.bitDepth) - 1;
      const uint8_t* pal = ctx.palette;
      const int palSize = ctx.paletteSize;
      for (uint32_t x = 0; x < w; x++) {
        int shift = (ppb - 1 - (x % ppb)) * ctx.bitDepth;
        uint8_t idx = (src[x / ppb] >> shift) & mask;
        if (idx >= palSize) idx = 0;
        const uint8_t gray = (pal[idx * 3] * 25 + pal[idx * 3 + 1] * 50 + pal[idx * 3 + 2] * 25) / 100;
        grayRow[x] = pngHelpers::blendWithWhite(gray, ctx.paletteAlpha[idx]);
      }
      break;
    }

    case PNG_COLOR_GRAYSCALE_ALPHA:
      if (ctx.bitDepth == 8) {
        for (uint32_t x = 0; x < w; x++) {
          grayRow[x] = pngHelpers::blendWithWhite(src[x * 2], src[x * 2 + 1]);
        }
      } else {
        for (uint32_t x = 0; x < w; x++) {
          const uint16_t alpha = static_cast<uint16_t>(src[x * 4 + 2]) << 8 | src[x * 4 + 3];
          grayRow[x] = pngHelpers::blendWithWhite(src[x * 4], static_cast<uint8_t>((alpha + 128) / 257));
        }
      }
      break;

    case PNG_COLOR_RGBA:
      if (ctx.bitDepth == 8) {
        for (uint32_t x = 0; x < w; x++) {
          const uint8_t* p = src + x * 4;
          const uint8_t gray = (p[0] * 25 + p[1] * 50 + p[2] * 25) / 100;
          grayRow[x] = pngHelpers::blendWithWhite(gray, p[3]);
        }
      } else {
        for (uint32_t x = 0; x < w; x++) {
          const uint8_t* p = src + x * 8;
          const uint8_t gray = (p[0] * 25 + p[2] * 50 + p[4] * 25) / 100;
          const uint16_t alpha = static_cast<uint16_t>(p[6]) << 8 | p[7];
          grayRow[x] = pngHelpers::blendWithWhite(gray, static_cast<uint8_t>((alpha + 128) / 257));
        }
      }
      break;

    default:
      memset(grayRow, 128, w);
      break;
  }
}

static void convertScanlineToOpacity(const PngDecodeContext& ctx, uint8_t* opacityRow) {
  const uint8_t* src = ctx.currentRow;
  const uint32_t w = ctx.width;

  switch (ctx.colorType) {
    case PNG_COLOR_GRAYSCALE:
      if (!ctx.hasTransparentGray) {
        memset(opacityRow, 1, w);
      } else if (ctx.bitDepth == 8) {
        for (uint32_t x = 0; x < w; x++) opacityRow[x] = src[x] != ctx.transparentGray;
      } else if (ctx.bitDepth == 16) {
        for (uint32_t x = 0; x < w; x++) {
          const uint16_t sample = static_cast<uint16_t>(src[x * 2]) << 8 | src[x * 2 + 1];
          opacityRow[x] = sample != ctx.transparentGray;
        }
      } else {
        const int ppb = 8 / ctx.bitDepth;
        const uint8_t mask = (1 << ctx.bitDepth) - 1;
        for (uint32_t x = 0; x < w; x++) {
          const int shift = (ppb - 1 - (x % ppb)) * ctx.bitDepth;
          opacityRow[x] = ((src[x / ppb] >> shift) & mask) != ctx.transparentGray;
        }
      }
      break;

    case PNG_COLOR_RGB:
      if (!ctx.hasTransparentRgb) {
        memset(opacityRow, 1, w);
      } else if (ctx.bitDepth == 8) {
        for (uint32_t x = 0; x < w; x++) {
          const uint8_t* p = src + x * 3;
          opacityRow[x] = p[0] != ctx.transparentRed || p[1] != ctx.transparentGreen || p[2] != ctx.transparentBlue;
        }
      } else {
        for (uint32_t x = 0; x < w; x++) {
          const uint8_t* p = src + x * 6;
          const uint16_t red = static_cast<uint16_t>(p[0]) << 8 | p[1];
          const uint16_t green = static_cast<uint16_t>(p[2]) << 8 | p[3];
          const uint16_t blue = static_cast<uint16_t>(p[4]) << 8 | p[5];
          opacityRow[x] = red != ctx.transparentRed || green != ctx.transparentGreen || blue != ctx.transparentBlue;
        }
      }
      break;

    case PNG_COLOR_PALETTE: {
      const int ppb = 8 / ctx.bitDepth;
      const uint8_t mask = (1 << ctx.bitDepth) - 1;
      for (uint32_t x = 0; x < w; x++) {
        const int shift = (ppb - 1 - (x % ppb)) * ctx.bitDepth;
        uint8_t idx = (src[x / ppb] >> shift) & mask;
        if (idx >= ctx.paletteSize) idx = 0;
        opacityRow[x] = ctx.paletteAlpha[idx] != 0;
      }
      break;
    }

    case PNG_COLOR_GRAYSCALE_ALPHA:
      if (ctx.bitDepth == 8) {
        for (uint32_t x = 0; x < w; x++) opacityRow[x] = pngHelpers::isOverlayPixelOpaque(src[x * 2 + 1]);
      } else {
        for (uint32_t x = 0; x < w; x++) {
          const uint16_t alpha = static_cast<uint16_t>(src[x * 4 + 2]) << 8 | src[x * 4 + 3];
          opacityRow[x] = pngHelpers::isOverlayPixelOpaque(alpha);
        }
      }
      break;

    case PNG_COLOR_RGBA:
      if (ctx.bitDepth == 8) {
        for (uint32_t x = 0; x < w; x++) opacityRow[x] = pngHelpers::isOverlayPixelOpaque(src[x * 4 + 3]);
      } else {
        for (uint32_t x = 0; x < w; x++) {
          const uint16_t alpha = static_cast<uint16_t>(src[x * 8 + 6]) << 8 | src[x * 8 + 7];
          opacityRow[x] = pngHelpers::isOverlayPixelOpaque(alpha);
        }
      }
      break;

    default:
      memset(opacityRow, 1, w);
      break;
  }
}

bool PngToBmpConverter::pngFileToBmpStreamInternal(HalFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight,
                                                   bool oneBit, bool crop, bool preserveTransparency) {
  LOG_DBG("PNG", "Converting PNG to %s BMP (target: %dx%d)",
          preserveTransparency ? "transparent 4-bit" : (oneBit ? "1-bit" : "2-bit"), targetWidth, targetHeight);

  // Verify PNG signature
  uint8_t sig[8];
  if (pngFile.read(sig, 8) != 8 || memcmp(sig, PNG_SIGNATURE, 8) != 0) {
    LOG_ERR("PNG", "Invalid PNG signature");
    return false;
  }

  // Read IHDR chunk
  uint32_t ihdrLen;
  if (!readBE32(pngFile, ihdrLen)) return false;
  if (ihdrLen != 13) {
    LOG_ERR("PNG", "Invalid IHDR length: %u", ihdrLen);
    return false;
  }

  uint8_t ihdrType[4];
  if (pngFile.read(ihdrType, 4) != 4 || memcmp(ihdrType, "IHDR", 4) != 0) {
    LOG_ERR("PNG", "Missing IHDR chunk");
    return false;
  }

  uint32_t width, height;
  if (!readBE32(pngFile, width) || !readBE32(pngFile, height)) return false;

  uint8_t ihdrRest[5];
  if (pngFile.read(ihdrRest, 5) != 5) return false;

  uint8_t bitDepth = ihdrRest[0];
  uint8_t colorType = ihdrRest[1];
  uint8_t compression = ihdrRest[2];
  uint8_t filter = ihdrRest[3];
  uint8_t interlace = ihdrRest[4];

  // Skip IHDR CRC
  if (!pngFile.seekCur(4)) return false;

  LOG_DBG("PNG", "Image: %ux%u, depth=%u, color=%u, interlace=%u", width, height, bitDepth, colorType, interlace);

  if (compression != 0 || filter != 0) {
    LOG_ERR("PNG", "Unsupported compression/filter method");
    return false;
  }

  if (interlace != 0) {
    LOG_ERR("PNG", "Interlaced PNGs not supported");
    return false;
  }

  if (!pngHelpers::isSupportedFormat(colorType, bitDepth)) {
    LOG_ERR("PNG", "Unsupported color/depth combination: %u/%u", colorType, bitDepth);
    return false;
  }

  // Safety limits
  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;

  if (width > MAX_IMAGE_WIDTH || height > MAX_IMAGE_HEIGHT || width == 0 || height == 0) {
    LOG_ERR("PNG", "Image too large or zero (%ux%u)", width, height);
    return false;
  }

  // Calculate bytes per pixel and raw row bytes
  uint8_t bytesPerPixel;
  uint32_t rawRowBytes;

  switch (colorType) {
    case PNG_COLOR_GRAYSCALE:
      if (bitDepth == 16) {
        bytesPerPixel = 2;
        rawRowBytes = width * 2;
      } else if (bitDepth == 8) {
        bytesPerPixel = 1;
        rawRowBytes = width;
      } else {
        // Sub-byte: 1, 2, or 4 bits
        bytesPerPixel = 1;
        rawRowBytes = (width * bitDepth + 7) / 8;
      }
      break;
    case PNG_COLOR_RGB:
      bytesPerPixel = (bitDepth == 16) ? 6 : 3;
      rawRowBytes = width * bytesPerPixel;
      break;
    case PNG_COLOR_PALETTE:
      bytesPerPixel = 1;
      rawRowBytes = (width * bitDepth + 7) / 8;
      break;
    case PNG_COLOR_GRAYSCALE_ALPHA:
      bytesPerPixel = (bitDepth == 16) ? 4 : 2;
      rawRowBytes = width * bytesPerPixel;
      break;
    case PNG_COLOR_RGBA:
      bytesPerPixel = (bitDepth == 16) ? 8 : 4;
      rawRowBytes = width * bytesPerPixel;
      break;
    default:
      LOG_ERR("PNG", "Unsupported color type: %d", colorType);
      return false;
  }

  // Validate raw row bytes won't cause memory issues
  if (rawRowBytes > 16384) {
    LOG_ERR("PNG", "Row too large: %u bytes", rawRowBytes);
    return false;
  }

  // This context is over 3KB; keep it off the 8KB render-task stack.
  auto ctx = makeUniqueNoThrow<PngDecodeContext>();
  if (!ctx) {
    LOG_ERR("PNG", "OOM: decode context (%u bytes)", static_cast<unsigned>(sizeof(PngDecodeContext)));
    return false;
  }
  ctx->file = &pngFile;
  ctx->width = width;
  ctx->height = height;
  ctx->bitDepth = bitDepth;
  ctx->colorType = colorType;
  ctx->bytesPerPixel = bytesPerPixel;
  ctx->rawRowBytes = rawRowBytes;
  ctx->paletteSize = 0;
  memset(ctx->paletteAlpha, 0xFF, sizeof(ctx->paletteAlpha));

  // Allocate scanline buffers
  auto currentRow = makeUniqueNoThrow<uint8_t[]>(rawRowBytes);
  auto previousRow = makeUniqueNoThrow<uint8_t[]>(rawRowBytes);
  if (!currentRow || !previousRow) {
    LOG_ERR("PNG", "Failed to allocate scanline buffers (%u bytes each)", rawRowBytes);
    return false;
  }
  memset(previousRow.get(), 0, rawRowBytes);
  ctx->currentRow = currentRow.get();
  ctx->previousRow = previousRow.get();

  // Scan for PLTE chunk (palette) and first IDAT chunk
  // We need to read chunks until we find IDAT, collecting PLTE along the way
  bool foundIdat = false;
  bool foundPalette = false;
  bool foundTransparency = false;
  while (!foundIdat) {
    uint32_t chunkLen;
    if (!readBE32(pngFile, chunkLen)) break;

    uint8_t chunkType[4];
    if (pngFile.read(chunkType, 4) != 4) break;

    if (memcmp(chunkType, "PLTE", 4) == 0) {
      if (foundPalette || colorType == PNG_COLOR_GRAYSCALE || colorType == PNG_COLOR_GRAYSCALE_ALPHA || chunkLen == 0 ||
          chunkLen > sizeof(ctx->palette) || chunkLen % 3 != 0) {
        LOG_ERR("PNG", "Invalid PLTE length: %u", chunkLen);
        return false;
      }
      foundPalette = true;
      const int entries = chunkLen / 3;
      if (colorType == PNG_COLOR_PALETTE && entries > (1 << bitDepth)) {
        LOG_ERR("PNG", "PLTE has too many entries: %d", entries);
        return false;
      }
      ctx->paletteSize = entries;
      if (pngFile.read(ctx->palette, chunkLen) != static_cast<int>(chunkLen) || !pngFile.seekCur(4)) return false;
    } else if (memcmp(chunkType, "tRNS", 4) == 0) {
      if (foundTransparency) {
        LOG_ERR("PNG", "Duplicate tRNS chunk");
        return false;
      }
      foundTransparency = true;
      if (colorType == PNG_COLOR_PALETTE) {
        if (ctx->paletteSize == 0 || chunkLen == 0 || chunkLen > static_cast<uint32_t>(ctx->paletteSize) ||
            pngFile.read(ctx->paletteAlpha, chunkLen) != static_cast<int>(chunkLen)) {
          LOG_ERR("PNG", "Invalid palette tRNS length: %u", chunkLen);
          return false;
        }
      } else if (colorType == PNG_COLOR_GRAYSCALE && chunkLen == 2) {
        if (!readBE16(pngFile, ctx->transparentGray)) break;
        if (bitDepth < 16 && ctx->transparentGray >= (1u << bitDepth)) return false;
        ctx->hasTransparentGray = true;
      } else if (colorType == PNG_COLOR_RGB && chunkLen == 6) {
        if (!readBE16(pngFile, ctx->transparentRed) || !readBE16(pngFile, ctx->transparentGreen) ||
            !readBE16(pngFile, ctx->transparentBlue))
          break;
        if (bitDepth == 8 && (ctx->transparentRed > 255 || ctx->transparentGreen > 255 || ctx->transparentBlue > 255)) {
          return false;
        }
        ctx->hasTransparentRgb = true;
      } else {
        LOG_ERR("PNG", "Invalid tRNS length/type: %u/%u", colorType, chunkLen);
        return false;
      }
      if (!pngFile.seekCur(4)) return false;  // CRC
    } else if (memcmp(chunkType, "IDAT", 4) == 0) {
      if (colorType == PNG_COLOR_PALETTE && ctx->paletteSize == 0) {
        LOG_ERR("PNG", "Palette PNG has no PLTE chunk");
        return false;
      }
      ctx->chunkBytesRemaining = chunkLen;
      foundIdat = true;
    } else if (memcmp(chunkType, "IEND", 4) == 0) {
      break;
    } else {
      // Skip unknown chunk
      if (!skipChunkDataAndCrc(pngFile, chunkLen)) return false;
    }
  }

  if (!foundIdat) {
    LOG_ERR("PNG", "No IDAT chunk found");
    return false;
  }

  // Initialize streaming decompressor with 32KB window for back-reference history
  if (!ctx->reader.init(true)) {
    LOG_ERR("PNG", "Failed to init inflate stream");
    return false;
  }
  ctx->reader.setFill(pngIdatFillCallback, ctx.get());
  // PNG IDAT data is zlib-wrapped (2-byte header + trailing adler32)
  ctx->reader.setZlibWrapped();

  // Calculate output dimensions (same logic as JpegToBmpConverter)
  int outWidth = width;
  int outHeight = height;
  uint32_t scaleX_fp = 65536;
  uint32_t scaleY_fp = 65536;
  bool needsScaling = false;

  if (targetWidth > 0 && targetHeight > 0 &&
      (static_cast<int>(width) != targetWidth || static_cast<int>(height) != targetHeight)) {
    const float scaleToFitWidth = static_cast<float>(targetWidth) / width;
    const float scaleToFitHeight = static_cast<float>(targetHeight) / height;
    float scale = 1.0;
    if (crop) {
      scale = (scaleToFitWidth > scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;
    } else {
      scale = (scaleToFitWidth < scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;
    }

    outWidth = static_cast<int>(width * scale);
    outHeight = static_cast<int>(height * scale);
    if (outWidth < 1) outWidth = 1;
    if (outHeight < 1) outHeight = 1;

    scaleX_fp = (width << 16) / outWidth;
    scaleY_fp = (height << 16) / outHeight;
    needsScaling = true;

    LOG_DBG("PNG", "Scaling %ux%u -> %dx%d (target %dx%d)", width, height, outWidth, outHeight, targetWidth,
            targetHeight);
  }

  // Write BMP header
  int bytesPerRow;
  if (preserveTransparency) {
    writeTransparentBmpHeader4bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 7) / 8 * 4;
  } else if (USE_8BIT_OUTPUT && !oneBit) {
    writeBmpHeader8bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 3) / 4 * 4;
  } else if (oneBit) {
    writeBmpHeader1bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth + 31) / 32 * 4;
  } else {
    writeBmpHeader2bit(bmpOut, outWidth, outHeight);
    bytesPerRow = (outWidth * 2 + 31) / 32 * 4;
  }

  // Allocate BMP row buffer
  auto rowBuffer = makeUniqueNoThrow<uint8_t[]>(bytesPerRow);
  if (!rowBuffer) {
    LOG_ERR("PNG", "Failed to allocate row buffer");
    return false;
  }

  // Create ditherers (same as JpegToBmpConverter)
  std::unique_ptr<AtkinsonDitherer> atkinsonDitherer;
  std::unique_ptr<FloydSteinbergDitherer> fsDitherer;
  std::unique_ptr<Atkinson1BitDitherer> atkinson1BitDitherer;

  if (oneBit) {
    atkinson1BitDitherer = makeUniqueNoThrow<Atkinson1BitDitherer>(outWidth);
    if (!atkinson1BitDitherer || !atkinson1BitDitherer->valid()) return false;
  } else if (!USE_8BIT_OUTPUT) {
    if (USE_ATKINSON) {
      atkinsonDitherer = makeUniqueNoThrow<AtkinsonDitherer>(outWidth);
      if (!atkinsonDitherer || !atkinsonDitherer->valid()) return false;
    } else if (USE_FLOYD_STEINBERG) {
      fsDitherer = makeUniqueNoThrow<FloydSteinbergDitherer>(outWidth);
      if (!fsDitherer || !fsDitherer->valid()) return false;
    }
  }

  // Scaling accumulators
  std::unique_ptr<uint32_t[]> rowAccum;
  std::unique_ptr<uint16_t[]> rowCount;
  std::unique_ptr<uint32_t[]> rowOpaqueCount;
  int currentOutY = 0;
  uint32_t nextOutY_srcStart = 0;

  if (needsScaling) {
    rowAccum = makeUniqueNoThrow<uint32_t[]>(outWidth);
    rowCount = makeUniqueNoThrow<uint16_t[]>(outWidth);
    if (preserveTransparency) rowOpaqueCount = makeUniqueNoThrow<uint32_t[]>(outWidth);
    if (!rowAccum || !rowCount || (preserveTransparency && !rowOpaqueCount)) return false;
    nextOutY_srcStart = scaleY_fp;
  }

  // Allocate grayscale row buffer - batch-convert each scanline to avoid
  // per-pixel getPixelGray() switch overhead in the hot loops
  auto grayRow = makeUniqueNoThrow<uint8_t[]>(width);
  // Source rows can be 2048 pixels wide, so the optional opacity row cannot live on the task stack.
  auto opacityRow = preserveTransparency ? makeUniqueNoThrow<uint8_t[]>(width) : nullptr;
  if (!grayRow || (preserveTransparency && !opacityRow)) {
    LOG_ERR("PNG", "Failed to allocate grayscale/opacity row buffers");
    return false;
  }

  bool success = true;
  uint8_t rowsSinceYield = 0;

  // Process each scanline
  for (uint32_t y = 0; y < height; y++) {
    // Decode one scanline
    if (!decodeScanline(*ctx)) {
      LOG_ERR("PNG", "Failed to decode scanline %u", y);
      success = false;
      break;
    }

    // Batch-convert entire scanline to grayscale (one branch, tight loop)
    convertScanlineToGray(*ctx, grayRow.get());
    if (preserveTransparency) convertScanlineToOpacity(*ctx, opacityRow.get());

    if (!needsScaling) {
      // Direct output (no scaling)
      memset(rowBuffer.get(), 0, bytesPerRow);

      if (!preserveTransparency && USE_8BIT_OUTPUT && !oneBit) {
        for (int x = 0; x < outWidth; x++) {
          rowBuffer[x] = adjustPixel(grayRow[x]);
        }
      } else if (oneBit) {
        for (int x = 0; x < outWidth; x++) {
          const uint8_t bit =
              atkinson1BitDitherer ? atkinson1BitDitherer->processPixel(grayRow[x], x) : quantize1bit(grayRow[x], x, y);
          const int byteIndex = x / 8;
          const int bitOffset = 7 - (x % 8);
          rowBuffer[byteIndex] |= (bit << bitOffset);
        }
        if (atkinson1BitDitherer) atkinson1BitDitherer->nextRow();
      } else {
        for (int x = 0; x < outWidth; x++) {
          const bool opaque = !preserveTransparency || opacityRow[x] != 0;
          const uint8_t gray = adjustPixel(grayRow[x]);
          uint8_t twoBit;
          if (!opaque) {
            twoBit = Bitmap::TRANSPARENT_PALETTE_INDEX;
          } else if (atkinsonDitherer) {
            twoBit = atkinsonDitherer->processPixel(gray, x);
          } else if (fsDitherer) {
            twoBit = fsDitherer->processPixel(gray, x);
          } else {
            twoBit = quantize(gray, x, y);
          }
          if (preserveTransparency) {
            const int byteIndex = x / 2;
            const int bitOffset = (x & 1) ? 0 : 4;
            rowBuffer[byteIndex] |= pngHelpers::overlayPaletteIndex(twoBit, opaque) << bitOffset;
          } else {
            const int byteIndex = (x * 2) / 8;
            const int bitOffset = 6 - ((x * 2) % 8);
            rowBuffer[byteIndex] |= twoBit << bitOffset;
          }
        }
        if (atkinsonDitherer)
          atkinsonDitherer->nextRow();
        else if (fsDitherer)
          fsDitherer->nextRow();
      }
      bmpOut.write(rowBuffer.get(), bytesPerRow);
      yieldDuringDecode(rowsSinceYield);
    } else {
      // Area-averaging scaling (same as JpegToBmpConverter)
      for (int outX = 0; outX < outWidth; outX++) {
        const int srcXStart = (static_cast<uint32_t>(outX) * scaleX_fp) >> 16;
        const int srcXEnd = (static_cast<uint32_t>(outX + 1) * scaleX_fp) >> 16;

        int sum = 0;
        int count = 0;
        uint32_t opaqueCount = 0;
        for (int srcX = srcXStart; srcX < srcXEnd && srcX < static_cast<int>(width); srcX++) {
          sum += grayRow[srcX];
          count++;
          if (preserveTransparency) opaqueCount += opacityRow[srcX] != 0;
        }

        if (count == 0 && srcXStart < static_cast<int>(width)) {
          sum = grayRow[srcXStart];
          count = 1;
          if (preserveTransparency) opaqueCount = opacityRow[srcXStart] != 0;
        }

        rowAccum[outX] += sum;
        rowCount[outX] += count;
        if (preserveTransparency) rowOpaqueCount[outX] += opaqueCount;
      }

      // Check if we've crossed into the next output row(s)
      const uint32_t srcY_fp = static_cast<uint32_t>(y + 1) << 16;

      // Output all rows whose boundaries we've crossed (handles both up and downscaling)
      // For upscaling, one source row may produce multiple output rows
      while (srcY_fp >= nextOutY_srcStart && currentOutY < outHeight) {
        memset(rowBuffer.get(), 0, bytesPerRow);

        if (!preserveTransparency && USE_8BIT_OUTPUT && !oneBit) {
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0;
            rowBuffer[x] = adjustPixel(gray);
          }
        } else if (oneBit) {
          for (int x = 0; x < outWidth; x++) {
            const uint8_t gray = (rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0;
            const uint8_t bit =
                atkinson1BitDitherer ? atkinson1BitDitherer->processPixel(gray, x) : quantize1bit(gray, x, currentOutY);
            const int byteIndex = x / 8;
            const int bitOffset = 7 - (x % 8);
            rowBuffer[byteIndex] |= (bit << bitOffset);
          }
          if (atkinson1BitDitherer) atkinson1BitDitherer->nextRow();
        } else {
          for (int x = 0; x < outWidth; x++) {
            const bool opaque = !preserveTransparency || rowOpaqueCount[x] != 0;
            const uint8_t gray = adjustPixel((rowCount[x] > 0) ? (rowAccum[x] / rowCount[x]) : 0);
            uint8_t twoBit;
            if (!opaque) {
              twoBit = Bitmap::TRANSPARENT_PALETTE_INDEX;
            } else if (atkinsonDitherer) {
              twoBit = atkinsonDitherer->processPixel(gray, x);
            } else if (fsDitherer) {
              twoBit = fsDitherer->processPixel(gray, x);
            } else {
              twoBit = quantize(gray, x, currentOutY);
            }
            if (preserveTransparency) {
              const int byteIndex = x / 2;
              const int bitOffset = (x & 1) ? 0 : 4;
              rowBuffer[byteIndex] |= pngHelpers::overlayPaletteIndex(twoBit, opaque) << bitOffset;
            } else {
              const int byteIndex = (x * 2) / 8;
              const int bitOffset = 6 - ((x * 2) % 8);
              rowBuffer[byteIndex] |= twoBit << bitOffset;
            }
          }
          if (atkinsonDitherer)
            atkinsonDitherer->nextRow();
          else if (fsDitherer)
            fsDitherer->nextRow();
        }

        bmpOut.write(rowBuffer.get(), bytesPerRow);
        currentOutY++;
        yieldDuringDecode(rowsSinceYield);

        nextOutY_srcStart = static_cast<uint32_t>(currentOutY + 1) * scaleY_fp;

        // For upscaling: don't reset accumulators if next output row uses same source data
        // Only reset when we'll move to a new source row
        if (srcY_fp >= nextOutY_srcStart) {
          // More output rows to emit from same source - keep accumulator data
          continue;
        }
        // Moving to next source row - reset accumulators
        memset(rowAccum.get(), 0, outWidth * sizeof(uint32_t));
        memset(rowCount.get(), 0, outWidth * sizeof(uint16_t));
        if (preserveTransparency) memset(rowOpaqueCount.get(), 0, outWidth * sizeof(uint32_t));
      }
    }

    // Swap current/previous row buffers
    uint8_t* temp = ctx->previousRow;
    ctx->previousRow = ctx->currentRow;
    ctx->currentRow = temp;
  }

  if (success) {
    LOG_DBG("PNG", "Successfully converted PNG to BMP");
  }
  return success;
}

bool PngToBmpConverter::pngFileToBmpStream(HalFile& pngFile, Print& bmpOut, bool crop) {
  // Use runtime display dimensions (swapped for portrait cover sizing)
  const int targetWidth = display.getDisplayHeight();
  const int targetHeight = display.getDisplayWidth();
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetWidth, targetHeight, false, crop);
}

bool PngToBmpConverter::pngFileToBmpFileInternal(const char* pngPath, const char* bmpPath, const bool crop,
                                                 const bool preserveTransparency) {
  if (!pngPath || !bmpPath) return false;
  if (Storage.exists(bmpPath)) Storage.remove(bmpPath);

  bool converted = false;
  {
    HalFile input;
    HalFile output;
    converted = Storage.openFileForRead("PNG", pngPath, input) && Storage.openFileForWrite("PNG", bmpPath, output) &&
                pngFileToBmpStreamInternal(input, output, display.getDisplayHeight(), display.getDisplayWidth(), false,
                                           crop, preserveTransparency);
    if (converted) output.flush();
  }
  if (!converted && Storage.exists(bmpPath)) Storage.remove(bmpPath);
  return converted;
}

bool PngToBmpConverter::pngFileToBmpFile(const char* pngPath, const char* bmpPath, const bool crop) {
  return pngFileToBmpFileInternal(pngPath, bmpPath, crop, false);
}

bool PngToBmpConverter::pngFileToTransparentBmpFile(const char* pngPath, const char* bmpPath, const bool crop) {
  return pngFileToBmpFileInternal(pngPath, bmpPath, crop, true);
}

bool PngToBmpConverter::pngFileToBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                   int targetMaxHeight) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetMaxWidth, targetMaxHeight, false);
}

bool PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(HalFile& pngFile, Print& bmpOut, int targetMaxWidth,
                                                       int targetMaxHeight) {
  return pngFileToBmpStreamInternal(pngFile, bmpOut, targetMaxWidth, targetMaxHeight, true, true);
}
