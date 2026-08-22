#include "ImageViewerActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <PngToBmpConverter.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* PNG_PREVIEW_PATH = "/.crosspoint/image_preview.bmp";
constexpr const char* TRANSPARENT_PREVIEW_PATH = "/.crosspoint/image_preview.transparent.bmp";
constexpr const char* SLEEP_IMAGE_PATH = "/sleep.bmp";
constexpr const char* SLEEP_IMAGE_PART_PATH = "/sleep.bmp.part";
constexpr const char* SLEEP_IMAGE_BACKUP_PATH = "/sleep.bmp.bak";
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
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.') {
        std::string fname(name);
        if (FsHelpers::hasBmpExtension(fname) || FsHelpers::hasPngExtension(fname)) {
          siblingImages.push_back(fname);
        }
      }
    }
  }

  FsHelpers::sortFileList(siblingImages);

  for (size_t i = 0; i < siblingImages.size(); ++i) {
    if (siblingImages[i] == fileName) {
      currentImageIndex = static_cast<int>(i);
      break;
    }
  }
}

bool ImageViewerActivity::isPng() const { return FsHelpers::hasPngExtension(filePath); }

void ImageViewerActivity::onEnter() {
  Activity::onEnter();

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  const bool png = isPng();
  bool prepared = !png;
  if (png && Storage.ensureDirectoryExists("/.crosspoint")) {
    GfxRenderer::FrameBufferLoan loan(renderer);
    prepared = PngToBmpConverter::pngFileToBmpFile(filePath.c_str(), PNG_PREVIEW_PATH, true);
  }
  const char* bitmapPath = png ? PNG_PREVIEW_PATH : filePath.c_str();
  HalFile file;
  // 1. Open the file
  if (!prepared) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_IMAGE_FILE));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  } else if (Storage.openFileForRead("IMAGE", bitmapPath, file)) {
    Bitmap bitmap(file, true);

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
      // Assuming drawBitmap defaults to 0,0 crop if omitted, or pass explicitly: drawBitmap(bitmap, x, y, pageWidth,
      // pageHeight, 0, 0)
      renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);

      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      // Single pass for non-grayscale images

      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

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
  if (Storage.exists(PNG_PREVIEW_PATH)) Storage.remove(PNG_PREVIEW_PATH);
  if (Storage.exists(TRANSPARENT_PREVIEW_PATH)) Storage.remove(TRANSPARENT_PREVIEW_PATH);
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void ImageViewerActivity::doSetSleepCover(const char* sourcePath, const bool transparent) {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  const char* preparedPath = sourcePath;
  bool success = true;
  if (transparent) {
    GfxRenderer::FrameBufferLoan loan(renderer);
    success = PngToBmpConverter::pngFileToTransparentBmpFile(sourcePath, TRANSPARENT_PREVIEW_PATH, true);
    if (success) preparedPath = TRANSPARENT_PREVIEW_PATH;
  }

  Storage.remove(SLEEP_IMAGE_PART_PATH);
  if (success) {
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
      success = success && copied == expected;
    } else {
      success = false;
    }
  }

  if (success) {
    Storage.remove(SLEEP_IMAGE_BACKUP_PATH);
    const bool hadPrevious = Storage.exists(SLEEP_IMAGE_PATH);
    if ((hadPrevious && !Storage.rename(SLEEP_IMAGE_PATH, SLEEP_IMAGE_BACKUP_PATH)) ||
        !Storage.rename(SLEEP_IMAGE_PART_PATH, SLEEP_IMAGE_PATH)) {
      if (hadPrevious && !Storage.exists(SLEEP_IMAGE_PATH)) {
        Storage.rename(SLEEP_IMAGE_BACKUP_PATH, SLEEP_IMAGE_PATH);
      }
      success = false;
    }
  }

  if (success) {
    const uint8_t previousMode = SETTINGS.sleepScreen;
    SETTINGS.sleepScreen = transparent ? CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT
                                       : CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    if (!SETTINGS.saveToFile()) {
      SETTINGS.sleepScreen = previousMode;
      (void)SETTINGS.saveToFile();
      Storage.remove(SLEEP_IMAGE_PATH);
      if (Storage.exists(SLEEP_IMAGE_BACKUP_PATH)) {
        Storage.rename(SLEEP_IMAGE_BACKUP_PATH, SLEEP_IMAGE_PATH);
      }
      success = false;
    }
  }

  Storage.remove(SLEEP_IMAGE_PART_PATH);
  if (success) Storage.remove(SLEEP_IMAGE_BACKUP_PATH);
  GUI.drawPopup(renderer, success ? tr(STR_DONE) : tr(STR_FAILED_LOWER));
  delay(1000);
}

void ImageViewerActivity::showSleepCoverOptions() {
  if (!isPng()) {
    doSetSleepCover(filePath.c_str(), false);
    return;
  }

  static constexpr StrId options[] = {StrId::STR_NORMAL, StrId::STR_TRANSPARENT};
  static constexpr int optionCount = sizeof(options) / sizeof(options[0]);
  sleepCoverPopup.show(StrId::STR_SET_SLEEP_COVER, options, optionCount, 0, [this](const int index) {
    doSetSleepCover(index == 1 ? filePath.c_str() : PNG_PREVIEW_PATH, index == 1);
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
