#include "FontDownloadActivity.h"

#ifdef ENABLE_CHINESE_VERSION
#include <atomic>
#endif

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NetworkStartup.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {

constexpr size_t kMaxManifestBytes = 64 * 1024;
constexpr size_t kMaxManifestFamilies = 32;
constexpr size_t kMaxManifestFiles = 128;
constexpr size_t kMaxFilesPerFamily = 16;
constexpr size_t kMaxFamilyNameBytes = 64;
constexpr size_t kMaxDescriptionBytes = 160;
constexpr size_t kMaxBaseUrlBytes = 256;
constexpr size_t kMaxFontFileBytes = 25 * 1024 * 1024;

bool parseManifestPointSize(const char* familyName, const char* fileName, uint8_t& pointSize) {
  const size_t familyLength = strlen(familyName);
  const size_t fileNameLength = strlen(fileName);
  if (fileNameLength <= familyLength + 1) return false;
  if (strncmp(fileName, familyName, familyLength) != 0 || fileName[familyLength] != '_') return false;

  const char* cursor = fileName + familyLength + 1;
  if (*cursor < '1' || *cursor > '9') return false;

  uint16_t value = 0;
  while (std::isdigit(static_cast<unsigned char>(*cursor))) {
    value = static_cast<uint16_t>(value * 10 + (*cursor - '0'));
    if (value > UINT8_MAX) return false;
    ++cursor;
  }
  if (strcmp(cursor, ".cpfont") != 0) return false;

  pointSize = static_cast<uint8_t>(value);
  return true;
}

#ifdef ENABLE_CHINESE_VERSION
std::atomic<bool> chineseFontPromptShownThisBoot{false};
#endif

}  // namespace

FontDownloadActivity::FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const Purpose purpose)
    : Activity("FontDownload", renderer, mappedInput), purpose_(purpose), fontInstaller_(sdFontSystem.registry()) {}

#ifdef ENABLE_CHINESE_VERSION
bool FontDownloadActivity::wasChineseFontPromptShownThisBoot() {
  return chineseFontPromptShownThisBoot.load(std::memory_order_relaxed);
}
#endif

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  Activity::onEnter();

  switch (purpose_) {
    case Purpose::Manage:
      startWifiSelection();
      return;
    case Purpose::PromptThenManage: {
      // ActivityManager owns the dialog across frames, so it must live on the heap.
      auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(
          renderer, mappedInput, tr(STR_CHINESE_FONT_INCOMPLETE), tr(STR_DOWNLOAD_FULL_CHINESE_FONT),
          ConfirmationActivity::BodyPlacement::PopupTitle);
      if (!confirmation) {
        LOG_ERR("FONT", "OOM allocating ConfirmationActivity (%zu bytes)", sizeof(ConfirmationActivity));
        finish();
        return;
      }
#ifdef ENABLE_CHINESE_VERSION
      chineseFontPromptShownThisBoot.store(true, std::memory_order_relaxed);
#endif
      startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
        if (result.isCancelled) {
          finish();
          return;
        }
        startWifiSelection();
      });
      return;
    }
  }
}

void FontDownloadActivity::startWifiSelection() {
  // ActivityManager owns the Wi-Fi picker across frames, so it must live on the heap.
  auto wifiSelection = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifiSelection) {
    LOG_ERR("FONT", "OOM allocating WifiSelectionActivity (%zu bytes)", sizeof(WifiSelectionActivity));
    finish();
    return;
  }
  startActivityForResult(std::move(wifiSelection),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FontDownloadActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FontDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    state_ = LOADING_MANIFEST;
  }
  requestUpdateAndWait();
  NetworkStartup::prepare(renderer);

  if (!fetchAndParseManifest()) {
    {
      RenderLock lock(*this);
      state_ = ERROR;
    }
    return;
  }

  {
    RenderLock lock(*this);
    state_ = FAMILY_LIST;
    selectedIndex_ = 0;
  }
}

