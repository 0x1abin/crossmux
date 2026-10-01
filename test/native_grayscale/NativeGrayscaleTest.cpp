#include <Arduino.h>
#include <Bitmap.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <SdCardFont.h>

#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <vector>

#include "AirPageImageRenderer.h"
#include "AirPageWallpaper.h"
#include "CrossPointSettings.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/converters/JpegToFramebufferConverter.h"
#include "QrParams.h"
#include "SleepProbe.h"
#include "components/themes/BaseTheme.h"

// Four-level ImageBlock is a deliberate failing seam: native AirPage must not use it.
ImageBlock::ImageBlock(const std::string&, const std::string&, int16_t, int16_t) : width(0), height(0) {}
void ImageBlock::releaseRenderCache() {}
void ImageBlock::clearSessionRenderFailures() {}
bool ImageBlock::render(GfxRenderer&, int, int, PixelCachePolicy) { return false; }

bool FontCacheManager::isScanning() const { return false; }
void FontCacheManager::clearCache() {}
void SdCardFont::clearCache() {}
ESPMock ESP;
namespace {
std::array<uint8_t, HalDisplay::BUFFER_SIZE> bw;
std::array<uint8_t, HalDisplay::DISPLAY_WIDTH * HalDisplay::DISPLAY_HEIGHT / 2> native;
uint8_t levels = 16;
bool loan = false, failRefresh = false, failAllocation = false;
int commits = 0, cancels = 0;
}  // namespace
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  return failAllocation ? nullptr : std::malloc(size);
}
HalDisplay::HalDisplay() {}
HalDisplay::~HalDisplay() {}
uint8_t* HalDisplay::getFrameBuffer() const { return bw.data(); }
uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }
void HalDisplay::clearScreen(uint8_t value) const { bw.fill(value); }
uint8_t HalDisplay::getGrayscaleLevels() const { return levels; }
uint8_t* HalDisplay::beginGrayscale16() {
  if (loan) return nullptr;
  loan = true;
  native.fill(0xFF);
  return native.data();
}
bool HalDisplay::commitGrayscale16() {
  loan = false;
  ++commits;
  return !failRefresh;
}
void HalDisplay::cancelGrayscale16() {
  loan = false;
  ++cancels;
}
bool HalDisplay::isInverted() const { return false; }
void HalDisplay::displayBuffer(RefreshMode, bool) {}
void HalDisplay::displayGrayscaleBase(RefreshMode, bool) {}
bool HalDisplay::displayGrayscaleBase(GrayscaleMode, RefreshMode mode, bool off) {
  displayGrayscaleBase(mode, off);
  return true;
}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(bool, const unsigned char*, bool) {}
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
HalDisplay::Controller HalDisplay::getController() const { return Controller::LgfxEpd; }
HalDisplay::GrayscaleCapabilities HalDisplay::grayscaleCapabilities(GrayscaleMode) const { return {}; }
HalDisplay display;

