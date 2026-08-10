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
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
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

constexpr uint32_t kProgressRefreshIntervalMs = 2000;

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
  WiFi.mode(WIFI_STA);
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
    sdFontSystem.releaseLoadedFont(renderer);
    state_ = LOADING_MANIFEST;
  }
  requestUpdateAndWait();

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
  downloadingFamilyIndex_ = -1;

  auto result = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, nullptr);
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Failed to fetch manifest from %s", FONT_MANIFEST_URL);
    errorMessage_ = "Failed to fetch font list";
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  // HTTP client is now closed — TLS buffers freed. Parse JSON from file.
  HalFile manifestFile;
  if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
    LOG_ERR("FONT", "Failed to open temp manifest");
    Storage.remove(MANIFEST_TMP);
    errorMessage_ = "Failed to read font list";
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, manifestFile);
  manifestFile.close();
  Storage.remove(MANIFEST_TMP);

  if (err) {
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
  if (baseUrl_.empty()) {
    LOG_ERR("FONT", "Manifest has no baseUrl");
    errorMessage_ = "Invalid font manifest";
    return false;
  }
  fontInstaller_.refreshRegistry();

  JsonArray familiesArr = doc["families"].as<JsonArray>();
  families_.reserve(familiesArr.size());

  for (JsonObject fObj : familiesArr) {
    ManifestFamily family;
    family.name = fObj["name"] | "";
    family.displayName = fObj["displayName"] | "";
    family.description = fObj["description"] | "";
    if (!FontInstaller::isValidFamilyName(family.name.c_str())) {
      LOG_ERR("FONT", "Malformed manifest family name: %s", family.name.c_str());
      families_.clear();
      errorMessage_ = "Invalid font manifest";
      return false;
    }
    if (!FontInstaller::isValidDisplayName(family.displayName.c_str())) {
      family.displayName = family.name;
    }

    const JsonArray stylesArr = fObj["styles"].as<JsonArray>();
    family.styles.reserve(stylesArr.size());
    for (JsonVariant s : stylesArr) {
      family.styles.push_back(s.as<std::string>());
    }

    family.totalSize = 0;
    const JsonArray filesArr = fObj["files"].as<JsonArray>();
    if (filesArr.isNull() || filesArr.size() == 0) {
      LOG_ERR("FONT", "Manifest family has no files: %s", family.name.c_str());
      families_.clear();
      errorMessage_ = "Invalid font manifest";
      return false;
    }
    family.files.reserve(filesArr.size());
    for (JsonObject fileObj : filesArr) {
      ManifestFile file;
      file.name = fileObj["name"] | "";
      file.size = fileObj["size"] | 0;

      if (!FontInstaller::isValidCpfontFilename(file.name.c_str()) || file.size == 0 ||
          !fileObj["crc32"].is<uint32_t>()) {
        LOG_ERR("FONT", "Malformed manifest file entry: %s", file.name.c_str());
        families_.clear();
        errorMessage_ = "Invalid font manifest";
        return false;
      }
      file.crc32 = fileObj["crc32"].as<uint32_t>();

      family.totalSize += file.size;
      family.files.push_back(std::move(file));
    }

    family.installed = fontInstaller_.isFamilyInstalled(family.name.c_str());
    if (family.installed && family.displayName != family.name &&
        FontInstaller::readDisplayName(family.name) == family.name &&
        !fontInstaller_.writeDisplayName(family.name.c_str(), family.displayName.c_str())) {
      LOG_ERR("FONT", "Failed to backfill display name for %s", family.name.c_str());
    }

    // Detect updates by comparing manifest file sizes with files on disk.
    // Not a checksum, but a size mismatch reliably indicates a rebuild in practice.
    if (family.installed) {
      for (const auto& file : family.files) {
        char path[128];
        FontInstaller::buildFontPath(family.name.c_str(), file.name.c_str(), path, sizeof(path));
        HalFile f;
        if (Storage.openFileForRead("FONT", path, f)) {
          size_t actual = f.fileSize();
          f.close();
          if (actual != file.size) {
            family.hasUpdate = true;
            break;
          }
        } else {
          // File missing on disk but family dir exists — treat as update
          family.hasUpdate = true;
          break;
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

FontDownloadActivity::DownloadResult FontDownloadActivity::downloadFamily(ManifestFamily& family) {
  const bool wasInstalled = family.installed;
  auto discardIncompleteFamily = [this, &family, wasInstalled] {
    if (!wasInstalled) fontInstaller_.deleteFamily(family.name.c_str());
    family.installed = wasInstalled;
    family.hasUpdate = wasInstalled;
  };
  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloadingFamilyIndex_ = static_cast<int>(&family - families_.data());
    fileProgress_ = 0;
    fileTotal_ = 0;
    cancelRequested_ = false;
  }
  requestUpdateAndWait();

  if (!fontInstaller_.ensureFamilyDir(family.name.c_str())) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to create font directory";
    return DownloadResult::Failed;
  }

  for (size_t i = 0; i < family.files.size(); i++) {
    const auto& file = family.files[i];

    {
      RenderLock lock(*this);
      fileProgress_ = 0;
      fileTotal_ = file.size;
    }
    requestUpdateAndWait();

    char destPath[128];
    FontInstaller::buildFontPath(family.name.c_str(), file.name.c_str(), destPath, sizeof(destPath));
    char downloadPath[136];
    snprintf(downloadPath, sizeof(downloadPath), "%s.part", destPath);

    std::string url = baseUrl_ + file.name;
    uint32_t lastProgressRefreshAt = millis();

    auto result = HttpDownloader::downloadToFile(
        url, downloadPath,
        [this, &lastProgressRefreshAt](size_t downloaded, size_t total) {
          fileProgress_ = downloaded;
          fileTotal_ = total;
          mappedInput.update();
          if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
              mappedInput.wasPressed(MappedInputManager::Button::Back)) {
            cancelRequested_ = true;
          }
          const uint32_t now = millis();
          if (now - lastProgressRefreshAt >= kProgressRefreshIntervalMs) {
            lastProgressRefreshAt = now;
            requestUpdate(true);
          }
        },
        &cancelRequested_);

    if (result == HttpDownloader::ABORTED) {
      discardIncompleteFamily();
      {
        RenderLock lock(*this);
        state_ = FAMILY_LIST;
      }
      operation_ = DownloadOperation::None;
      return DownloadResult::Cancelled;
    }

    if (result != HttpDownloader::OK) {
      LOG_ERR("FONT", "Download failed: %s (%d)", file.name.c_str(), result);
      discardIncompleteFamily();
      RenderLock lock(*this);
      state_ = ERROR;
      errorMessage_ = "Download failed: " + file.name;
      return DownloadResult::Failed;
    }

    uint32_t actualCrc = 0;
    if (!computeFileCrc32(downloadPath, actualCrc)) {
      LOG_ERR("FONT", "Failed to open file for CRC check: %s", downloadPath);
      Storage.remove(downloadPath);
      discardIncompleteFamily();
      RenderLock lock(*this);
      state_ = ERROR;
      errorMessage_ = "Failed to compute checksum: " + file.name;
      return DownloadResult::Failed;
    }
    if (actualCrc != file.crc32) {
      LOG_ERR("FONT", "CRC32 mismatch for %s: got %08x expected %08x", file.name.c_str(), actualCrc, file.crc32);
      Storage.remove(downloadPath);
      discardIncompleteFamily();
      RenderLock lock(*this);
      state_ = ERROR;
      errorMessage_ = "Checksum mismatch: " + file.name;
      return DownloadResult::Failed;
    }
    LOG_DBG("FONT", "Downloaded %s (size=%zu crc32=%08x)", file.name.c_str(), file.size, actualCrc);

    if (!fontInstaller_.validateCpfontFile(downloadPath)) {
      LOG_ERR("FONT", "Invalid .cpfont: %s", downloadPath);
      Storage.remove(downloadPath);
      discardIncompleteFamily();
      RenderLock lock(*this);
      state_ = ERROR;
      errorMessage_ = "Invalid font file: " + file.name;
      return DownloadResult::Failed;
    }

    char backupPath[136];
    snprintf(backupPath, sizeof(backupPath), "%s.bak", destPath);
    Storage.remove(backupPath);
    const bool hadPrevious = Storage.exists(destPath);
    if ((hadPrevious && !Storage.rename(destPath, backupPath)) || !Storage.rename(downloadPath, destPath)) {
      if (hadPrevious && !Storage.exists(destPath)) Storage.rename(backupPath, destPath);
      Storage.remove(downloadPath);
      discardIncompleteFamily();
      RenderLock lock(*this);
      state_ = ERROR;
      errorMessage_ = "Failed to install font file: " + file.name;
      return DownloadResult::Failed;
    }
    if (hadPrevious) Storage.remove(backupPath);
    currentFileIndex_++;
  }

  if (family.displayName != family.name &&
      !fontInstaller_.writeDisplayName(family.name.c_str(), family.displayName.c_str())) {
    LOG_ERR("FONT", "Installed %s without display-name sidecar", family.name.c_str());
  }
  fontInstaller_.refreshRegistry();
  family.installed = true;
  family.hasUpdate = false;

  return DownloadResult::Success;
}

void FontDownloadActivity::selectDownloadedFontAndPreview(const char* familyName) {
  auto textSettings = makeUniqueNoThrow<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                              TextSettingsActivity::Tab::Family);

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
        currentFileTotal_ = families_[downloadingFamilyIndex_].files.size();
      }
      downloadSingle(downloadingFamilyIndex_);
      return;
    case DownloadOperation::DownloadAll:
      for (const auto& family : families_) {
        if (!family.installed) currentFileTotal_ += family.files.size();
      }
      downloadAll();
      return;
    case DownloadOperation::UpdateAll:
      for (const auto& family : families_) {
        if (family.hasUpdate) currentFileTotal_ += family.files.size();
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
  std::string body = displayNameFor(family);
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
          if (!f.installed) currentFileTotal_ += f.files.size();
        }
        downloadAll();
      } else if (isUpdateAllRow(selectedIndex_)) {
        operation_ = DownloadOperation::UpdateAll;
        selectionUpdated_ = false;
        accelerationCompleted_ = false;
        currentFileIndex_ = 0;
        currentFileTotal_ = 0;
        for (const auto& f : families_) {
          if (f.hasUpdate) currentFileTotal_ += f.files.size();
        }
        updateAll();
      } else {
        auto& family = families_[familyIndexFromList(selectedIndex_)];
        if (!family.installed || family.hasUpdate) {
          operation_ = DownloadOperation::Single;
          selectionUpdated_ = false;
          accelerationCompleted_ = false;
          currentFileIndex_ = 0;
          currentFileTotal_ = family.files.size();
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

const std::string& FontDownloadActivity::displayNameFor(const ManifestFamily& family) const {
  return renderer.canRenderText(UI_12_FONT_ID, family.displayName.c_str()) ? family.displayName : family.name;
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
            return displayNameFor(families_[familyIndexFromList(index)]);
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

    std::string statusText = std::string(tr(STR_DOWNLOADING)) + " " + displayNameFor(family) + " (" +
                             std::to_string(currentFileIndex_ + 1) + "/" + std::to_string(currentFileTotal_) + ")";
    float progress = 0;
    if (fileTotal_ > 0) {
      progress = static_cast<float>(fileProgress_) / static_cast<float>(fileTotal_);
    }

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