// --- Manifest fetching ---

bool FontDownloadActivity::fetchAndParseManifest() {
  // Download manifest to a temp file on SD card to avoid holding both
  // TLS buffers and the full JSON string in RAM simultaneously.
  static constexpr const char* MANIFEST_TMP = "/fonts_manifest.tmp";
  families_.clear();
  files_.clear();
  baseUrl_.clear();
  downloadingFamilyIndex_ = -1;

  auto result = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, nullptr);
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Failed to fetch manifest from %s", FONT_MANIFEST_URL);
    errorMessage_ = "Failed to fetch font list";
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  JsonDocument doc;
  DeserializationError err;
  bool manifestTooLarge = false;
  {
    HalFile manifestFile;
    if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
      LOG_ERR("FONT", "Failed to open temp manifest");
      Storage.remove(MANIFEST_TMP);
      errorMessage_ = "Failed to read font list";
      return false;
    }
    manifestTooLarge = manifestFile.fileSize() > kMaxManifestBytes;
    if (!manifestTooLarge) err = deserializeJson(doc, manifestFile);
  }
  Storage.remove(MANIFEST_TMP);

  if (manifestTooLarge || err) {
    LOG_ERR("FONT", "Manifest parse error: %s", err.c_str());
    errorMessage_ = "Invalid font manifest";
    return false;
  }

  int version = doc["version"] | 0;
  if (version != FONTS_MANIFEST_VERSION) {
    LOG_ERR("FONT", "Unsupported manifest version: %d", version);
    errorMessage_ = "Unsupported manifest version";
    return false;
  }

  baseUrl_ = doc["baseUrl"] | "";
  if (baseUrl_.empty() || baseUrl_.size() > kMaxBaseUrlBytes || baseUrl_.rfind("https://", 0) != 0 ||
      baseUrl_.back() != '/') {
    LOG_ERR("FONT", "Manifest has invalid baseUrl");
    errorMessage_ = "Invalid font manifest";
    return false;
  }
  fontInstaller_.refreshRegistry();

  JsonArray familiesArr = doc["families"].as<JsonArray>();
  if (familiesArr.isNull() || familiesArr.size() == 0 || familiesArr.size() > kMaxManifestFamilies) {
    LOG_ERR("FONT", "Manifest has invalid family count: %zu", familiesArr.size());
    errorMessage_ = "Invalid font manifest";
    return false;
  }

  size_t fileCount = 0;
  for (JsonObject familyObject : familiesArr) {
    const JsonArray familyFiles = familyObject["files"].as<JsonArray>();
    if (familyFiles.isNull() || familyFiles.size() == 0 || familyFiles.size() > kMaxFilesPerFamily ||
        familyFiles.size() > kMaxManifestFiles - fileCount) {
      LOG_ERR("FONT", "Manifest has invalid file count");
      errorMessage_ = "Invalid font manifest";
      return false;
    }
    fileCount += familyFiles.size();
  }

  families_.reserve(familiesArr.size());
  files_.reserve(fileCount);

  for (JsonObject fObj : familiesArr) {
    ManifestFamily family;
    family.name = fObj["name"] | "";
    family.description = fObj["description"] | "";
    const bool duplicateFamily = std::any_of(families_.begin(), families_.end(),
                                             [&family](const auto& existing) { return existing.name == family.name; });
    if (!FontInstaller::isValidFamilyName(family.name.c_str()) || family.name.size() > kMaxFamilyNameBytes ||
        family.description.empty() || family.description.size() > kMaxDescriptionBytes || duplicateFamily) {
      LOG_ERR("FONT", "Malformed manifest family name: %s", family.name.c_str());
      families_.clear();
      files_.clear();
      errorMessage_ = "Invalid font manifest";
      return false;
    }

    const JsonArray filesArr = fObj["files"].as<JsonArray>();
    family.fileOffset = files_.size();
    family.fileCount = filesArr.size();
    family.installed = fontInstaller_.isFamilyInstalled(family.name.c_str());
    for (JsonObject fileObj : filesArr) {
      ManifestFile file;
      const char* fileName = fileObj["name"] | "";
      file.size = fileObj["size"] | 0;

      if (!FontInstaller::isValidCpfontFilename(fileName) ||
          !parseManifestPointSize(family.name.c_str(), fileName, file.pointSize) || file.size == 0 ||
          file.size >= kMaxFontFileBytes || !fileObj["crc32"].is<uint32_t>()) {
        LOG_ERR("FONT", "Malformed manifest file entry: %s", fileName);
        families_.clear();
        files_.clear();
        errorMessage_ = "Invalid font manifest";
        return false;
      }
      const bool duplicatePointSize =
          std::any_of(files_.begin() + static_cast<ptrdiff_t>(family.fileOffset), files_.end(),
                      [&file](const auto& existing) { return existing.pointSize == file.pointSize; });
      if (duplicatePointSize) {
        LOG_ERR("FONT", "Duplicate manifest point size: %s", fileName);
        families_.clear();
        files_.clear();
        errorMessage_ = "Invalid font manifest";
        return false;
      }
      file.crc32 = fileObj["crc32"].as<uint32_t>();

      family.totalSize += file.size;
      files_.push_back(file);

      // Detect updates by comparing manifest file sizes with files on disk.
      if (family.installed && !family.hasUpdate) {
        char path[128];
        FontInstaller::buildFontPath(family.name.c_str(), fileName, path, sizeof(path));
        HalFile f;
        if (Storage.openFileForRead("FONT", path, f)) {
          if (f.fileSize() != file.size) family.hasUpdate = true;
        } else {
          family.hasUpdate = true;
        }
      }
    }

    families_.push_back(std::move(family));
  }

  LOG_DBG("FONT", "Manifest loaded: %zu families", families_.size());
  return true;
}

