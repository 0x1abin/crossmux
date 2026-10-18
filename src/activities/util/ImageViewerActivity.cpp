#include "ImageViewerActivity.h"

#include <Bitmap.h>
#include <Epub/converters/JpegToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PngToBmpConverter.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/SleepCover.h"

namespace {
constexpr const char* IMAGE_PREVIEW_PATH = "/.crosspoint/image_preview.bmp";
constexpr const char* TRANSPARENT_PREVIEW_PATH = "/.crosspoint/image_preview.transparent.bmp";
constexpr const char* SLEEP_IMAGE_PART_PATH = "/sleep.bmp.part";
constexpr const char* JPEG_SLEEP_TMP_PXC = "/sleep.bmp.pxc.tmp";
constexpr uint8_t GRAYSCALE_PALETTE[] = {0, 0, 0, 0, 85, 85, 85, 0, 170, 170, 170, 0, 255, 255, 255, 0};

Rect sleepCoverActionRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return Rect(metrics.contentSidePadding,
              renderer.getScreenHeight() - metrics.contentSidePadding - metrics.menuRowHeight,
              renderer.getScreenWidth() - metrics.contentSidePadding * 2, metrics.menuRowHeight);
}
RenderConfig fitImage(const ImageDimensions& dimensions, const GfxRenderer& renderer) {
  const float scale = std::min({static_cast<float>(renderer.getScreenWidth()) / dimensions.width,
                                static_cast<float>(renderer.getScreenHeight()) / dimensions.height, 1.0f});
  const int width = std::max(1, std::min(renderer.getScreenWidth(), static_cast<int>(dimensions.width * scale)));
  const int height = std::max(1, std::min(renderer.getScreenHeight(), static_cast<int>(dimensions.height * scale)));
  RenderConfig config{(renderer.getScreenWidth() - width) / 2, (renderer.getScreenHeight() - height) / 2, width,
                      height};
  config.useExactDimensions = true;
  return config;
}
}  // namespace

ImageViewerActivity::ImageViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("ImageViewer", renderer, mappedInput), filePath(std::move(path)) {}

void ImageViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) return;

  char name[500];
  size_t imageCount = 0;
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) continue;
    file.getName(name, sizeof(name));
    if (name[0] != '.' && FsHelpers::hasImageExtension(std::string_view{name})) ++imageCount;
  }
  dir.rewindDirectory();
  siblingImages.reserve(imageCount);
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.') {
        std::string fname(name);
        if (FsHelpers::hasImageExtension(fname)) {
          siblingImages.push_back(fname);
        }
      }
    }
  }

  FsHelpers::sortFileList(siblingImages);

  const auto image = std::find(siblingImages.begin(), siblingImages.end(), fileName);
  if (image != siblingImages.end()) {
    currentImageIndex = static_cast<int>(image - siblingImages.begin());
  }
}

bool ImageViewerActivity::isPng() const { return FsHelpers::hasPngExtension(filePath); }

bool ImageViewerActivity::preparePreview() {
  if (!Storage.ensureDirectoryExists("/.crosspoint")) return false;
  if (Storage.exists(IMAGE_PREVIEW_PATH) && !Storage.remove(IMAGE_PREVIEW_PATH)) return false;

  bool prepared = false;
  {
    GfxRenderer::FrameBufferLoan loan(renderer);
    prepared = PngToBmpConverter::pngFileToBmpFile(filePath.c_str(), IMAGE_PREVIEW_PATH, true);
  }
  if (!prepared) Storage.remove(IMAGE_PREVIEW_PATH);
  return prepared;
}

