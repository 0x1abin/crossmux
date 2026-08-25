#include "FontDownloadActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <atomic>
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
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace fui = freeink::ui;

namespace {

constexpr size_t kMaxManifestBytes = 64 * 1024;
constexpr size_t kMaxManifestFamilies = 32;
constexpr size_t kMaxManifestFiles = 128;
constexpr size_t kMaxFilesPerFamily = 16;
constexpr size_t kMaxFamilyNameBytes = 64;
constexpr size_t kMaxDescriptionBytes = 160;
constexpr size_t kMaxBaseUrlBytes = 256;
constexpr size_t kMaxFontFileBytes = 25 * 1024 * 1024;

#ifdef ENABLE_CHINESE_VERSION
std::atomic<bool> chineseFontPromptShownThisBoot{false};
#endif

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

}  // namespace

FontDownloadActivity::FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const Purpose purpose)
    : UiListActivity("FontDownload", renderer, mappedInput),
      purpose_(purpose),
      fontInstaller_(sdFontSystem.registry()) {}

#ifdef ENABLE_CHINESE_VERSION
bool FontDownloadActivity::wasChineseFontPromptShownThisBoot() {
  return chineseFontPromptShownThisBoot.load(std::memory_order_relaxed);
}

void FontDownloadActivity::suppressChineseFontPromptThisBoot() {
  chineseFontPromptShownThisBoot.store(true, std::memory_order_relaxed);
}
#endif

void FontDownloadActivity::activateIndex(const int index) {
  if (state_ != FAMILY_LIST) return;
  nav.selected = index;
  // Activation starts a download or opens the delete prompt; a lingering
  // flash would gray an unrelated row.
  app.clearTapFlash();
  activateSelected();  // ends with requestUpdateAndWait itself
}

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_CANCEL_DOWNLOAD, &FontDownloadActivity::onCancelDownload, this);
  app.on(ACTION_RETURN_TO_LIST, &FontDownloadActivity::onReturnToList, this);
  app.on(ACTION_RETRY_DOWNLOAD, &FontDownloadActivity::onRetryDownload, this);
  if (purpose_ == Purpose::ReaderAutoInstall) targetPointSize_ = SETTINGS.fontPointSize;
  if (purpose_ != Purpose::Manage) {
    auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_CHINESE_FONT_INCOMPLETE),
                                                                tr(STR_DOWNLOAD_FULL_CHINESE_FONT),
                                                                ConfirmationActivity::BodyPlacement::PopupTitle);
    if (!confirmation) {
      LOG_ERR("FONT", "OOM allocating ConfirmationActivity (%zu bytes)", sizeof(ConfirmationActivity));
#ifdef ENABLE_CHINESE_VERSION
      chineseFontPromptShownThisBoot.store(true, std::memory_order_relaxed);
#endif
      if (purpose_ == Purpose::ReaderAutoInstall) {
        finishAutomaticFlow(true);
      } else {
        finish();
      }
      return;
    }
#ifdef ENABLE_CHINESE_VERSION
    chineseFontPromptShownThisBoot.store(true, std::memory_order_relaxed);
#endif
    startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
      if (result.isCancelled) {
        if (purpose_ == Purpose::ReaderAutoInstall) {
          finishAutomaticFlow(true);
        } else {
          finish();
        }
      } else {
        startWifiSelection();
      }
    });
    return;
  }
  startWifiSelection();
}

void FontDownloadActivity::startWifiSelection() {
  if (!startActivityForResultWith<WifiSelectionActivity>(
          [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); })) {
    if (purpose_ == Purpose::ReaderAutoInstall) {
      finishAutomaticFlow(true);
    } else {
      finish();
    }
  }
}

void FontDownloadActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    if (returnToReader_) {
      silentRestartToReader(suppressPromptAfterRestart_);
    } else {
      silentRestart();
    }
  }
}

void FontDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    if (purpose_ == Purpose::ReaderAutoInstall) {
      finishAutomaticFlow(true);
    } else {
      finish();
    }
    return;
  }

  {
    RenderLock lock(*this);
    state_ = LOADING_MANIFEST;
  }
  requestUpdateAndWait();
  NetworkStartup::prepare(renderer);
  automaticError_ = AutomaticError::Download;

  if (!fetchAndParseManifest()) {
    {
      RenderLock lock(*this);
      state_ = ERROR;
    }
    return;
  }

  if (purpose_ == Purpose::ReaderAutoInstall) {
    startAutomaticDownload();
    return;
  }

  {
    RenderLock lock(*this);
    state_ = FAMILY_LIST;
    rowsDirty_ = true;  // families_ just loaded
    nav.selected = 0;
  }
}

bool FontDownloadActivity::startAutomaticDownload() {
  const auto familyIt = std::find_if(families_.begin(), families_.end(), [](const auto& family) {
    return family.name == SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY;
  });
  if (familyIt == families_.end()) {
    LOG_ERR("FONT", "Manifest does not contain required family %s",
            SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY);
    RenderLock lock(*this);
    state_ = ERROR;
    operation_ = DownloadOperation::None;
    automaticError_ = AutomaticError::FamilyMissing;
    return false;
  }

  const bool hasExactPointSize =
      std::any_of(files_.begin() + static_cast<ptrdiff_t>(familyIt->fileOffset),
                  files_.begin() + static_cast<ptrdiff_t>(familyIt->fileOffset + familyIt->fileCount),
                  [this](const auto& file) { return file.pointSize == targetPointSize_; });
  if (!hasExactPointSize) {
    LOG_ERR("FONT", "Required family %s does not contain point size %u",
            SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY, static_cast<unsigned>(targetPointSize_));
    RenderLock lock(*this);
    state_ = ERROR;
    operation_ = DownloadOperation::None;
    automaticError_ = AutomaticError::PointSizeMissing;
    return false;
  }

  downloadingFamilyIndex_ = static_cast<int>(familyIt - families_.begin());
  operation_ = DownloadOperation::Single;
  selectionUpdated_ = false;
  accelerationCompleted_ = false;
  currentFileIndex_ = 0;
  currentFileTotal_ = familyIt->fileCount;
  downloadSingle(downloadingFamilyIndex_);
  return true;
}

void FontDownloadActivity::finishAutomaticFlow(const bool suppressPrompt) {
  returnToReader_ = true;
  suppressPromptAfterRestart_ = suppressPrompt;
  finish();
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
    // Scope closes the local handle before the temp file is removed.
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

  if (manifestTooLarge) {
    LOG_ERR("FONT", "Manifest exceeds %zu bytes", kMaxManifestBytes);
    errorMessage_ = "Invalid font manifest";
    return false;
  }
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

    const JsonArray familyFiles = fObj["files"].as<JsonArray>();
    family.fileOffset = files_.size();
    family.fileCount = familyFiles.size();
    family.totalSize = 0;
    family.installed = fontInstaller_.isFamilyInstalled(family.name.c_str());
    for (JsonObject fileObj : familyFiles) {
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
    operation_ = DownloadOperation::None;
    renderer.requestNextFullRefresh();
  }
}