// --- Download ---

void FontDownloadActivity::downloadAll() {
  cancelRequested_ = false;
  for (size_t i = 0; i < families_.size(); i++) {
    if (families_[i].installed) continue;
    if (downloadFamily(families_[i]) != DownloadResult::Success) return;
  }

  const ManifestFamily* selected = nullptr;
  for (const auto& family : families_) {
    if (family.installed && family.name == SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY) {
      selected = &family;
      break;
    }
  }
  if (!selected) {
    const auto it =
        std::find_if(families_.begin(), families_.end(), [](const auto& family) { return family.installed; });
    if (it != families_.end()) selected = &*it;
  }
  if (selected) selectDownloadedFontAndPreview(selected->name.c_str());
}

void FontDownloadActivity::updateAll() {
  cancelRequested_ = false;
  for (size_t i = 0; i < families_.size(); i++) {
    if (!families_[i].hasUpdate) continue;
    if (downloadFamily(families_[i]) != DownloadResult::Success) return;
  }

  {
    RenderLock lock(*this);
    state_ = COMPLETE;
    selectionUpdated_ = false;
    renderer.requestNextFullRefresh();
  }
}

void FontDownloadActivity::downloadSingle(const int familyIndex) {
  if (familyIndex < 0 || familyIndex >= static_cast<int>(families_.size())) return;
  auto& family = families_[familyIndex];
  if (downloadFamily(family) == DownloadResult::Success) selectDownloadedFontAndPreview(family.name.c_str());
}

bool FontDownloadActivity::showDownloadAllRow() const {
  for (const auto& f : families_) {
    if (!f.installed) return true;
  }
  return false;
}

bool FontDownloadActivity::showUpdateAllRow() const {
  for (const auto& f : families_) {
    if (f.hasUpdate) return true;
  }
  return false;
}

int FontDownloadActivity::specialRowCount() const {
  return (showDownloadAllRow() ? 1 : 0) + (showUpdateAllRow() ? 1 : 0);
}