bool ImageViewerActivity::renderJpeg() {
  JpegToFramebufferConverter decoder;
  ImageDimensions dimensions;
  if (!decoder.getDimensions(filePath, dimensions) || dimensions.width <= 0 || dimensions.height <= 0) return false;
  auto config = fitImage(dimensions, renderer);
  const bool hasPrevious = siblingImages.size() > 1 && currentImageIndex > 0;
  const bool hasNext = siblingImages.size() > 1 && currentImageIndex >= 0 &&
                       currentImageIndex < static_cast<int>(siblingImages.size()) - 1;
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_SET_SLEEP_COVER), hasPrevious ? "<" : "", hasNext ? ">" : "");
  const auto drawChrome = [&] {
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    if (mappedInput.hasTouch()) GUI.drawActionButton(renderer, sleepCoverActionRect(renderer), tr(STR_SET_SLEEP_COVER));
  };
  const auto drawImage = [&](const DecodeOutput output) {
    config.output = output;
    if (!decoder.decodeToFramebuffer(filePath, renderer, config)) return false;
    drawChrome();
    return true;
  };
  renderer.clearScreen();
  if (renderer.getGrayscaleLevels() == 16 && renderer.beginGrayscale16()) {
    if (drawImage(DecodeOutput::NativeGrayscale16) && renderer.commitGrayscale16()) return true;
    renderer.cancelGrayscale16();
    renderer.clearScreen();
  }
  if (!drawImage(DecodeOutput::FrameBufferAndCache)) return false;
  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return false;
  if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  const ScopedCleanup resetMode{[this] { renderer.setRenderMode(GfxRenderer::BW); }};
  bool ready = true;
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.clearScreen(absolute ? 0xFF : 0x00);
    renderer.setRenderMode(mode);
    if (!drawImage(DecodeOutput::FrameBufferAndCache)) {
      ready = false;
      break;
    }
    if (mode == GfxRenderer::GRAYSCALE_LSB)
      renderer.copyGrayscaleLsbBuffers();
    else
      renderer.copyGrayscaleMsbBuffers();
  }
  if (ready) renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  if (!drawImage(DecodeOutput::FrameBufferAndCache)) ready = false;
  renderer.cleanupGrayscaleWithFrameBuffer();
  return ready;
}

