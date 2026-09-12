#pragma once
#include <Print.h>

#include "ImageToFramebufferDecoder.h"

// Streaming JPEG/PNG header parser: finds image dimensions from the first few
// KB of a compressed stream without inflating the whole image. Feed bytes via
// the Print interface (e.g. Epub::readItemContentsToStream with
// allowEarlyStop=true); write() returns short once the dimensions are known or
// the stream is known to be unusable, which the zip layer treats as a polite
// early stop rather than an error.
//
// JPEG: walks marker segments (skipping EXIF/APPn of any size statefully, so
// nothing is buffered) until a SOFn frame header yields the dimensions.
// PNG: reads the IHDR fields at their fixed offsets (bytes 16..23).
class ImageDimsProbe : public Print {
 public:
  // Format identified from the stream's leading bytes. The extension in an
  // EPUB's href is only a hint — manifests legitimately point at images named
  // without one — so callers that must choose a decoder should trust this.
  enum class Format : uint8_t { Unknown, Jpeg, Png };

  size_t write(uint8_t b) override;
  size_t write(const uint8_t* data, size_t len) override;

  // True only when a valid header was found; fills `out`.
  bool getDimensions(ImageDimensions& out) const;

  // Format validated so far. Unknown until a signature has been checked in full
  // (a bare 0xFF or 0x89 identifies nothing), and still Unknown after a failed
  // parse.
  Format detectedFormat() const { return format; }
  // Canonical extension for a format, or nullptr when it is not decodable.
  static const char* extensionForFormat(const Format value) {
    switch (value) {
      case Format::Jpeg:
        return ".jpg";
      case Format::Png:
        return ".png";
      case Format::Unknown:
        break;
    }
    return nullptr;
  }

 private:
  bool feed(uint8_t b);  // returns false once parsing is finished (found or failed)

  enum class State : uint8_t {
    Sniff,       // first byte selects the parser; the format follows on validation
    PngHeader,   // PNG signature + IHDR at fixed offsets
    JpegSoi,     // second SOI byte (0xD8)
    JpegFf,      // expect a 0xFF marker prefix
    JpegMarker,  // marker type byte (0xFF padding allowed)
    JpegLenHi,   // segment length, high byte
    JpegLenLo,   // segment length, low byte
    JpegSkip,    // skipping a non-SOF segment body
    JpegSof,     // collecting the 5 SOF bytes: precision, height(2), width(2)
    Done,
    Failed,
  };
  State state = State::Sniff;
  Format format = Format::Unknown;  // stamped once a signature has been validated
  uint32_t pos = 0;                 // absolute stream offset (PNG fixed-offset parsing)
  uint32_t skipLeft = 0;            // remaining segment bytes to skip
  uint16_t segLen = 0;
  bool sofPending = false;  // current segment is a SOF frame header
  uint8_t sofBuf[5] = {0};
  uint8_t sofFill = 0;
  // 32-bit: PNG IHDR width/height are 4-byte fields. Accumulating them in a
  // uint16_t silently truncates an oversized image to a plausible small value
  // that passes the INT16_MAX sanity check in getDimensions().
  uint32_t width = 0;
  uint32_t height = 0;
};