bool FontDownloadActivity::isDownloadAllRow(int index) const { return showDownloadAllRow() && index == 0; }

bool FontDownloadActivity::isUpdateAllRow(int index) const {
  return showUpdateAllRow() && index == (showDownloadAllRow() ? 1 : 0);
}

int FontDownloadActivity::listItemCount() const {
  return families_.empty() ? 0 : static_cast<int>(families_.size()) + specialRowCount();
}

size_t FontDownloadActivity::totalDownloadSize() const {
  size_t total = 0;
  for (const auto& f : families_) {
    if (!f.installed) total += f.totalSize;
  }
  return total;
}

size_t FontDownloadActivity::totalUpdateSize() const {
  size_t total = 0;
  for (const auto& f : families_) {
    if (f.hasUpdate) total += f.totalSize;
  }
  return total;
}

// Standard CRC32 matching zlib/Python zlib.crc32().
bool FontDownloadActivity::computeFileCrc32(const char* path, uint32_t& outCrc) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", path, f)) {
    return false;
  }
  constexpr size_t BUF_SIZE = 128;
  uint8_t buf[BUF_SIZE];
  uint32_t crc = 0;
  while (f.available()) {
    const int n = f.read(buf, BUF_SIZE);
    if (n <= 0) break;
    crc = esp_rom_crc32_le(crc, buf, static_cast<uint32_t>(n));
  }
  outCrc = crc;
  return true;
}

bool FontDownloadActivity::isVerifiedFontFile(const char* path, const ManifestFile& file) {
  {
    HalFile existing;
    if (!Storage.openFileForRead("FONT", path, existing) || existing.size() != file.size) return false;
  }

  uint32_t actualCrc = 0;
  return fontInstaller_.validateCpfontFile(path) && computeFileCrc32(path, actualCrc) && actualCrc == file.crc32;
}

FontDownloadActivity::DownloadResult FontDownloadActivity::downloadFile(const ManifestFamily& family,
                                                                        const ManifestFile& file) {
  char fileName[128];
  const int fileNameLength =
      snprintf(fileName, sizeof(fileName), "%s_%u.cpfont", family.name.c_str(), static_cast<unsigned>(file.pointSize));
  if (fileNameLength < 0 || static_cast<size_t>(fileNameLength) >= sizeof(fileName)) {
    errorMessage_ = "Invalid font filename";
    return DownloadResult::Failed;
  }
  char destPath[128];
  FontInstaller::buildFontPath(family.name.c_str(), fileName, destPath, sizeof(destPath));
  char downloadPath[136];
  snprintf(downloadPath, sizeof(downloadPath), "%s.part", destPath);

  if (isVerifiedFontFile(destPath, file)) {
    Storage.remove(downloadPath);
    LOG_INF("FONT", "Skipping verified file: %s", fileName);
    currentFileIndex_++;
    return DownloadResult::Success;
  }

  requestUpdateAndWait();

  std::string url = baseUrl_ + fileName;
  std::string destination = downloadPath;
  HttpDownloader::ProgressCallback progress = [this](size_t, size_t) {
    mappedInput.update();
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested_ = true;
    }
  };

  NetworkStartup::prepare(renderer);
  const auto result = HttpDownloader::downloadToFile(url, destination, std::move(progress), &cancelRequested_);
  if (result == HttpDownloader::ABORTED) return DownloadResult::Cancelled;
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Download failed: %s (%d)", fileName, result);
    errorMessage_ = std::string("Download failed: ") + fileName;
    return DownloadResult::Failed;
  }

  uint32_t actualCrc = 0;
  if (!computeFileCrc32(downloadPath, actualCrc)) {
    LOG_ERR("FONT", "Failed to open file for CRC check: %s", downloadPath);
    Storage.remove(downloadPath);
    errorMessage_ = std::string("Failed to compute checksum: ") + fileName;
    return DownloadResult::Failed;
  }
  if (actualCrc != file.crc32) {
    LOG_ERR("FONT", "CRC32 mismatch for %s: got %08x expected %08x", fileName, actualCrc, file.crc32);
    Storage.remove(downloadPath);
    errorMessage_ = std::string("Checksum mismatch: ") + fileName;
    return DownloadResult::Failed;
  }
  LOG_DBG("FONT", "Downloaded %s (size=%zu crc32=%08x)", fileName, file.size, actualCrc);

  if (!fontInstaller_.validateCpfontFile(downloadPath)) {
    LOG_ERR("FONT", "Invalid .cpfont: %s", downloadPath);
    Storage.remove(downloadPath);
    errorMessage_ = std::string("Invalid font file: ") + fileName;
    return DownloadResult::Failed;
  }

  char backupPath[136];
  snprintf(backupPath, sizeof(backupPath), "%s.bak", destPath);
  Storage.remove(backupPath);
  const bool hadPrevious = Storage.exists(destPath);
  if ((hadPrevious && !Storage.rename(destPath, backupPath)) || !Storage.rename(downloadPath, destPath)) {
    if (hadPrevious && !Storage.exists(destPath)) Storage.rename(backupPath, destPath);
    Storage.remove(downloadPath);
    errorMessage_ = std::string("Failed to install font file: ") + fileName;
    return DownloadResult::Failed;
  }
  if (hadPrevious) Storage.remove(backupPath);
  currentFileIndex_++;
  return DownloadResult::Success;
}