void ImageViewerActivity::onEnter() {
  Activity::onEnter();
  imageReady = false;

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  if (FsHelpers::hasJpgExtension(filePath)) {
    imageReady = renderJpeg();
    if (!imageReady) {
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    return;
  }
  const bool needsPreview = isPng();
  const bool prepared = !needsPreview || preparePreview();
  const char* bitmapPath = needsPreview ? IMAGE_PREVIEW_PATH : filePath.c_str();
  HalFile file;
  // 1. Open the file
  if (!prepared) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_IMAGE_FILE));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else if (Storage.openFileForRead("IMAGE", bitmapPath, file)) {
    Bitmap bitmap(file, true,
                  renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                      display.getController() == HalDisplay::Controller::SSD1677);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
      bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                      currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), tr(STR_SET_SLEEP_COVER), (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));

      GUI.fillPopupProgress(renderer, popupRect, 50);

      renderer.clearScreen();
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
        renderer.clearScreen();
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        return;
      }

      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      if (mappedInput.hasTouch()) {
        GUI.drawActionButton(renderer, sleepCoverActionRect(renderer), tr(STR_SET_SLEEP_COVER));
      }
      if (bitmap.hasGreyscale()) {
        const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
        if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return;
        if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
        bool planesReady = true;
        for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
          if (bitmap.rewindToData() != BmpReaderError::Ok) {
            LOG_ERR("BMP", "Failed to rewind bitmap for grayscale rendering");
            planesReady = false;
            break;
          }
          renderer.clearScreen(absolute ? 0xFF : 0x00);
          renderer.setRenderMode(mode);
          if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
            planesReady = false;
            break;
          }
          GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
          if (mappedInput.hasTouch())
            GUI.drawActionButton(renderer, sleepCoverActionRect(renderer), tr(STR_SET_SLEEP_COVER));
          if (mode == GfxRenderer::GRAYSCALE_LSB) {
            renderer.copyGrayscaleLsbBuffers();
          } else {
            renderer.copyGrayscaleMsbBuffers();
          }
        }
        if (planesReady) renderer.displayGrayBuffer();

        // Rebuild the BW framebuffer for popups and subsequent differential updates.
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.clearScreen();
        if (bitmap.rewindToData() != BmpReaderError::Ok ||
            !renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
          LOG_ERR("BMP", "Failed to rewind bitmap to restore the BW framebuffer");
          renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
          planesReady = false;
        }
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        renderer.cleanupGrayscaleWithFrameBuffer();
        imageReady = planesReady;
        if (!planesReady) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      } else {
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
        imageReady = true;
      }

    } else {
      // Handle file parsing error
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_IMAGE_FILE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }

  } else {
    // Handle file open error
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void ImageViewerActivity::onExit() {
  Activity::onExit();
  imageReady = false;
  if (Storage.exists(IMAGE_PREVIEW_PATH)) Storage.remove(IMAGE_PREVIEW_PATH);
  if (Storage.exists(TRANSPARENT_PREVIEW_PATH)) Storage.remove(TRANSPARENT_PREVIEW_PATH);
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool ImageViewerActivity::prepareJpegSleepCover() {
  if (Storage.exists(JPEG_SLEEP_TMP_PXC) && !Storage.remove(JPEG_SLEEP_TMP_PXC)) {
    LOG_ERR("BMP", "Failed to remove previous sleep image pixels");
    return false;
  }
  bool prepared = false;
  const ScopedCleanup cleanup{[&prepared]() {
    Storage.remove(JPEG_SLEEP_TMP_PXC);
    if (!prepared) Storage.remove(SLEEP_IMAGE_PART_PATH);
  }};

  JpegToFramebufferConverter decoder;
  ImageDimensions dimensions;
  if (!decoder.getDimensions(filePath, dimensions)) return false;
  auto config = fitImage(dimensions, renderer);
  if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP &&
      (dimensions.width > renderer.getScreenWidth() || dimensions.height > renderer.getScreenHeight())) {
    const float ratio = static_cast<float>(dimensions.width) / dimensions.height;
    const float screenRatio = static_cast<float>(renderer.getScreenWidth()) / renderer.getScreenHeight();
    config.x = config.y = 0;
    config.maxWidth = renderer.getScreenWidth();
    config.maxHeight = renderer.getScreenHeight();
    if (ratio > screenRatio) {
      config.sourceCropX = 1.0f - screenRatio / ratio;
    } else {
      config.sourceCropY = 1.0f - ratio / screenRatio;
    }
  }
  config.cachePath = JPEG_SLEEP_TMP_PXC;
  renderer.clearScreen();
  bool cacheWritten;
  if (!decoder.decodeToFramebuffer(filePath, renderer, config, cacheWritten) || !cacheWritten) {
    LOG_ERR("BMP", "Failed to decode sleep image pixels");
    return false;
  }

  HalFile input, output;
  if (!Storage.openFileForRead("BMP", JPEG_SLEEP_TMP_PXC, input)) return false;
  uint16_t width, height;
  if (input.read(&width, sizeof(width)) != sizeof(width) || input.read(&height, sizeof(height)) != sizeof(height) ||
      width != config.maxWidth || height != config.maxHeight) {
    LOG_ERR("BMP", "Invalid sleep image pixels");
    return false;
  }
  const size_t pixelRowBytes = (width + 3) / 4;
  const size_t bmpRowBytes = (width + 15) / 16 * 4;
  if (input.size() != 4 + pixelRowBytes * height) {
    LOG_ERR("BMP", "Incomplete sleep image pixels");
    return false;
  }
  // One variable-width BMP row is reused; full-frame pixels remain streamed on SD.
  auto row = makeUniqueNoThrow<uint8_t[]>(bmpRowBytes);
  if (!row) {
    LOG_ERR("BMP", "OOM: sleep image row");
    return false;
  }
  if (!Storage.openFileForWrite("BMP", SLEEP_IMAGE_PART_PATH, output)) return false;
  BmpHeader header;
  createBmpHeader(&header, width, height, BmpRowOrder::TopDown);
  header.infoHeader.biBitCount = 2;
  header.infoHeader.biClrUsed = header.infoHeader.biClrImportant = 4;
  header.infoHeader.biSizeImage = bmpRowBytes * height;
  header.fileHeader.bfOffBits = sizeof(header) - sizeof(header.colors) + sizeof(GRAYSCALE_PALETTE);
  header.fileHeader.bfSize = header.fileHeader.bfOffBits + header.infoHeader.biSizeImage;
  if (output.write(&header, sizeof(header) - sizeof(header.colors)) != sizeof(header) - sizeof(header.colors) ||
      output.write(GRAYSCALE_PALETTE, sizeof(GRAYSCALE_PALETTE)) != sizeof(GRAYSCALE_PALETTE)) {
    LOG_ERR("BMP", "Failed to write sleep image header");
    return false;
  }
  for (int y = 0; y < height; ++y) {
    if (input.read(row.get(), pixelRowBytes) != pixelRowBytes || output.write(row.get(), bmpRowBytes) != bmpRowBytes) {
      LOG_ERR("BMP", "Failed to write sleep image row %d", y);
      return false;
    }
  }
  if (!output.close()) {
    LOG_ERR("BMP", "Failed to close sleep image");
    return false;
  }
  prepared = true;
  return true;
}

void ImageViewerActivity::doSetSleepCover(const char* sourcePath, const bool transparent) {
  if (!imageReady) return;
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  const char* preparedPath = sourcePath;
  bool success = sleepCover::recover();
  if (success && transparent) {
    GfxRenderer::FrameBufferLoan loan(renderer);
    success = PngToBmpConverter::pngFileToTransparentBmpFile(sourcePath, TRANSPARENT_PREVIEW_PATH, true);
    if (success) preparedPath = TRANSPARENT_PREVIEW_PATH;
  }

  if (Storage.exists(SLEEP_IMAGE_PART_PATH) && !Storage.remove(SLEEP_IMAGE_PART_PATH)) success = false;
  const bool jpeg = FsHelpers::hasJpgExtension(std::string_view{sourcePath});
  if (success && jpeg) success = prepareJpegSleepCover();
  if (success && !jpeg) {
    HalFile inFile, outFile;
    if (Storage.openFileForRead("IMAGE", preparedPath, inFile) &&
        Storage.openFileForWrite("IMAGE", SLEEP_IMAGE_PART_PATH, outFile)) {
      const uint64_t expected = inFile.fileSize64();
      uint64_t copied = 0;
      char buffer[128];
      int bytesRead;
      while ((bytesRead = inFile.read(buffer, sizeof(buffer))) > 0) {
        if (outFile.write(buffer, bytesRead) != bytesRead) {
          success = false;
          break;
        }
        copied += bytesRead;
      }
      outFile.flush();
      success = success && copied == expected && outFile.close();
    } else {
      success = false;
    }
  }

  if (success)
    success = sleepCover::commit(transparent ? CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT
                                             : CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM);
  Storage.remove(SLEEP_IMAGE_PART_PATH);
  GUI.drawPopup(renderer, success ? tr(STR_DONE) : tr(STR_FAILED_LOWER));
  delay(1000);
}

void ImageViewerActivity::showSleepCoverOptions() {
  if (!imageReady) return;
  if (!isPng()) {
    doSetSleepCover(filePath.c_str(), false);
    return;
  }

  static constexpr StrId options[] = {StrId::STR_NORMAL, StrId::STR_TRANSPARENT};
  static constexpr int optionCount = sizeof(options) / sizeof(options[0]);
  sleepCoverPopup.show(StrId::STR_SET_SLEEP_COVER, options, optionCount, 0, [this](const int index) {
    doSetSleepCover(index == 1 ? filePath.c_str() : IMAGE_PREVIEW_PATH, index == 1);
  });
  requestUpdate();
}

void ImageViewerActivity::render(RenderLock&&) { sleepCoverPopup.processRender(renderer, mappedInput); }

void ImageViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  if (sleepCoverPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (!sleepCoverPopup.isActive()) onEnter();
    return;
  }

  auto openSibling = [this](const int delta) {
    if (currentImageIndex < 0) {
      return false;
    }
    const int nextIndex = currentImageIndex + delta;
    if (siblingImages.size() <= 1 || nextIndex < 0 || nextIndex >= static_cast<int>(siblingImages.size())) {
      return false;
    }
    currentImageIndex = nextIndex;
    std::string dirPath = FsHelpers::extractFolderPath(filePath);
    if (dirPath.back() != '/') dirPath += "/";
    filePath = dirPath + siblingImages[currentImageIndex];
    onEnter();
    return true;
  };

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToFileBrowser(filePath);
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    openSibling(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    openSibling(-1);
    return;
  }

  if (mappedInput.hasTouch()) {
    const Rect sleepCoverAction = sleepCoverActionRect(renderer);
    if (mappedInput.wasTapInRect(sleepCoverAction.x, sleepCoverAction.y, sleepCoverAction.width,
                                 sleepCoverAction.height)) {
      showSleepCoverOptions();
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    showSleepCoverOptions();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    openSibling(1);
    return;
  }
}
