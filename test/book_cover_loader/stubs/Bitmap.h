#pragma once

#include <HalStorage.h>

#include <cstdint>

// Packed 1bpp header. Must match lib/GfxRenderer/Bitmap.h. The host test
// shadows that header, so sizeof(BmpHeader) in BookCoverLoader uses this copy.
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

enum class BmpReaderError { Ok, NotBMP };

class Bitmap {
 public:
  explicit Bitmap(HalFile& file) : file(file) {}

  BmpReaderError parseHeaders() {
    if (!file.seek(0)) return BmpReaderError::NotBMP;
    const int first = file.read();
    const int second = file.read();
    valid = first == 'B' && second == 'M';
    return valid ? BmpReaderError::Ok : BmpReaderError::NotBMP;
  }

  int getWidth() const { return valid ? 1 : 0; }
  int getHeight() const { return valid ? 1 : 0; }
  // 1x1 1bpp rows are padded to 4 bytes, matching cover_stub::writeBmp (66 bytes).
  int getRowBytes() const { return valid ? 4 : 0; }

 private:
  HalFile& file;
  bool valid = false;
};