FontDownloadActivity::DownloadResult FontDownloadActivity::downloadFamily(ManifestFamily& family) {
  const bool wasInstalled = family.installed;
  const bool hadUpdate = family.hasUpdate;
  auto restoreFamilyState = [&family, wasInstalled, hadUpdate] {
    family.installed = wasInstalled;
    family.hasUpdate = hadUpdate;
  };
  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloadingFamilyIndex_ = static_cast<int>(&family - families_.data());
    cancelRequested_ = false;
  }

  if (!fontInstaller_.ensureFamilyDir(family.name.c_str())) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to create font directory";
    return DownloadResult::Failed;
  }

  for (size_t i = 0; i < family.fileCount; ++i) {
    const auto result = downloadFile(family, files_[family.fileOffset + i]);
    if (result == DownloadResult::Success) continue;

    restoreFamilyState();
    if (result == DownloadResult::Cancelled) operation_ = DownloadOperation::None;
    RenderLock lock(*this);
    state_ = result == DownloadResult::Cancelled ? FAMILY_LIST : ERROR;
    return result;
  }

  fontInstaller_.refreshRegistry();
  family.installed = true;
  family.hasUpdate = false;

  return DownloadResult::Success;
}

void FontDownloadActivity::selectDownloadedFontAndPreview(const char* familyName) {
  auto textSettings = makeUniqueNoThrow<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                              TextSettingsActivity::Tab::Family,
                                                              TextSettingsActivity::InitialFontState::Changed);

  strncpy(SETTINGS.sdFontFamilyName, familyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  SETTINGS.sdFontFlashPreload = 0;
  SETTINGS.saveToFile();
  selectionUpdated_ = true;
  accelerationCompleted_ = false;

  if (!textSettings) {
    LOG_ERR("FONT", "OOM allocating TextSettingsActivity (%zu bytes)", sizeof(TextSettingsActivity));
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = tr(STR_MEMORY_ERROR);
    operation_ = DownloadOperation::None;
    return;
  }

  {
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer, false);
    state_ = SELECTING_FONT;
  }
  startActivityForResult(std::move(textSettings), [this](const ActivityResult& result) {
    RenderLock lock(*this);
    accelerationCompleted_ = !result.isCancelled && SETTINGS.sdFontFamilyName[0] != '\0';
    state_ = COMPLETE;
    operation_ = DownloadOperation::None;
    renderer.requestNextFullRefresh();
  });
}