void FontDownloadActivity::downloadSingle(const int familyIndex) {
  if (familyIndex < 0 || familyIndex >= static_cast<int>(families_.size())) return;
  auto& family = families_[familyIndex];
  const DownloadResult result = downloadFamily(family);
  if (result == DownloadResult::Success) {
    selectDownloadedFontAndPreview(family.name.c_str());
  } else if (result == DownloadResult::Cancelled && purpose_ == Purpose::ReaderAutoInstall && !goHomeRequested_) {
    finishAutomaticFlow(true);
  }
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
    if (!Storage.openFileForRead("FONT", path, existing) || existing.fileSize() != file.size) return false;
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

  std::string url = baseUrl_ + fileName;
  NetworkStartup::prepare(renderer);
  const auto result = HttpDownloader::downloadToFile(
      url, downloadPath,
      [this](size_t downloaded, size_t total) {
        fileProgress_ = downloaded;
        fileTotal_ = total;
        mappedInput.update();
        if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
          cancelRequested_ = true;
        }
        // The callback owns this input snapshot while the main loop is blocked.
        if (mappedInput.wasHomeGesture()) {
          cancelRequested_ = true;
          goHomeRequested_ = true;
        }
        UiAppHost::routeTouch(mappedInput);
        requestUpdate(true);
      },
      &cancelRequested_);

  if (result == HttpDownloader::ABORTED) {
    Storage.remove(downloadPath);
    return DownloadResult::Cancelled;
  }
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Download failed: %s (%d)", fileName, result);
    Storage.remove(downloadPath);
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
  if (!fontInstaller_.validateCpfontFile(downloadPath)) {
    LOG_ERR("FONT", "Invalid .cpfont: %s", downloadPath);
    Storage.remove(downloadPath);
    errorMessage_ = std::string("Invalid font file: ") + fileName;
    return DownloadResult::Failed;
  }

  char backupPath[136];
  snprintf(backupPath, sizeof(backupPath), "%s.bak", destPath);
  if (Storage.exists(backupPath)) {
    if (Storage.exists(destPath)) {
      Storage.remove(backupPath);
    } else if (!Storage.rename(backupPath, destPath)) {
      errorMessage_ = std::string("Failed to recover font file: ") + fileName;
      return DownloadResult::Failed;
    }
  }
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
  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloadingFamilyIndex_ = static_cast<int>(&family - families_.data());
    fileProgress_ = 0;
    fileTotal_ = 0;
    cancelRequested_ = false;
    goHomeRequested_ = false;
  }
  requestUpdateAndWait();

  if (!fontInstaller_.ensureFamilyDir(family.name.c_str())) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to create font directory";
    return DownloadResult::Failed;
  }

  for (size_t i = 0; i < family.fileCount; i++) {
    const auto& file = files_[family.fileOffset + i];

    {
      RenderLock lock(*this);
      fileProgress_ = 0;
      fileTotal_ = file.size;
    }
    requestUpdateAndWait();

    const auto result = downloadFile(family, file);
    if (result != DownloadResult::Success) {
      family.installed = wasInstalled;
      family.hasUpdate = hadUpdate;
      if (result == DownloadResult::Cancelled) operation_ = DownloadOperation::None;
      if (goHomeRequested_) {
        onGoHome();
        return result;
      }
      RenderLock lock(*this);
      state_ = result == DownloadResult::Cancelled ? FAMILY_LIST : ERROR;
      rowsDirty_ = result == DownloadResult::Cancelled;
      return result;
    }
  }

  fontInstaller_.refreshRegistry();
  family.installed = true;
  family.hasUpdate = false;
  return DownloadResult::Success;
}