namespace {
uint8_t tone(int x, int y) {
  const size_t pixel = y * HalDisplay::DISPLAY_WIDTH + x;
  return (native[pixel / 2] >> ((pixel & 1) * 4)) & 15;
}
void le16(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(value);
  bytes.push_back(value >> 8);
}
void le32(std::vector<uint8_t>& bytes, uint32_t value) {
  le16(bytes, value);
  le16(bytes, value >> 16);
}
std::vector<uint8_t> ramp(bool topDown, bool reversedPalette) {
  constexpr int width = 17, height = 2, rowBytes = 12, offset = 118;
  std::vector<uint8_t> bytes;
  bytes.reserve(offset + height * rowBytes);
  le16(bytes, 0x4D42);
  le32(bytes, offset + height * rowBytes);
  le32(bytes, 0);
  le32(bytes, offset);
  le32(bytes, 40);
  le32(bytes, width);
  le32(bytes, topDown ? -height : height);
  le16(bytes, 1);
  le16(bytes, 4);
  le32(bytes, 0);
  le32(bytes, height * rowBytes);
  le32(bytes, 0);
  le32(bytes, 0);
  le32(bytes, 16);
  le32(bytes, 16);
  for (int i = 0; i < 16; ++i) {
    const uint8_t gray = (reversedPalette ? 15 - i : i) * 17;
    bytes.insert(bytes.end(), {gray, gray, gray, 0});
  }
  for (int row = 0; row < height; ++row) {
    const int y = topDown ? row : height - 1 - row;
    for (int x = 0; x < rowBytes * 2; x += 2) {
      const auto index = [=](int pixel) { return pixel < width ? (pixel + y) % 16 : 0; };
      bytes.push_back((index(x) << 4) | index(x + 1));
    }
  }
  return bytes;
}
std::pair<int, int> physical(GfxRenderer::Orientation orientation, int x, int y) {
  switch (orientation) {
    case GfxRenderer::Portrait:
      return {y, 31 - x};
    case GfxRenderer::PortraitInverted:
      return {127 - y, x};
    case GfxRenderer::LandscapeClockwise:
      return {127 - x, 31 - y};
    case GfxRenderer::LandscapeCounterClockwise:
      return {x, y};
  }
  std::abort();
}
class RecordingPrint : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t limit = SIZE_MAX;
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    const size_t count = std::min(size, limit - bytes.size());
    bytes.insert(bytes.end(), data, data + count);
    return count;
  }
};
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 3);
  setenv("CROSSPOINT_SIM_SD", argv[2], 1);
  GfxRenderer renderer(display);
  renderer.begin();
  levels = 4;
  assert(renderer.getGrayscaleLevels() == 4 && !renderer.beginGrayscale16());
  assert(qrParams(renderer) == "&w=32&h=128&mode=gray4");
  levels = 16;
  assert(qrParams(renderer) == "&w=32&h=128&mode=gray16");
  for (const auto orientation : {GfxRenderer::Portrait, GfxRenderer::PortraitInverted, GfxRenderer::LandscapeClockwise,
                                 GfxRenderer::LandscapeCounterClockwise}) {
    renderer.setOrientation(orientation);
    for (const bool topDown : {false, true})
      for (const bool reversed : {false, true}) {
        auto bytes = ramp(topDown, reversed);
        Bitmap bitmap(bytes.data(), bytes.size());
        assert(bitmap.parseHeaders() == BmpReaderError::Ok);
        assert(renderer.beginGrayscale16() && !renderer.beginGrayscale16());
        assert(renderer.drawBitmapGrayscale16(bitmap, 2, 3, 17, 2));
        for (int y = 0; y < 2; ++y)
          for (int x = 0; x < 17; ++x) {
            const auto [px, py] = physical(orientation, x + 2, y + 3);
            const int expected = reversed ? 15 - (x + y) % 16 : (x + y) % 16;
            assert(tone(px, py) == expected);
          }
        assert(renderer.commitGrayscale16() && !renderer.commitGrayscale16());
        bitmap.rewindToData();
        assert(renderer.beginGrayscale16());
        assert(renderer.drawBitmapGrayscale16(bitmap, 0, 0, 8, 1));
        renderer.cancelGrayscale16();
        assert(!loan);
        bytes.pop_back();
        Bitmap truncated(bytes.data(), bytes.size());
        assert(truncated.parseHeaders() == BmpReaderError::Ok);
        assert(renderer.beginGrayscale16());
        assert(!renderer.drawBitmapGrayscale16(truncated, 0, 0, 17, 2));
        renderer.cancelGrayscale16();
        Bitmap oom(bytes.data(), bytes.size());
        assert(oom.parseHeaders() == BmpReaderError::Ok && renderer.beginGrayscale16());
        failAllocation = true;
        assert(!renderer.drawBitmapGrayscale16(oom, 0, 0, 17, 2));
        failAllocation = false;
        renderer.cancelGrayscale16();
      }
  }
  renderer.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  std::ifstream source(argv[1], std::ios::binary);
  std::ofstream copied(std::string(argv[2]) + "/ramp.jpg", std::ios::binary);
  copied << source.rdbuf();
  copied.close();
  JpegToFramebufferConverter jpeg;
  RenderConfig config{};
  config.x = 0;
  config.y = 0;
  config.maxWidth = 128;
  config.maxHeight = 16;
  config.output = DecodeOutput::NativeGrayscale16;
  config.useDithering = true;  // Native branch must bypass four-level dithering.
  assert(!jpeg.decodeToFramebuffer("/ramp.jpg", renderer, config));
  assert(renderer.beginGrayscale16());
  assert(jpeg.decodeToFramebuffer("/ramp.jpg", renderer, config));
  for (int i = 0; i < 16; ++i) assert(tone(i * 8 + 4, 8) == i);
  failRefresh = true;
  assert(!renderer.commitGrayscale16() && !renderer.isGrayscale16Active());
  failRefresh = false;
  HalFile file;
  assert(Storage.openFileForRead("TEST", "/ramp.jpg", file));
  RecordingPrint out;
  assert(JpegToBmpConverter::jpegFileToBmpStream(file, out, false, JpegToBmpConverter::Output::Gray8));
  Bitmap saved(out.bytes.data(), out.bytes.size());
  assert(saved.parseHeaders() == BmpReaderError::Ok && saved.getBpp() == 8);
  assert(renderer.beginGrayscale16());
  assert(renderer.drawBitmapGrayscale16(saved, 0, 0, 128, 32));
  for (int i = 0; i < 16; ++i) assert(tone(i * 2 + 1, 2) == i);
  assert(renderer.commitGrayscale16());
  for (const size_t limit : {size_t(3), size_t(1079)}) {
    assert(file.seek(0));
    RecordingPrint shortOut;
    shortOut.limit = limit;
    assert(!JpegToBmpConverter::jpegFileToBmpStream(file, shortOut, false, JpegToBmpConverter::Output::Gray8));
  }
  assert(file.seek(0));
  RecordingPrint legacy;
  assert(JpegToBmpConverter::jpegFileToBmpStream(file, legacy, false));
  Bitmap old(legacy.bytes.data(), legacy.bytes.size());
  assert(old.parseHeaders() == BmpReaderError::Ok && old.getBpp() == 2);
  airpage::SelectedImage selected;
  std::strcpy(selected.path, "/ramp.jpg");
  selected.image = {airpage::ImageFormat::Jpeg, 128, 16, true};
  const Rect viewport(0, 0, 128, 16);
  const int before = commits;
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, selected));
  assert(commits == before + 1 && !loan);
  // A popup paints the B/W proxy; closing it re-renders the unchanged original.
  renderer.clearScreen();
  assert(airpage::AirPageImageRenderer::render(renderer, viewport, selected));
  for (int i = 0; i < 16; ++i) assert(tone(i * 8 + 4, 8) == i);
  failRefresh = true;
  assert(!airpage::AirPageImageRenderer::render(renderer, viewport, selected));
  failRefresh = false;
  assert(!loan);
  assert(airpage::AirPageWallpaper::install(selected));
  assert(SETTINGS.sleepScreen == CrossPointSettings::CUSTOM);
  HalFile sleep;
  assert(Storage.openFileForRead("TEST", "/sleep.bmp", sleep));
  Bitmap sleepBitmap(sleep);
  assert(sleepBitmap.parseHeaders() == BmpReaderError::Ok && sleepBitmap.getBpp() == 8);
  // A settings failure must restore the previous original BMP and delete .part/.bak.
  const auto bmp = ramp(false, true);
  std::ofstream bmpFile(std::string(argv[2]) + "/ramp.bmp", std::ios::binary);
  bmpFile.write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
  bmpFile.close();
  std::strcpy(selected.path, "/ramp.bmp");
  selected.image = {airpage::ImageFormat::Bmp, 17, 2, true};
  SETTINGS.failedSaves = 1;
  assert(!airpage::AirPageWallpaper::install(selected));
  airpage::ImageInfo retained;
  assert(airpage::AirPageImageStore::inspectImage("/sleep.bmp", retained) && retained.width == 32);
  assert(!Storage.exists("/sleep.bmp.part") && !Storage.exists("/sleep.bmp.bak"));
  assert(airpage::AirPageWallpaper::install(selected));
  assert(airpage::AirPageImageStore::inspectImage("/sleep.bmp", retained) && retained.width == 17);
  SleepProbe sleepProbe(renderer);
  const int beforeSleep = commits;
  sleepProbe.renderCustomSleepScreen();
  assert(commits == beforeSleep + 1 && sleepProbe.defaults == 0 && !loan);
  failRefresh = true;
  sleepProbe.renderCustomSleepScreen();
  assert(sleepProbe.defaults == 1 && !loan);
  failRefresh = false;
  SETTINGS.sleepScreenCoverFilter = CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::BLACK_AND_WHITE;
  const int beforeFiltered = commits;
  sleepProbe.renderCustomSleepScreen();
  assert(commits == beforeFiltered);  // Existing filters must not enter native output.
  SETTINGS.sleepScreenCoverFilter = CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  assert(commits > 0 && cancels > 0);
}