void FontDownloadActivity::retryDownloadOperation() {
  currentFileIndex_ = 0;
  currentFileTotal_ = 0;
  switch (operation_) {
    case DownloadOperation::Single:
      if (downloadingFamilyIndex_ >= 0 && downloadingFamilyIndex_ < static_cast<int>(families_.size())) {
        currentFileTotal_ = families_[downloadingFamilyIndex_].fileCount;
      }
      downloadSingle(downloadingFamilyIndex_);
      return;
    case DownloadOperation::DownloadAll:
      for (const auto& family : families_) {
        if (!family.installed) currentFileTotal_ += family.fileCount;
      }
      downloadAll();
      return;
    case DownloadOperation::UpdateAll:
      for (const auto& family : families_) {
        if (family.hasUpdate) currentFileTotal_ += family.fileCount;
      }
      updateAll();
      return;
    case DownloadOperation::None:
      onWifiSelectionComplete(true);
      return;
  }
}

void FontDownloadActivity::promptDeleteSelectedFamily() {
  const int pendingDeleteFamilyIndex = familyIndexFromList(selectedIndex_);
  if (pendingDeleteFamilyIndex < 0 || pendingDeleteFamilyIndex >= static_cast<int>(families_.size())) {
    return;
  }

  std::string heading = tr(STR_DELETE);
  const auto& family = families_[pendingDeleteFamilyIndex];
  std::string body = family.name;
  // ActivityManager owns the dialog across frames, so it must live on the heap.
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, heading, body);
  if (!confirmation) {
    LOG_ERR("FONT", "OOM allocating ConfirmationActivity (%zu bytes)", sizeof(ConfirmationActivity));
    return;
  }
  startActivityForResult(std::move(confirmation),
                         [this](const ActivityResult& result) { onDeleteConfirmationResult(result); });
}

void FontDownloadActivity::onDeleteConfirmationResult(const ActivityResult& result) {
  waitForConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  if (result.isCancelled) {
    requestUpdate();
    return;
  }

  auto& family = families_[familyIndexFromList(selectedIndex_)];

  if (fontInstaller_.deleteFamily(family.name.c_str()) != FontInstaller::Error::OK) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to delete font";
  } else {
    fontInstaller_.refreshRegistry();
    family.installed = false;
    family.hasUpdate = false;
  }

  requestUpdate();
}

bool FontDownloadActivity::isSelectedFamilyDeletable() const {
  if (isDownloadAllRow(selectedIndex_) || isUpdateAllRow(selectedIndex_)) return false;
  if (selectedIndex_ < specialRowCount() || selectedIndex_ >= listItemCount()) return false;
  const auto& family = families_[familyIndexFromList(selectedIndex_)];
  return family.installed && !family.hasUpdate;
}

// --- Input handling ---

