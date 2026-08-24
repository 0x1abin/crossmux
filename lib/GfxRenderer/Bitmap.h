#pragma once

#include <HalStorage.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "BitmapHelpers.h"

#pragma pack(push, 1)
struct BmpHeader {
  struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
  } fileHeader;
  struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
  } infoHeader;
  struct RgbQuad {
    uint8_t rgbBlue;
    uint8_t rgbGreen;
    uint8_t rgbRed;
    uint8_t rgbReserved;
  };
  RgbQuad colors[2];
};
#pragma pack(pop)

enum class BmpReaderError : uint8_t {
  Ok = 0,
  FileInvalid,
  SeekStartFailed,

  NotBMP,
  DIBTooSmall,

  BadPlanes,
  UnsupportedBpp,
  UnsupportedCompression,

  BadDimensions,
  ImageTooLarge,
  PaletteTooLarge,

  SeekPixelDataFailed,
  BufferTooSmall,
  OomRowBuffer,
  ShortReadRow,
};

class Bitmap {
  friend class GfxRenderer;

 public:
  static constexpr uint16_t TRANSPARENT_OVERLAY_MARKER = 0x5843;
  static constexpr uint16_t TRANSPARENT_OVERLAY_VERSION = 1;
  static constexpr uint8_t TRANSPARENT_PALETTE_INDEX = 4;

  static const char* errorToString(BmpReaderError err);

  explicit Bitmap(HalFile& file, bool dithering = false) : file(&file), dithering(dithering) {}
#if defined(BOARD_HAS_PSRAM) || defined(CROSSPOINT_EMULATED)
  // Non-owning memory source. The caller must keep `data` alive for the
  // Bitmap's lifetime; sequential rows are copied into the existing internal
  // draw scratch before pixel processing.
  Bitmap(const uint8_t* data, size_t size, bool dithering = false)
      : memoryData(data), memorySize(size), dithering(dithering) {}
#endif
  ~Bitmap();
  Bitmap(const Bitmap&) = delete;
  Bitmap& operator=(const Bitmap&) = delete;
  Bitmap(Bitmap&&) = delete;
  Bitmap& operator=(Bitmap&&) = delete;
  BmpReaderError parseHeaders();
  BmpReaderError readNextRow(uint8_t* data, uint8_t* rowBuffer, uint8_t* opacityRow = nullptr) const;
  BmpReaderError rewindToData() const;
  int getWidth() const { return width; }
  int getHeight() const { return height; }
  bool isTopDown() const { return topDown; }
  bool hasGreyscale() const { return bpp > 1; }
  int getRowBytes() const { return rowBytes; }
  bool is1Bit() const { return bpp == 1; }
  uint16_t getBpp() const { return bpp; }
  bool hasTransparency() const { return transparentOverlay; }

 private:
  uint16_t readLE16() const;
  uint32_t readLE32() const;
  bool sourceValid() const;
  int sourceRead(void* data, size_t size) const;
  int sourceReadByte() const;
  bool sourceSeek(size_t position) const;
  bool sourceSeekCur(int64_t offset) const;

  HalFile* file = nullptr;
#if defined(BOARD_HAS_PSRAM) || defined(CROSSPOINT_EMULATED)
  const uint8_t* memoryData = nullptr;
  size_t memorySize = 0;
  mutable size_t memoryPosition = 0;
#endif
  bool dithering = false;
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t bfOffBits = 0;
  uint16_t bpp = 0;
  uint32_t colorsUsed = 0;
  bool transparentOverlay = false;
  bool nativePalette = false;  // true if all palette entries map to native gray levels
  int rowBytes = 0;
  uint8_t paletteLum[256] = {};

  // Dithering state (mutable for const methods)
  mutable int16_t* errorCurRow = nullptr;
  mutable int16_t* errorNextRow = nullptr;
  mutable int prevRowY = -1;  // Track row progression for error propagation

  mutable AtkinsonDitherer* atkinsonDitherer = nullptr;
  mutable FloydSteinbergDitherer* fsDitherer = nullptr;
  // One bounded row workspace, reused across repeated grayscale draw passes.
  mutable std::unique_ptr<uint8_t[]> drawScratch;
  mutable size_t drawScratchCapacity = 0;

  bool ensureDrawScratch(size_t bytes) const;
};
