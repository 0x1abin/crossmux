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
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/CrossMuxEndpoints.h"
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
constexpr uint8_t kDownloadProgressStepPercent = 10;

constexpr uint8_t downloadProgressPercent(const size_t completed, const size_t total) {
  if (total == 0) return 0;
  const size_t boundedCompleted = completed < total ? completed : total;
  return static_cast<uint8_t>(static_cast<uint64_t>(boundedCompleted) * 100 / total);
}

constexpr bool shouldRefreshDownloadProgress(const uint8_t previous, const uint8_t current) {
  return current < 100 && current >= previous + kDownloadProgressStepPercent;
}

static_assert(downloadProgressPercent(9, 100) == 9);
static_assert(downloadProgressPercent(110, 100) == 100);
static_assert(!shouldRefreshDownloadProgress(0, 9));
static_assert(shouldRefreshDownloadProgress(0, 10));
static_assert(!shouldRefreshDownloadProgress(90, 100));  // The verified final frame is drawn synchronously.

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
                                           const Purpose purpose, const StartMode startMode)
    : UiListActivity("FontDownload", renderer, mappedInput),
      purpose_(purpose),
      startMode_(startMode),
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
  switch (state_) {
    case GROUP_LIST:
      app.clearTapFlash();
      enterGroup(index);
      requestUpdate();
      return;
    case FAMILY_LIST:
      nav.selected = index;
      // Activation starts a download or opens the delete prompt; a lingering
      // flash would gray an unrelated row.
      app.clearTapFlash();
      activateSelected();  // ends with requestUpdateAndWait itself
      return;
    case WIFI_SELECTION:
    case LOADING_MANIFEST:
    case DOWNLOADING:
    case COMPLETE:
    case ERROR:
      return;
  }
}

fui::ListNav& FontDownloadActivity::activeNav() { return state_ == GROUP_LIST ? groupNav_ : nav; }

void FontDownloadActivity::onBackButton() {
  if (state_ != FAMILY_LIST || !hasGroupScreen()) {
    finish();
    return;
  }

  closeRouting();
  {
    RenderLock lock(*this);
    state_ = GROUP_LIST;
    rowsDirty_ = true;
  }
  requestUpdate();
}

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_CANCEL_DOWNLOAD, &FontDownloadActivity::onCancelDownload, this);
  app.on(ACTION_RETURN_TO_LIST, &FontDownloadActivity::onReturnToList, this);
  app.on(ACTION_RETRY_DOWNLOAD, &FontDownloadActivity::onRetryDownload, this);
  if (purpose_ == Purpose::ReaderAutoInstall) targetPointSize_ = SETTINGS.fontPointSize;
  if (startMode_ == StartMode::ResumeFontLoadError) {
#ifdef ENABLE_CHINESE_VERSION
    suppressChineseFontPromptThisBoot();
#endif
    state_ = ERROR;
    operation_ = DownloadOperation::None;
    automaticError_ = AutomaticError::FontLoad;
    requestUpdate();
    return;
  }
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
        finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
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
          finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
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
      finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
    } else {
      finish();
    }
  }
}