void FontDownloadActivity::selectDownloadedFontAndPreview(const char* familyName) {
  const auto startMode = purpose_ == Purpose::ReaderAutoInstall ? TextSettingsActivity::StartMode::PreloadThenExit
                                                                : TextSettingsActivity::StartMode::Interactive;
  auto textSettings = makeUniqueNoThrow<TextSettingsActivity>(
      renderer, mappedInput, &sdFontSystem.registry(), TextSettingsActivity::Tab::Family,
      TextSettingsActivity::InitialFontState::Changed, startMode);
  if (!textSettings) {
    LOG_ERR("FONT", "OOM allocating TextSettingsActivity (%zu bytes)", sizeof(TextSettingsActivity));
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = tr(STR_MEMORY_ERROR);
    operation_ = DownloadOperation::None;
    return;
  }

  char previousFamily[sizeof(SETTINGS.sdFontFamilyName)];
  strncpy(previousFamily, SETTINGS.sdFontFamilyName, sizeof(previousFamily));
  previousFamily[sizeof(previousFamily) - 1] = '\0';
  const uint8_t previousFlashPreload = SETTINGS.sdFontFlashPreload;
  strncpy(SETTINGS.sdFontFamilyName, familyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  SETTINGS.sdFontFlashPreload = 0;
  if (purpose_ == Purpose::ReaderAutoInstall && !SETTINGS.saveToFile()) {
    LOG_ERR("FONT", "Failed to save selected family %s", familyName);
    strncpy(SETTINGS.sdFontFamilyName, previousFamily, sizeof(SETTINGS.sdFontFamilyName));
    SETTINGS.sdFontFlashPreload = previousFlashPreload;
    RenderLock lock(*this);
    state_ = ERROR;
    automaticError_ = AutomaticError::SettingsSave;
    return;
  }
  if (purpose_ != Purpose::ReaderAutoInstall) SETTINGS.saveToFile();
  selectionUpdated_ = true;
  accelerationCompleted_ = false;
  {
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer, false);
    if (purpose_ == Purpose::ReaderAutoInstall &&
        (SETTINGS.sdFontFamilyName[0] == '\0' || SETTINGS.fontPointSize != targetPointSize_)) {
      LOG_ERR("FONT", "Failed to load selected family %s at point size %u", familyName,
              static_cast<unsigned>(targetPointSize_));
      strncpy(SETTINGS.sdFontFamilyName, previousFamily, sizeof(SETTINGS.sdFontFamilyName));
      SETTINGS.sdFontFlashPreload = previousFlashPreload;
      state_ = ERROR;
      automaticError_ = SETTINGS.saveToFile() ? AutomaticError::FontLoad : AutomaticError::SettingsSave;
      return;
    }
    state_ = SELECTING_FONT;
  }
  startActivityForResult(std::move(textSettings), [this](const ActivityResult& result) {
    if (purpose_ == Purpose::ReaderAutoInstall) {
      if (strcmp(SETTINGS.sdFontFamilyName, SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY) != 0 ||
          SETTINGS.fontPointSize != targetPointSize_) {
        LOG_ERR("FONT", "Automatic font selection was not retained");
        RenderLock lock(*this);
        state_ = ERROR;
        automaticError_ = AutomaticError::FontLoad;
        return;
      }
      if (!SETTINGS.saveToFile()) {
        LOG_ERR("FONT", "Failed to save automatic font preload state");
        RenderLock lock(*this);
        state_ = ERROR;
        automaticError_ = AutomaticError::SettingsSave;
        return;
      }
      accelerationCompleted_ = !result.isCancelled;
      finishAutomaticFlow(false);
      return;
    }
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
  const int pendingDeleteFamilyIndex = familyIndexFromList(nav.selected);
  if (pendingDeleteFamilyIndex < 0 || pendingDeleteFamilyIndex >= static_cast<int>(families_.size())) {
    return;
  }

  std::string heading = tr(STR_DELETE);
  const auto& family = families_[pendingDeleteFamilyIndex];
  std::string body = family.name;
  startActivityForResultWith<ConfirmationActivity>(
      [this](const ActivityResult& result) { onDeleteConfirmationResult(result); }, heading, body);
}

void FontDownloadActivity::onDeleteConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    requestUpdate();
    return;
  }

  auto& family = families_[familyIndexFromList(nav.selected)];

  if (fontInstaller_.deleteFamily(family.name.c_str()) != FontInstaller::Error::OK) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to delete font";
  } else {
    fontInstaller_.refreshRegistry();
    family.installed = false;
    family.hasUpdate = false;
    // Unlike the other family_ mutations, this one stays in FAMILY_LIST (no
    // state_ transition to hang the rebuild off), so it must set the flag
    // directly.
    rowsDirty_ = true;
  }

  requestUpdate();
}

bool FontDownloadActivity::isSelectedFamilyDeletable() const {
  if (isDownloadAllRow(nav.selected) || isUpdateAllRow(nav.selected)) return false;
  if (nav.selected < specialRowCount() || nav.selected >= listItemCount()) return false;
  const auto& family = families_[familyIndexFromList(nav.selected)];
  return family.installed && !family.hasUpdate;
}

void FontDownloadActivity::activateSelected() {
  if (families_.empty()) return;
  if (isDownloadAllRow(nav.selected)) {
    operation_ = DownloadOperation::DownloadAll;
    selectionUpdated_ = false;
    accelerationCompleted_ = false;
    currentFileIndex_ = 0;
    currentFileTotal_ = 0;
    for (const auto& f : families_) {
      if (!f.installed) currentFileTotal_ += f.fileCount;
    }
    downloadAll();
  } else if (isUpdateAllRow(nav.selected)) {
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
    // The special rows disappear when a download starts, so a stale selection
    // can map past the family table.
    const int familyIndex = familyIndexFromList(nav.selected);
    if (familyIndex < 0 || familyIndex >= static_cast<int>(families_.size())) return;
    auto& family = families_[familyIndex];
    if (!family.installed || family.hasUpdate) {
      operation_ = DownloadOperation::Single;
      selectionUpdated_ = false;
      accelerationCompleted_ = false;
      currentFileIndex_ = 0;
      currentFileTotal_ = family.fileCount;
      downloadSingle(familyIndex);
    } else {
      operation_ = DownloadOperation::None;
      promptDeleteSelectedFamily();
      return;
    }
  }
  requestUpdateAndWait();
}

void FontDownloadActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (state_ != FAMILY_LIST) {
    if (!mappedInput.hasTouch()) return;
    fui::FooterAction actions[2];
    uint8_t count = 0;
    switch (state_) {
      case DOWNLOADING:
        actions[count++] = {tr(STR_CANCEL), ACTION_CANCEL_DOWNLOAD};
        break;
      case COMPLETE:
        actions[count++] = {tr(STR_BACK), ACTION_RETURN_TO_LIST};
        break;
      case ERROR:
        actions[count++] = {tr(STR_BACK), ACTION_RETURN_TO_LIST};
        actions[count++] = {tr(STR_RETRY), ACTION_RETRY_DOWNLOAD};
        break;
      default:
        break;
    }
    if (count > 0) screen.footer(actions, count);
    return;
  }

  if (families_.empty()) {
    screen.centeredText(tr(STR_NO_FONTS_AVAILABLE), screen.theme().bodyText);
    return;
  }

  if (rowsDirty_) {
    rebuildRowItems();
    rowsDirty_ = false;
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the status and the row edge
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void FontDownloadActivity::onCancelDownload(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<FontDownloadActivity*>(user);
  if (self->state_ == DOWNLOADING) self->cancelRequested_ = true;
}

void FontDownloadActivity::returnToFamilyList() {
  closeRouting();
  {
    RenderLock lock(*this);
    state_ = FAMILY_LIST;
    operation_ = DownloadOperation::None;
    rowsDirty_ = true;
  }
  requestUpdate();
}

void FontDownloadActivity::onReturnToList(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<FontDownloadActivity*>(user);
  if (self->state_ != COMPLETE && self->state_ != ERROR) return;
  self->app.clearTapFlash();
  if (self->purpose_ == Purpose::ReaderAutoInstall) {
    self->finishAutomaticFlow(self->state_ == ERROR);
  } else {
    self->returnToFamilyList();
  }
}

void FontDownloadActivity::onRetryDownload(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<FontDownloadActivity*>(user);
  if (self->state_ != ERROR) return;
  self->app.clearTapFlash();
  self->closeRouting();
  self->retryDownloadOperation();
  self->requestUpdateAndWait();
}

// Rebuilds rowLabels_/rowItems_ from families_. Only called when rowsDirty_ is
// set (families_/state_ changed since the last build), never on every repaint.
void FontDownloadActivity::rebuildRowItems() {
  const int listSize = listItemCount();
  rowLabels_.assign(listSize, std::string());
  rowItems_.clear();
  rowItems_.reserve(listSize);
  for (int i = 0; i < listSize; i++) {
    fui::ListItem item;
    if (isDownloadAllRow(i)) {
      rowLabels_[i] = std::string(tr(STR_DOWNLOAD_ALL)) + " (" + formatSize(totalDownloadSize()) + ")";
      item.label = rowLabels_[i].c_str();
    } else if (isUpdateAllRow(i)) {
      rowLabels_[i] = std::string(tr(STR_UPDATE_ALL)) + " (" + formatSize(totalUpdateSize()) + ")";
      item.label = rowLabels_[i].c_str();
    } else {
      const auto& family = families_[familyIndexFromList(i)];
      item.label = family.name.c_str();
      if (!family.description.empty()) item.subtitle = family.description.c_str();
      if (family.hasUpdate) {
        item.value = tr(STR_UPDATE_AVAILABLE);
      } else if (family.installed) {
        item.value = tr(STR_INSTALLED);
        // Dimmed but still tappable (opens the delete prompt): visual-only
        // disabled state, the row stays enabled for hit registration.
        item.state = fui::StateDisabled;
      }
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

// --- Input handling ---

bool FontDownloadActivity::handleCustomInput() {
  if (state_ == FAMILY_LIST) {
    // The base list protocol (Back/Confirm, touch routing, swipe scroll,
    // button navigation) handles this state.
    return false;
  }

  const auto touch = UiAppHost::routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return true;

  if (state_ == COMPLETE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (purpose_ == Purpose::ReaderAutoInstall) {
        finishAutomaticFlow(false);
      } else {
        returnToFamilyList();
      }
    }
  } else if (state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      if (purpose_ == Purpose::ReaderAutoInstall) {
        finishAutomaticFlow(true);
      } else {
        returnToFamilyList();
      }
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      closeRouting();
      retryDownloadOperation();
      requestUpdateAndWait();
      return true;
    }
  }

  return true;
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

const char* FontDownloadActivity::automaticErrorText() const {
  switch (automaticError_) {
    case AutomaticError::Download:
      return tr(STR_AUTO_FONT_DOWNLOAD_FAILED);
    case AutomaticError::FamilyMissing:
      return tr(STR_AUTO_FONT_FAMILY_MISSING);
    case AutomaticError::PointSizeMissing:
      return tr(STR_AUTO_FONT_SIZE_MISSING);
    case AutomaticError::SettingsSave:
      return tr(STR_AUTO_FONT_SETTINGS_SAVE_FAILED);
    case AutomaticError::FontLoad:
      return tr(STR_AUTO_FONT_LOAD_FAILED);
  }
  return tr(STR_AUTO_FONT_DOWNLOAD_FAILED);
}

void FontDownloadActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FONT_BROWSER));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto centerY = (pageHeight - lineHeight) / 2;

  if (state_ == LOADING_MANIFEST) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_LOADING_FONT_LIST));
  } else if (state_ == FAMILY_LIST) {
    renderUi();

    const char* confirmLabel = families_.empty()              ? ""
                               : isSelectedFamilyDeletable()  ? tr(STR_DELETE)
                               : isUpdateAllRow(nav.selected) ? tr(STR_UPDATE)
                                                              : tr(STR_DOWNLOAD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, families_.empty() ? "" : tr(STR_DIR_UP),
                                              families_.empty() ? "" : tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == DOWNLOADING) {
    const auto& family = families_[downloadingFamilyIndex_];

    std::string statusText = std::string(tr(STR_DOWNLOADING)) + " " + family.name + " (" +
                             std::to_string(currentFileIndex_ + 1) + "/" + std::to_string(currentFileTotal_) + ")";
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, statusText.c_str());

    float progress = 0;
    if (fileTotal_ > 0) {
      progress = static_cast<float>(fileProgress_) / static_cast<float>(fileTotal_);
    }

    int barY = centerY + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, barY, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(progress * 100), 100);

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == COMPLETE) {
    const char* detail = I18N.get(selectionUpdated_ ? StrId::STR_READER_FONT_SELECTION_UPDATED
                                                    : StrId::STR_READER_FONT_SELECTION_UNCHANGED);
    const char* finalLine = accelerationCompleted_ ? tr(STR_FONT_CACHE_READY)
                            : !selectionUpdated_   ? tr(STR_READER_FONT_SELECTION_PATH)
                                                   : nullptr;
    const int detailY = centerY;
    renderer.drawCenteredText(UI_12_FONT_ID, detailY - lineHeight - metrics.verticalSpacing, tr(STR_FONT_INSTALLED),
                              true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, detailY, detail);
    if (finalLine) {
      renderer.drawCenteredText(UI_10_FONT_ID, detailY + lineHeight + metrics.verticalSpacing, finalLine);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_FONT_INSTALL_FAILED), true,
                              EpdFontFamily::BOLD);
    const char* detail = purpose_ == Purpose::ReaderAutoInstall ? automaticErrorText() : errorMessage_.c_str();
    if (detail[0] != '\0') {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + metrics.verticalSpacing, detail);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  if (state_ == DOWNLOADING || state_ == COMPLETE || state_ == ERROR) renderUi();

  renderer.displayBuffer();
}