void FontDownloadActivity::loop() {
  if (waitForConfirmRelease_) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      waitForConfirmRelease_ = false;
    }
    return;
  }

  if (state_ == FAMILY_LIST) {
    auto activateSelected = [this] {
      if (families_.empty()) return;
      if (isDownloadAllRow(selectedIndex_)) {
        operation_ = DownloadOperation::DownloadAll;
        selectionUpdated_ = false;
        accelerationCompleted_ = false;
        currentFileIndex_ = 0;
        currentFileTotal_ = 0;
        for (const auto& f : families_) {
          if (!f.installed) currentFileTotal_ += f.fileCount;
        }
        downloadAll();
      } else if (isUpdateAllRow(selectedIndex_)) {
        operation_ = DownloadOperation::UpdateAll;
        selectionUpdated_ = false;
        accelerationCompleted_ = false;
        currentFileIndex_ = 0;
        currentFileTotal_ = 0;
        for (const auto& f : families_) {
          if (f.hasUpdate) currentFileTotal_ += f.fileCount;
        }
        updateAll();
      } else {
        auto& family = families_[familyIndexFromList(selectedIndex_)];
        if (!family.installed || family.hasUpdate) {
          operation_ = DownloadOperation::Single;
          selectionUpdated_ = false;
          accelerationCompleted_ = false;
          currentFileIndex_ = 0;
          currentFileTotal_ = family.fileCount;
          downloadSingle(familyIndexFromList(selectedIndex_));
        } else {
          promptDeleteSelectedFamily();
          return;
        }
      }
      requestUpdateAndWait();
    };

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    const int listSize = listItemCount();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect content =
        SubpageLayout::contentRect(UITheme::getInstance().getScreenSafeArea(renderer, true, false), metrics);
    const int pageItems = GUI.getListPageItems(content.height, true);

    if (!families_.empty()) {
      switch (handleListTouch(selectedIndex_, listSize, content.y, content.height, true)) {
        case ListTouchResult::Activated:
          activateSelected();
          return;
        case ListTouchResult::Consumed:
          return;
        case ListTouchResult::None:
          break;
      }

      const auto swipe = mappedInput.wasSwipe();
      if (swipe == MappedInputManager::SwipeDir::Up) {
        selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
        requestUpdate();
        return;
      }
      if (swipe == MappedInputManager::SwipeDir::Down) {
        selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
        requestUpdate();
        return;
      }
    }

    buttonNavigator_.onNextRelease([this, listSize] {
      selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
      requestUpdate();
    });

    buttonNavigator_.onPreviousRelease([this, listSize] {
      selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
      requestUpdate();
    });

    buttonNavigator_.onNextContinuous([this, listSize, pageItems] {
      selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
      requestUpdate();
    });

    buttonNavigator_.onPreviousContinuous([this, listSize, pageItems] {
      selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
      requestUpdate();
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activateSelected();
      return;
    }
  } else if (state_ == COMPLETE) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      {
        RenderLock lock(*this);
        state_ = FAMILY_LIST;
        operation_ = DownloadOperation::None;
      }
      requestUpdate();
    }
  } else if (state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      {
        RenderLock lock(*this);
        state_ = FAMILY_LIST;
        operation_ = DownloadOperation::None;
      }
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      retryDownloadOperation();
      requestUpdateAndWait();
      return;
    } else {
      int x = 0;
      int y = 0;
      if (mappedInput.wasScreenTapped(x, y)) {
        retryDownloadOperation();
        requestUpdateAndWait();
        return;
      }
    }
  }
}

// --- Rendering ---

std::string FontDownloadActivity::formatSize(size_t bytes) {
  char buf[32];
  if (bytes >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%zu B", bytes);
  }
  return buf;
}

void FontDownloadActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_FONT_BROWSER));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int relatedGap = SubpageLayout::relatedGap(metrics);
  const int sectionGap = SubpageLayout::sectionGap(metrics);
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);

  if (state_ == LOADING_MANIFEST) {
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                              tr(STR_LOADING_FONT_LIST));
  } else if (state_ == FAMILY_LIST) {
    if (families_.empty()) {
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                                tr(STR_NO_FONTS_AVAILABLE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      GUI.drawList(
          renderer, content, listItemCount(), selectedIndex_,
          [this](int index) -> std::string {
            if (isDownloadAllRow(index)) {
              return std::string(tr(STR_DOWNLOAD_ALL)) + " (" + formatSize(totalDownloadSize()) + ")";
            }
            if (isUpdateAllRow(index)) {
              return std::string(tr(STR_UPDATE_ALL)) + " (" + formatSize(totalUpdateSize()) + ")";
            }
            return families_[familyIndexFromList(index)].name;
          },
          [this](int index) -> std::string {
            if (isDownloadAllRow(index) || isUpdateAllRow(index)) return "";
            return families_[familyIndexFromList(index)].description;
          },
          nullptr,
          [this](int index) -> std::string {
            if (isDownloadAllRow(index) || isUpdateAllRow(index)) return "";
            const auto& f = families_[familyIndexFromList(index)];
            if (f.hasUpdate) return tr(STR_UPDATE_AVAILABLE);
            if (f.installed) return tr(STR_INSTALLED);
            return "";
          },
          true,
          [this](int index) -> bool {
            if (isDownloadAllRow(index) || isUpdateAllRow(index)) return false;
            const auto& f = families_[familyIndexFromList(index)];
            return f.installed && !f.hasUpdate;
          });

      const auto labels = mappedInput.mapLabels(tr(STR_BACK),
                                                isSelectedFamilyDeletable()      ? tr(STR_DELETE)
                                                : isUpdateAllRow(selectedIndex_) ? tr(STR_UPDATE)
                                                                                 : tr(STR_DOWNLOAD),
                                                tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else if (state_ == DOWNLOADING) {
    const auto& family = families_[downloadingFamilyIndex_];

    std::string statusText = std::string(tr(STR_DOWNLOADING)) + " " + family.name + " (" +
                             std::to_string(currentFileIndex_ + 1) + "/" + std::to_string(currentFileTotal_) + ")";
    const float progress =
        currentFileTotal_ == 0 ? 0 : static_cast<float>(currentFileIndex_) / static_cast<float>(currentFileTotal_);

    const int statusLines = renderer.getTextWidth(UI_10_FONT_ID, statusText.c_str()) <= textBounds.width ? 1 : 2;
    const int statusHeight = lineHeight * statusLines;
    const int blockHeight =
        statusHeight + sectionGap + GUI.measureProgressBarHeight(renderer, metrics.progressBarHeight);
    int y = SubpageLayout::centeredTop(content, blockHeight);
    UITheme::drawCenteredWrappedText(renderer, Rect{textBounds.x, y, textBounds.width, statusHeight}, UI_10_FONT_ID,
                                     statusText.c_str(), 2, true, EpdFontFamily::REGULAR,
                                     UITheme::TextVerticalAlignment::TOP);
    y += statusHeight + sectionGap;
    GUI.drawProgressBar(renderer, Rect{textBounds.x, y, textBounds.width, metrics.progressBarHeight},
                        static_cast<int>(progress * 100), 100);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == COMPLETE) {
    const char* detail = I18N.get(selectionUpdated_ ? StrId::STR_READER_FONT_SELECTION_UPDATED
                                                    : StrId::STR_READER_FONT_SELECTION_UNCHANGED);
    const char* finalLine = accelerationCompleted_ ? tr(STR_FONT_CACHE_READY)
                            : !selectionUpdated_   ? tr(STR_READER_FONT_SELECTION_PATH)
                                                   : nullptr;
    const int blockHeight = titleHeight + relatedGap + lineHeight + (finalLine ? relatedGap + lineHeight : 0);
    int y = SubpageLayout::centeredTop(content, blockHeight);
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y, tr(STR_FONT_INSTALLED), true,
                              EpdFontFamily::BOLD);
    y += titleHeight + relatedGap;
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, y, detail);
    if (accelerationCompleted_) {
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, y + lineHeight + relatedGap,
                                tr(STR_FONT_CACHE_READY));
    } else if (!selectionUpdated_) {
      UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, y + lineHeight + relatedGap,
                                tr(STR_READER_FONT_SELECTION_PATH));
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    const int detailHeight = errorMessage_.empty() ? 0 : lineHeight * 2;
    const int y = SubpageLayout::centeredTop(content, titleHeight + (detailHeight ? relatedGap + detailHeight : 0));
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y, tr(STR_FONT_INSTALL_FAILED), true,
                              EpdFontFamily::BOLD);
    if (!errorMessage_.empty()) {
      UITheme::drawCenteredWrappedText(
          renderer, Rect{textBounds.x, y + titleHeight + relatedGap, textBounds.width, detailHeight}, UI_10_FONT_ID,
          errorMessage_.c_str(), 2, true, EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