void FontDownloadActivity::onExit() {
  Activity::onExit();

  const bool wifiWasEnabled = WiFi.getMode() != WIFI_MODE_NULL;
  if (wifiWasEnabled) {
    WiFi.disconnect(false);
    delay(30);
  }
  if (exitRoute_ == ExitRoute::ReaderPreloadChineseFont) {
    LOG_INF("FONT", "Network phase complete: free=%u, maxAlloc=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
  if (!wifiWasEnabled && exitRoute_ != ExitRoute::ReaderPreloadChineseFont) return;

  switch (exitRoute_) {
    case ExitRoute::Home:
      silentRestart();
      return;
    case ExitRoute::Reader:
      silentRestartToReader();
      return;
    case ExitRoute::ReaderSuppressPrompt:
      silentRestartToReader(true);
      return;
    case ExitRoute::ReaderPreloadChineseFont:
      silentRestartToReaderAndPreloadChineseFont(targetPointSize_);
      return;
  }
}

void FontDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    if (purpose_ == Purpose::ReaderAutoInstall) {
      finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
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
  if (!hasGroupScreen()) buildFilteredIndices(0);

  {
    RenderLock lock(*this);
    rowsDirty_ = true;  // families_ just loaded
    if (hasGroupScreen()) {
      groupNav_.reset();
      state_ = GROUP_LIST;
    } else {
      nav.reset();
      state_ = FAMILY_LIST;
    }
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
  resetDownloadProgress(familyIt->totalSize);
  downloadSingle(downloadingFamilyIndex_);
  return true;
}

void FontDownloadActivity::finishAutomaticFlow(const ExitRoute route) {
  exitRoute_ = route;
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

  char manifestUrl[160];
  const int manifestUrlLength =
      snprintf(manifestUrl, sizeof(manifestUrl), CrossMuxEndpoints::FONT_MANIFEST_FORMAT, CrossMuxEndpoints::host(),
               FONT_MANIFEST_URL_STRINGIFY(FONTS_MANIFEST_VERSION), FONT_MANIFEST_URL_STRINGIFY(CPFONT_VERSION));
  if (manifestUrlLength < 0 || static_cast<size_t>(manifestUrlLength) >= sizeof(manifestUrl)) {
    LOG_ERR("FONT", "Manifest URL exceeds %zu bytes", sizeof(manifestUrl));
    errorMessage_ = "Failed to fetch font list";
    return false;
  }
  auto result = HttpDownloader::downloadToFile(manifestUrl, MANIFEST_TMP, nullptr);
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Failed to fetch manifest from %s", manifestUrl);
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
  families_.clear();
  files_.clear();
  scriptGroupLabels_.clear();
  filteredIndices_.clear();
  fontInstaller_.refreshRegistry();

  JsonArray groupsArr = doc["scriptGroups"].as<JsonArray>();
  const size_t groupCount = std::min(groupsArr.size(), MAX_SCRIPT_GROUPS);
  scriptGroupLabels_.reserve(groupCount);
  if (groupsArr.size() > MAX_SCRIPT_GROUPS) {
    LOG_ERR("FONT", "Manifest declares more than %zu script groups; extra groups ignored", MAX_SCRIPT_GROUPS);
  }
  for (size_t groupIndex = 0; groupIndex < groupCount; groupIndex++) {
    JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
    const char* tag = groupObj["tag"] | "";
    const char* label = groupObj["label"] | "";
    if (*tag == '\0' || *label == '\0') {
      LOG_ERR("FONT", "Malformed script group at index %zu", groupIndex);
      errorMessage_ = "Invalid font manifest";
      return false;
    }
    scriptGroupLabels_.push_back(label);
  }

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
  filteredIndices_.reserve(familiesArr.size());

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
    for (JsonVariant script : fObj["scripts"].as<JsonArray>()) {
      const char* familyTag = script.as<const char*>();
      if (!familyTag) continue;
      for (size_t groupIndex = 0; groupIndex < scriptGroupLabels_.size(); groupIndex++) {
        JsonObject groupObj = groupsArr[groupIndex].as<JsonObject>();
        const char* groupTag = groupObj["tag"] | "";
        if (std::strcmp(familyTag, groupTag) == 0) {
          family.scriptMask |= uint32_t{1} << groupIndex;
          break;
        }
      }
    }
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

  const size_t rowCapacity = std::max(families_.size() + 2, scriptGroupLabels_.size() + 1);
  rowLabels_.reserve(rowCapacity);
  rowItems_.reserve(rowCapacity);

  LOG_DBG("FONT", "Manifest loaded: %zu families, %zu script groups", families_.size(), scriptGroupLabels_.size());
  return true;
}

// --- Download ---

void FontDownloadActivity::downloadAll() {
  cancelRequested_ = false;
  for (const int familyIndex : filteredIndices_) {
    if (families_[familyIndex].installed) continue;
    if (downloadFamily(families_[familyIndex]) != DownloadResult::Success) return;
  }

  finishDownloadProgress();

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
  for (const int familyIndex : filteredIndices_) {
    if (!families_[familyIndex].hasUpdate) continue;
    if (downloadFamily(families_[familyIndex]) != DownloadResult::Success) return;
  }

  finishDownloadProgress();

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
    finishDownloadProgress();
    selectDownloadedFontAndPreview(family.name.c_str());
  } else if (result == DownloadResult::Cancelled && purpose_ == Purpose::ReaderAutoInstall && !goHomeRequested_) {
    finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
  }
}

bool FontDownloadActivity::showDownloadAllRow() const {
  for (const int familyIndex : filteredIndices_) {
    if (!families_[familyIndex].installed) return true;
  }
  return false;
}

bool FontDownloadActivity::showUpdateAllRow() const {
  for (const int familyIndex : filteredIndices_) {
    if (families_[familyIndex].hasUpdate) return true;
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
  return filteredIndices_.empty() ? 0 : static_cast<int>(filteredIndices_.size()) + specialRowCount();
}

int FontDownloadActivity::listCount() const {
  switch (state_) {
    case GROUP_LIST:
      return groupListItemCount();
    case FAMILY_LIST:
      return listItemCount();
    case WIFI_SELECTION:
    case LOADING_MANIFEST:
    case DOWNLOADING:
    case COMPLETE:
    case ERROR:
      return 0;
  }
  return 0;
}

int FontDownloadActivity::familyIndexFromList(const int listIndex) const {
  const int filteredIndex = listIndex - specialRowCount();
  if (filteredIndex < 0 || filteredIndex >= static_cast<int>(filteredIndices_.size())) return -1;
  return filteredIndices_[filteredIndex];
}

int FontDownloadActivity::groupMemberCount(const int scriptGroupIndex) const {
  if (scriptGroupIndex < 0 || scriptGroupIndex >= static_cast<int>(scriptGroupLabels_.size())) return 0;
  const uint32_t groupBit = uint32_t{1} << scriptGroupIndex;
  int count = 0;
  for (const auto& family : families_) {
    if (family.scriptMask & groupBit) count++;
  }
  return count;
}

void FontDownloadActivity::buildFilteredIndices(const int groupListIndex) {
  filteredIndices_.clear();
  filteredIndices_.reserve(families_.size());
  if (groupListIndex <= 0) {
    for (int familyIndex = 0; familyIndex < static_cast<int>(families_.size()); familyIndex++) {
      filteredIndices_.push_back(familyIndex);
    }
    return;
  }

  const uint32_t groupBit = uint32_t{1} << (groupListIndex - 1);
  for (int familyIndex = 0; familyIndex < static_cast<int>(families_.size()); familyIndex++) {
    if (families_[familyIndex].scriptMask & groupBit) filteredIndices_.push_back(familyIndex);
  }
}

void FontDownloadActivity::enterGroup(const int groupListIndex) {
  closeRouting();
  buildFilteredIndices(groupListIndex);
  {
    RenderLock lock(*this);
    nav.reset();
    state_ = FAMILY_LIST;
    rowsDirty_ = true;
  }
}

size_t FontDownloadActivity::totalDownloadSize() const {
  size_t total = 0;
  for (const int familyIndex : filteredIndices_) {
    if (!families_[familyIndex].installed) total += families_[familyIndex].totalSize;
  }
  return total;
}

size_t FontDownloadActivity::totalUpdateSize() const {
  size_t total = 0;
  for (const int familyIndex : filteredIndices_) {
    if (families_[familyIndex].hasUpdate) total += families_[familyIndex].totalSize;
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

void FontDownloadActivity::resetDownloadProgress(const size_t total) {
  downloadProgress_ = 0;
  downloadTotal_ = total;
  lastProgressRefreshPercent_ = 0;
}

void FontDownloadActivity::updateDownloadProgress(const size_t completed) {
  downloadProgress_ = std::min(completed, downloadTotal_);
  const uint8_t percent = downloadProgressPercent(downloadProgress_, downloadTotal_);
  if (!shouldRefreshDownloadProgress(lastProgressRefreshPercent_, percent)) return;
  lastProgressRefreshPercent_ = percent;
  requestUpdate(true);
}

void FontDownloadActivity::finishDownloadProgress() {
  downloadProgress_ = downloadTotal_;
  lastProgressRefreshPercent_ = 100;
  requestUpdateAndWait();
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
  const size_t fileStartProgress = downloadProgress_;

  if (isVerifiedFontFile(destPath, file)) {
    Storage.remove(downloadPath);
    LOG_INF("FONT", "Skipping verified file: %s", fileName);
    currentFileIndex_++;
    updateDownloadProgress(fileStartProgress + file.size);
    return DownloadResult::Success;
  }

  std::string url = baseUrl_ + fileName;
  NetworkStartup::prepare(renderer);
  const auto result = HttpDownloader::downloadToFile(
      url, downloadPath,
      [this, fileStartProgress, fileSize = file.size](size_t downloaded, size_t) {
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
        updateDownloadProgress(fileStartProgress + std::min(downloaded, fileSize));
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
  updateDownloadProgress(fileStartProgress + file.size);
  return DownloadResult::Success;
}

FontDownloadActivity::DownloadResult FontDownloadActivity::downloadFamily(ManifestFamily& family) {
  const bool wasInstalled = family.installed;
  const bool hadUpdate = family.hasUpdate;
  const bool enteringDownload = state_ != DOWNLOADING;
  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloadingFamilyIndex_ = static_cast<int>(&family - families_.data());
    cancelRequested_ = false;
    goHomeRequested_ = false;
  }
  if (enteringDownload) requestUpdateAndWait();

  if (!fontInstaller_.ensureFamilyDir(family.name.c_str())) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to create font directory";
    return DownloadResult::Failed;
  }

  for (size_t i = 0; i < family.fileCount; i++) {
    const auto& file = files_[family.fileOffset + i];
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
  if (purpose_ == Purpose::ReaderAutoInstall) {
    char previousFamily[sizeof(SETTINGS.sdFontFamilyName)];
    strncpy(previousFamily, SETTINGS.sdFontFamilyName, sizeof(previousFamily));
    previousFamily[sizeof(previousFamily) - 1] = '\0';
    const uint8_t previousFlashPreload = SETTINGS.sdFontFlashPreload;

    strncpy(SETTINGS.sdFontFamilyName, familyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    SETTINGS.sdFontFlashPreload = 0;
    if (!SETTINGS.saveToFile()) {
      LOG_ERR("FONT", "Failed to save selected family %s", familyName);
      strncpy(SETTINGS.sdFontFamilyName, previousFamily, sizeof(SETTINGS.sdFontFamilyName));
      SETTINGS.sdFontFlashPreload = previousFlashPreload;
      RenderLock lock(*this);
      state_ = ERROR;
      automaticError_ = AutomaticError::SettingsSave;
      return;
    }

    selectionUpdated_ = true;
    accelerationCompleted_ = false;
    finishAutomaticFlow(ExitRoute::ReaderPreloadChineseFont);
    return;
  }

  auto textSettings = makeUniqueNoThrow<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                              TextSettingsActivity::Tab::Family,
                                                              TextSettingsActivity::InitialFontState::Changed);
  if (!textSettings) {
    LOG_ERR("FONT", "OOM allocating TextSettingsActivity (%zu bytes)", sizeof(TextSettingsActivity));
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = tr(STR_MEMORY_ERROR);
    operation_ = DownloadOperation::None;
    return;
  }

  strncpy(SETTINGS.sdFontFamilyName, familyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  SETTINGS.sdFontFlashPreload = 0;
  SETTINGS.saveToFile();
  selectionUpdated_ = true;
  accelerationCompleted_ = false;
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
        resetDownloadProgress(families_[downloadingFamilyIndex_].totalSize);
      }
      downloadSingle(downloadingFamilyIndex_);
      return;
    case DownloadOperation::DownloadAll:
      for (const auto& family : families_) {
        if (!family.installed) currentFileTotal_ += family.fileCount;
      }
      resetDownloadProgress(totalDownloadSize());
      downloadAll();
      return;
    case DownloadOperation::UpdateAll:
      for (const auto& family : families_) {
        if (family.hasUpdate) currentFileTotal_ += family.fileCount;
      }
      resetDownloadProgress(totalUpdateSize());
      updateAll();
      return;
    case DownloadOperation::None:
      if (WiFi.getMode() == WIFI_MODE_NULL) {
        startWifiSelection();
      } else {
        onWifiSelectionComplete(true);
      }
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

  const int familyIndex = familyIndexFromList(nav.selected);
  if (familyIndex < 0) {
    requestUpdate();
    return;
  }
  auto& family = families_[familyIndex];

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
  if (filteredIndices_.empty()) return;
  if (isDownloadAllRow(nav.selected)) {
    operation_ = DownloadOperation::DownloadAll;
    selectionUpdated_ = false;
    accelerationCompleted_ = false;
    currentFileIndex_ = 0;
    currentFileTotal_ = 0;
    for (const int familyIndex : filteredIndices_) {
      if (!families_[familyIndex].installed) currentFileTotal_ += families_[familyIndex].fileCount;
    }
    resetDownloadProgress(totalDownloadSize());
    downloadAll();
  } else if (isUpdateAllRow(nav.selected)) {
    operation_ = DownloadOperation::UpdateAll;
    selectionUpdated_ = false;
    accelerationCompleted_ = false;
    currentFileIndex_ = 0;
    currentFileTotal_ = 0;
    for (const int familyIndex : filteredIndices_) {
      if (families_[familyIndex].hasUpdate) currentFileTotal_ += families_[familyIndex].fileCount;
    }
    resetDownloadProgress(totalUpdateSize());
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
      resetDownloadProgress(family.totalSize);
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

  if (state_ == FAMILY_LIST && filteredIndices_.empty()) {
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
  syncListViewport(screen, props, /*hasSubtitle=*/state_ == FAMILY_LIST);
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
    self->finishAutomaticFlow(self->state_ == ERROR ? ExitRoute::ReaderSuppressPrompt : ExitRoute::Reader);
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
  switch (state_) {
    case GROUP_LIST:
      rebuildGroupRowItems();
      return;
    case FAMILY_LIST:
      rebuildFamilyRowItems();
      return;
    case WIFI_SELECTION:
    case LOADING_MANIFEST:
    case DOWNLOADING:
    case COMPLETE:
    case ERROR:
      rowLabels_.clear();
      rowItems_.clear();
      return;
  }
}

void FontDownloadActivity::rebuildGroupRowItems() {
  const int listSize = groupListItemCount();
  rowLabels_.assign(listSize, std::string());
  rowItems_.clear();
  rowItems_.reserve(listSize);
  for (int rowIndex = 0; rowIndex < listSize; rowIndex++) {
    fui::ListItem item;
    item.label = rowIndex == 0 ? tr(STR_ALL_FONTS) : scriptGroupLabels_[rowIndex - 1].c_str();
    const int memberCount = rowIndex == 0 ? static_cast<int>(families_.size()) : groupMemberCount(rowIndex - 1);
    rowLabels_[rowIndex] = std::to_string(memberCount);
    item.value = rowLabels_[rowIndex].c_str();
    item.actionValue = static_cast<int16_t>(rowIndex);
    rowItems_.push_back(item);
  }
}

void FontDownloadActivity::rebuildFamilyRowItems() {
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
  if (state_ == GROUP_LIST || state_ == FAMILY_LIST) {
    // The base list protocol (Back/Confirm, touch routing, swipe scroll,
    // button navigation) handles both list states.
    return false;
  }

  const auto touch = UiAppHost::routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return true;

  if (state_ == COMPLETE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (purpose_ == Purpose::ReaderAutoInstall) {
        finishAutomaticFlow(ExitRoute::Reader);
      } else {
        returnToFamilyList();
      }
    }
  } else if (state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      if (purpose_ == Purpose::ReaderAutoInstall) {
        finishAutomaticFlow(ExitRoute::ReaderSuppressPrompt);
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

  const char* headerSubtitle = nullptr;
  if (state_ == FAMILY_LIST && hasGroupScreen()) {
    const int scriptGroupIndex = groupNav_.selected - 1;
    headerSubtitle = scriptGroupIndex >= 0 && scriptGroupIndex < static_cast<int>(scriptGroupLabels_.size())
                         ? scriptGroupLabels_[scriptGroupIndex].c_str()
                         : tr(STR_ALL_FONTS);
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FONT_BROWSER),
                 headerSubtitle);

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto centerY = (pageHeight - lineHeight) / 2;

  if (state_ == LOADING_MANIFEST) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_LOADING_FONT_LIST));
  } else if (state_ == GROUP_LIST) {
    renderUi();
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == FAMILY_LIST) {
    renderUi();

    const bool hasVisibleFamilies = !filteredIndices_.empty();
    const char* confirmLabel = !hasVisibleFamilies            ? ""
                               : isSelectedFamilyDeletable()  ? tr(STR_DELETE)
                               : isUpdateAllRow(nav.selected) ? tr(STR_UPDATE)
                                                              : tr(STR_DOWNLOAD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, hasVisibleFamilies ? tr(STR_DIR_UP) : "",
                                              hasVisibleFamilies ? tr(STR_DIR_DOWN) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == DOWNLOADING) {
    const auto& family = families_[downloadingFamilyIndex_];

    const size_t displayedFile = std::min(currentFileIndex_ + 1, currentFileTotal_);
    std::string statusText = std::string(tr(STR_DOWNLOADING)) + " " + family.name + " (" +
                             std::to_string(displayedFile) + "/" + std::to_string(currentFileTotal_) + ")";
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const Rect content = SubpageLayout::contentRect(safeArea, metrics);
    const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
    const int sectionGap = SubpageLayout::sectionGap(metrics);
    const int blockHeight = lineHeight + sectionGap + GUI.measureProgressBarHeight(renderer, metrics.progressBarHeight);
    int y = SubpageLayout::centeredTop(content, blockHeight);
    UITheme::drawCenteredText(renderer, textBounds, UI_10_FONT_ID, y, statusText.c_str());
    y += lineHeight + sectionGap;
    GUI.drawProgressBar(renderer, Rect{textBounds.x, y, textBounds.width, metrics.progressBarHeight}, downloadProgress_,
                        downloadTotal_);

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
