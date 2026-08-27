#include "DictionaryDownloadActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/CrossMuxEndpoints.h"
#include "network/HttpDownloader.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* MANIFEST_TMP = "/dictionary_manifest.tmp";
constexpr const char* ROOT = "/.dictionaries";
constexpr uint32_t PROGRESS_REFRESH_MS = 2000;
constexpr size_t MAX_NAME_BYTES = 64;
constexpr size_t MAX_DESCRIPTION_BYTES = 160;
constexpr size_t MAX_MANIFEST_BYTES = 64 * 1024;

void buildPath(char* out, const size_t outSize, const char* prefix, const char* id) {
  snprintf(out, outSize, "/.dictionaries/%s%s", prefix, id);
}

bool hasSuffix(const std::string& value, const char* suffix) {
  const size_t suffixLength = strlen(suffix);
  return value.size() >= suffixLength && value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
}

int readMarkerRevision(const char* directory) {
  char markerPath[112];
  char markerData[32];
  snprintf(markerPath, sizeof(markerPath), "%s/.crossmux-resource", directory);
  return DictionaryResource::parseMarkerRevision(
      Storage.readFileToBuffer(markerPath, markerData, sizeof(markerData)) > 0 ? markerData : nullptr);
}

void logHeap(const char* phase) {
  const auto heap = HalSystem::getHeapInfo();
  LOG_INF("DICTDL", "%s heap free=%lu largest=%lu", phase, static_cast<unsigned long>(heap.freeBytes),
          static_cast<unsigned long>(heap.largestFreeBlockBytes));
}

}  // namespace

DictionaryDownloadActivity::DictionaryDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("DictionaryDownload", renderer, mappedInput), UiAppHost(renderer) {}

void DictionaryDownloadActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_CANCEL_DOWNLOAD, &DictionaryDownloadActivity::onCancelDownload, this);
  app.on(ACTION_RETURN_TO_LIST, &DictionaryDownloadActivity::onReturnToList, this);
  app.on(ACTION_RETRY_DOWNLOAD, &DictionaryDownloadActivity::onRetryDownload, this);
  app.setScreen(&DictionaryDownloadActivity::stateScreen, this);
  startWifiSelection();
}

void DictionaryDownloadActivity::startWifiSelection() {
  auto wifiSelection = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifiSelection) {
    LOG_ERR("DICTDL", "OOM allocating WifiSelectionActivity (%zu bytes)", sizeof(WifiSelectionActivity));
    finish();
    return;
  }

  startActivityForResult(std::move(wifiSelection),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void DictionaryDownloadActivity::onExit() {
  Activity::onExit();
  std::vector<ManifestItem>().swap(items_);
  std::string().swap(baseUrl_);
  logHeap("exit");
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void DictionaryDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }
  {
    RenderLock lock(*this);
    state_ = State::Loading;
  }
  requestUpdateAndWait();
  if (!fetchAndParseManifest()) {
    RenderLock lock(*this);
    state_ = State::Error;
    return;
  }
  {
    RenderLock lock(*this);
    selectedIndex_ = 0;
    operation_ = Operation::None;
    state_ = State::List;
  }
}

bool DictionaryDownloadActivity::fetchAndParseManifest() {
  items_.clear();
  downloadingIndex_ = -1;
  char manifestUrl[192];
  const int manifestUrlLength =
      snprintf(manifestUrl, sizeof(manifestUrl), CrossMuxEndpoints::DICTIONARY_MANIFEST_FORMAT,
               CrossMuxEndpoints::host(), LANGUAGE_CODES[static_cast<uint8_t>(I18N.getLanguage())]);
  if (manifestUrlLength < 0 || static_cast<size_t>(manifestUrlLength) >= sizeof(manifestUrl)) {
    LOG_ERR("DICTDL", "Manifest URL exceeds %zu bytes", sizeof(manifestUrl));
    return false;
  }
  LOG_DBG("DICTDL", "Fetching manifest: %s", manifestUrl);
  if (HttpDownloader::downloadToFile(manifestUrl, MANIFEST_TMP, nullptr) != HttpDownloader::OK) {
    LOG_ERR("DICTDL", "Failed to fetch dictionary manifest");
    Storage.remove(MANIFEST_TMP);
    errorMessage_ = StrId::STR_DOWNLOAD_FAILED;
    return false;
  }

  JsonDocument doc;
  DeserializationError parseError;
  bool manifestTooLarge = false;
  {
    HalFile manifestFile;
    if (!Storage.openFileForRead("DICTDL", MANIFEST_TMP, manifestFile)) {
      Storage.remove(MANIFEST_TMP);
      errorMessage_ = StrId::STR_DICTIONARY_MANIFEST_INVALID;
      return false;
    }
    manifestTooLarge = manifestFile.fileSize() > MAX_MANIFEST_BYTES;
    if (!manifestTooLarge) parseError = deserializeJson(doc, manifestFile);
  }
  Storage.remove(MANIFEST_TMP);
  if (manifestTooLarge || parseError || (doc["version"] | 0) != DictionaryResource::MANIFEST_VERSION) {
    LOG_ERR("DICTDL", "Invalid dictionary manifest: %s", parseError.c_str());
    errorMessage_ = StrId::STR_DICTIONARY_MANIFEST_INVALID;
    return false;
  }

  baseUrl_ = doc["baseUrl"] | "";
  JsonArray dictionaries = doc["dictionaries"].as<JsonArray>();
  if (baseUrl_.empty() || dictionaries.isNull() || dictionaries.size() > DictionaryResource::MAX_ITEMS) {
    errorMessage_ = StrId::STR_DICTIONARY_MANIFEST_INVALID;
    return false;
  }
  if (baseUrl_.back() != '/') baseUrl_.push_back('/');
  // The list must survive across frames, so stack storage is not viable. The
  // trust-boundary caps above keep this lifecycle allocation bounded.
  items_.reserve(dictionaries.size());

  for (JsonObject object : dictionaries) {
    ManifestItem item;
    item.id = object["id"] | "";
    item.name = object["name"] | "";
    item.description = object["description"] | "";
    item.revision = object["revision"] | 0;
    const char* sourceUrl = object["sourceUrl"] | "";
    const char* licenseName = object["license"]["name"] | "";
    const char* licenseUrl = object["license"]["url"] | "";
    const JsonArray files = object["files"].as<JsonArray>();
    const bool duplicate =
        std::any_of(items_.begin(), items_.end(), [&item](const auto& existing) { return existing.id == item.id; });
    if (!DictionaryResource::isValidId(item.id) || item.name.empty() || item.name.size() > MAX_NAME_BYTES ||
        item.description.size() > MAX_DESCRIPTION_BYTES || item.revision < 1 || sourceUrl[0] == '\0' ||
        licenseName[0] == '\0' || licenseUrl[0] == '\0' || files.isNull() || files.size() < 2 || files.size() > 3 ||
        duplicate) {
      errorMessage_ = StrId::STR_DICTIONARY_MANIFEST_INVALID;
      items_.clear();
      return false;
    }

    item.files.reserve(files.size());
    bool hasIndex = false;
    bool hasData = false;
    bool hasIfo = false;
    for (JsonObject fileObject : files) {
      ManifestFile file;
      file.name = fileObject["name"] | "";
      file.size = fileObject["size"] | 0;
      if (!DictionaryResource::isValidFileName(item.id, file.name) || file.size == 0 ||
          file.size >= DictionaryResource::MAX_FILE_BYTES || !fileObject["crc32"].is<uint32_t>() ||
          std::any_of(item.files.begin(), item.files.end(),
                      [&file](const auto& existing) { return existing.name == file.name; })) {
        errorMessage_ = StrId::STR_DICTIONARY_MANIFEST_INVALID;
        items_.clear();
        return false;
      }
      file.crc32 = fileObject["crc32"].as<uint32_t>();
      hasIndex |= hasSuffix(file.name, ".idx");
      hasData |= hasSuffix(file.name, ".dict") || hasSuffix(file.name, ".dict.dz");
      hasIfo |= hasSuffix(file.name, ".ifo");
      item.totalSize += file.size;
      item.files.push_back(std::move(file));
    }
    if (!hasIndex || !hasData || (files.size() == 3 && !hasIfo)) {
      errorMessage_ = StrId::STR_DICTIONARY_MANIFEST_INVALID;
      items_.clear();
      return false;
    }

    recoverInstall(item.id.c_str());
    refreshLocalState(item);
    items_.push_back(std::move(item));
  }

  LOG_DBG("DICTDL", "Manifest loaded: %zu dictionaries", items_.size());
  logHeap("manifest loaded");
  return true;
}

void DictionaryDownloadActivity::recoverInstall(const char* id) {
  char active[80];
  char staging[80];
  char backup[80];
  buildPath(active, sizeof(active), "", id);
  buildPath(staging, sizeof(staging), ".install-", id);
  buildPath(backup, sizeof(backup), ".backup-", id);
  const bool activeExists = Storage.exists(active);
  const bool backupExists = Storage.exists(backup);
  if (!activeExists && backupExists) {
    Storage.rename(backup, active);
  } else if (activeExists && backupExists) {
    if (readMarkerRevision(active) > 0) {
      Storage.removeDir(backup);
    } else if (Storage.removeDir(active)) {
      Storage.rename(backup, active);
    }
  }
  if (Storage.exists(staging)) Storage.removeDir(staging);
}

void DictionaryDownloadActivity::refreshLocalState(ManifestItem& item) {
  char visible[80];
  char hidden[80];
  snprintf(visible, sizeof(visible), "/dictionaries/%s", item.id.c_str());
  buildPath(hidden, sizeof(hidden), "", item.id.c_str());
  const int installedRevision = Storage.exists(hidden) ? readMarkerRevision(hidden) : -1;
  item.localState =
      DictionaryResource::classify(Storage.exists(visible), Storage.exists(hidden), installedRevision, item.revision);
}

bool DictionaryDownloadActivity::validateDownloadedFile(const char* path, const ManifestFile& file) {
  HalFile downloaded;
  if (!Storage.openFileForRead("DICTDL", path, downloaded) || downloaded.fileSize() != file.size) return false;
  uint8_t buffer[128];
  uint32_t crc = 0;
  const bool isIndex = hasSuffix(file.name, ".idx");
  const bool isDictZip = hasSuffix(file.name, ".dict.dz");
  uint32_t indexEntries = 0;
  size_t wordBytes = 0;
  uint8_t suffixBytes = 0;
  size_t bytesSeen = 0;
  uint8_t gzipMagic[2] = {};
  while (downloaded.available()) {
    const int read = downloaded.read(buffer, sizeof(buffer));
    if (read <= 0) return false;
    crc = esp_rom_crc32_le(crc, buffer, static_cast<uint32_t>(read));
    for (int i = 0; i < read; ++i, ++bytesSeen) {
      if (bytesSeen < sizeof(gzipMagic)) gzipMagic[bytesSeen] = buffer[i];
      if (!isIndex) continue;
      if (suffixBytes > 0) {
        if (--suffixBytes == 0) ++indexEntries;
      } else if (buffer[i] == 0) {
        if (wordBytes == 0) return false;
        wordBytes = 0;
        suffixBytes = 8;
      } else {
        ++wordBytes;
      }
    }
  }
  if (crc != file.crc32) return false;
  if (isIndex && (indexEntries == 0 || suffixBytes != 0 || wordBytes != 0)) return false;
  return !isDictZip || (gzipMagic[0] == 0x1f && gzipMagic[1] == 0x8b);
}

bool DictionaryDownloadActivity::ifoUses64BitOffsets(const char* path) {
  static constexpr char NEEDLE[] = "idxoffsetbits=64";
  HalFile file;
  if (!Storage.openFileForRead("DICTDL", path, file)) return true;
  size_t matched = 0;
  while (file.available()) {
    const int byte = file.read();
    if (byte < 0) break;
    if (byte == NEEDLE[matched]) {
      if (++matched == sizeof(NEEDLE) - 1) return true;
    } else {
      matched = byte == NEEDLE[0] ? 1 : 0;
    }
  }
  return false;
}

bool DictionaryDownloadActivity::writeMarker(const char* stagePath, const int revision) {
  char path[112];
  char contents[24];
  snprintf(path, sizeof(path), "%s/.crossmux-resource", stagePath);
  const int length = snprintf(contents, sizeof(contents), "1:%d\n", revision);
  HalFile marker;
  return Storage.openFileForWrite("DICTDL", path, marker) &&
         marker.write(contents, static_cast<size_t>(length)) == static_cast<size_t>(length);
}

bool DictionaryDownloadActivity::commitInstall(const char* id) {
  char active[80];
  char staging[80];
  char backup[80];
  buildPath(active, sizeof(active), "", id);
  buildPath(staging, sizeof(staging), ".install-", id);
  buildPath(backup, sizeof(backup), ".backup-", id);
  if (Storage.exists(backup) && !Storage.removeDir(backup)) return false;
  const bool hadActive = Storage.exists(active);
  if (hadActive && !Storage.rename(active, backup)) return false;
  if (!Storage.rename(staging, active)) {
    if (hadActive) Storage.rename(backup, active);
    return false;
  }
  if (hadActive && !Storage.removeDir(backup)) {
    LOG_ERR("DICTDL", "Installed dictionary but could not remove backup: %s", backup);
  }
  return true;
}

DictionaryDownloadActivity::DownloadResult DictionaryDownloadActivity::downloadItem(ManifestItem& item,
                                                                                    const bool selectAfterInstall) {
  uint64_t totalBytes = 0;
  uint64_t freeBytes = 0;
  if (!Storage.getSpace(totalBytes, freeBytes) || freeBytes < item.totalSize) {
    errorMessage_ = StrId::STR_DICTIONARY_NOT_ENOUGH_SPACE;
    RenderLock lock(*this);
    state_ = State::Error;
    return DownloadResult::Failed;
  }
  if (item.localState == DictionaryResource::LocalState::Conflict || !Storage.ensureDirectoryExists(ROOT)) {
    errorMessage_ = StrId::STR_DICTIONARY_LOCAL_CONFLICT;
    RenderLock lock(*this);
    state_ = State::Error;
    return DownloadResult::Failed;
  }

  char staging[80];
  buildPath(staging, sizeof(staging), ".install-", item.id.c_str());
  if ((Storage.exists(staging) && !Storage.removeDir(staging)) || !Storage.mkdir(staging)) {
    errorMessage_ = StrId::STR_DICTIONARY_INSTALL_FAILED;
    RenderLock lock(*this);
    state_ = State::Error;
    return DownloadResult::Failed;
  }

  {
    RenderLock lock(*this);
    state_ = State::Downloading;
    downloadingIndex_ = static_cast<int>(&item - items_.data());
    cancelRequested_ = false;
    goHomeRequested_ = false;
  }
  requestUpdateAndWait();
  logHeap("download start");

  const size_t fileOffset = currentFileIndex_;
  for (size_t index = 0; index < item.files.size(); ++index) {
    const auto& file = item.files[index];
    currentFileIndex_ = fileOffset + index;
    fileProgress_ = 0;
    fileTotal_ = file.size;
    requestUpdateAndWait();
    char destination[160];
    snprintf(destination, sizeof(destination), "%s/%s", staging, file.name.c_str());
    uint32_t lastRefresh = millis();
    const auto download = HttpDownloader::downloadToFile(
        baseUrl_ + file.name, destination,
        [this, &lastRefresh](const size_t downloaded, const size_t total) {
          fileProgress_ = downloaded;
          fileTotal_ = total;
          mappedInput.update();
          if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
              mappedInput.wasPressed(MappedInputManager::Button::Back)) {
            cancelRequested_ = true;
          }
          if (mappedInput.wasHomeGesture()) {
            cancelRequested_ = true;
            goHomeRequested_ = true;
          }
          routeTouch(mappedInput);
          const uint32_t now = millis();
          if (now - lastRefresh >= PROGRESS_REFRESH_MS) {
            lastRefresh = now;
            requestUpdate(true);
          }
        },
        &cancelRequested_);
    if (download == HttpDownloader::ABORTED) {
      Storage.removeDir(staging);
      operation_ = Operation::None;
      if (goHomeRequested_) {
        onGoHome();
        return DownloadResult::Cancelled;
      }
      waitForBackRelease_ = true;
      RenderLock lock(*this);
      state_ = State::List;
      return DownloadResult::Cancelled;
    }
    if (download != HttpDownloader::OK || !validateDownloadedFile(destination, file) ||
        (hasSuffix(file.name, ".ifo") && ifoUses64BitOffsets(destination))) {
      LOG_ERR("DICTDL", "Dictionary file validation failed: %s", file.name.c_str());
      Storage.removeDir(staging);
      errorMessage_ = StrId::STR_DICTIONARY_FILE_INVALID;
      RenderLock lock(*this);
      state_ = State::Error;
      return DownloadResult::Failed;
    }
  }

  if (!writeMarker(staging, item.revision) || !commitInstall(item.id.c_str())) {
    if (Storage.exists(staging)) Storage.removeDir(staging);
    errorMessage_ = StrId::STR_DICTIONARY_INSTALL_FAILED;
    RenderLock lock(*this);
    state_ = State::Error;
    return DownloadResult::Failed;
  }

  item.localState = DictionaryResource::LocalState::Installed;
  currentFileIndex_ = fileOffset + item.files.size();
  if (selectAfterInstall) {
    strncpy(SETTINGS.dictionaryName, item.id.c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
    SETTINGS.saveToFile();
  }
  return DownloadResult::Success;
}

void DictionaryDownloadActivity::downloadSingle(const int itemIndex) {
  if (itemIndex < 0 || itemIndex >= static_cast<int>(items_.size())) return;
  currentFileTotal_ = items_[itemIndex].files.size();
  if (downloadItem(items_[itemIndex], true) == DownloadResult::Success) {
    operation_ = Operation::None;
    RenderLock lock(*this);
    state_ = State::Complete;
    renderer.requestNextFullRefresh();
  }
  requestUpdateAndWait();
}

void DictionaryDownloadActivity::updateAll() {
  char selectedDictionary[sizeof(SETTINGS.dictionaryName)];
  strncpy(selectedDictionary, SETTINGS.dictionaryName, sizeof(selectedDictionary));
  selectedDictionary[sizeof(selectedDictionary) - 1] = '\0';
  for (auto& item : items_) {
    if (item.localState != DictionaryResource::LocalState::UpdateAvailable) continue;
    if (downloadItem(item, false) != DownloadResult::Success) {
      requestUpdateAndWait();
      return;
    }
  }
  strncpy(SETTINGS.dictionaryName, selectedDictionary, sizeof(SETTINGS.dictionaryName) - 1);
  SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
  SETTINGS.saveToFile();
  operation_ = Operation::None;
  {
    RenderLock lock(*this);
    state_ = State::Complete;
    renderer.requestNextFullRefresh();
  }
  requestUpdateAndWait();
}

void DictionaryDownloadActivity::retryOperation() {
  currentFileIndex_ = 0;
  switch (operation_) {
    case Operation::Single:
      downloadSingle(downloadingIndex_);
      break;
    case Operation::UpdateAll:
      updateAll();
      break;
    case Operation::None:
      onWifiSelectionComplete(true);
      break;
  }
}

bool DictionaryDownloadActivity::showUpdateAllRow() const {
  return std::any_of(items_.begin(), items_.end(), [](const auto& item) {
    return item.localState == DictionaryResource::LocalState::UpdateAvailable;
  });
}

void DictionaryDownloadActivity::promptDeleteSelected() {
  const int index = itemIndexFromList(selectedIndex_);
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  auto confirmation =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE), items_[index].name);
  if (!confirmation) {
    LOG_ERR("DICTDL", "OOM allocating ConfirmationActivity (%zu bytes)", sizeof(ConfirmationActivity));
    return;
  }
  startActivityForResult(std::move(confirmation),
                         [this](const ActivityResult& result) { onDeleteConfirmationResult(result); });
}

void DictionaryDownloadActivity::onDeleteConfirmationResult(const ActivityResult& result) {
  waitForConfirmRelease_ = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  if (result.isCancelled) {
    requestUpdate();
    return;
  }
  const int index = itemIndexFromList(selectedIndex_);
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  auto& item = items_[index];
  char path[80];
  buildPath(path, sizeof(path), "", item.id.c_str());
  if (!Storage.removeDir(path)) {
    RenderLock lock(*this);
    errorMessage_ = StrId::STR_DICTIONARY_INSTALL_FAILED;
    state_ = State::Error;
  } else {
    if (strcmp(SETTINGS.dictionaryName, item.id.c_str()) == 0) {
      SETTINGS.dictionaryName[0] = '\0';
      SETTINGS.saveToFile();
    }
    item.localState = DictionaryResource::LocalState::NotInstalled;
  }
  requestUpdate();
}

void DictionaryDownloadActivity::loop() {
  if (waitForBackRelease_) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Back)) waitForBackRelease_ = false;
    return;
  }
  if (waitForConfirmRelease_) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) waitForConfirmRelease_ = false;
    return;
  }

  if (state_ == State::Downloading || state_ == State::Complete || state_ == State::Error) {
    const auto touch = routeTouch(mappedInput);
    if (touch.routed && app.invalidated()) requestUpdate();
    if (touch) return;
  }

  if (state_ == State::List) {
    auto activate = [this] {
      if (isUpdateAllRow(selectedIndex_)) {
        operation_ = Operation::UpdateAll;
        currentFileIndex_ = 0;
        currentFileTotal_ = 0;
        for (const auto& item : items_) {
          if (item.localState == DictionaryResource::LocalState::UpdateAvailable)
            currentFileTotal_ += item.files.size();
        }
        updateAll();
        return;
      }
      const int index = itemIndexFromList(selectedIndex_);
      if (index < 0 || index >= static_cast<int>(items_.size())) return;
      const auto localState = items_[index].localState;
      if (localState == DictionaryResource::LocalState::Conflict) return;
      if (localState == DictionaryResource::LocalState::Installed) {
        promptDeleteSelected();
      } else {
        operation_ = Operation::Single;
        downloadingIndex_ = index;
        downloadSingle(index);
      }
    };

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    const int count = listItemCount();
    const auto& metrics = UITheme::getInstance().getMetrics();
    const Rect content =
        SubpageLayout::contentRect(UITheme::getInstance().getScreenSafeArea(renderer, true, false), metrics);
    const int pageItems = GUI.getListPageItems(content.height, true);
    if (count > 0) {
      switch (handleListTouch(selectedIndex_, count, content.y, content.height, true)) {
        case ListTouchResult::Activated:
          activate();
          return;
        case ListTouchResult::Consumed:
          return;
        case ListTouchResult::None:
          break;
      }
      if (mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Up) {
        selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, count, pageItems);
        requestUpdate();
        return;
      }
      if (mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Down) {
        selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, count, pageItems);
        requestUpdate();
        return;
      }
    }
    buttonNavigator_.onNextRelease([this, count] {
      selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, count);
      requestUpdate();
    });
    buttonNavigator_.onPreviousRelease([this, count] {
      selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, count);
      requestUpdate();
    });
    buttonNavigator_.onNextContinuous([this, count, pageItems] {
      selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, count, pageItems);
      requestUpdate();
    });
    buttonNavigator_.onPreviousContinuous([this, count, pageItems] {
      selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, count, pageItems);
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) activate();
  } else if (state_ == State::Complete) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      returnToList();
    }
  } else if (state_ == State::Error) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      operation_ = Operation::None;
      if (items_.empty()) {
        finish();
        return;
      }
      returnToList();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      closeRouting();
      retryOperation();
      requestUpdateAndWait();
    }
  }
}

void DictionaryDownloadActivity::stateScreen(UiScreen& screen, void* user) {
  static_cast<DictionaryDownloadActivity*>(user)->buildStateScreen(screen);
}

void DictionaryDownloadActivity::buildStateScreen(UiScreen& screen) {
  if (!mappedInput.hasTouch()) return;
  fui::FooterAction actions[2];
  uint8_t count = 0;
  switch (state_) {
    case State::Downloading:
      actions[count++] = {tr(STR_CANCEL), ACTION_CANCEL_DOWNLOAD};
      break;
    case State::Complete:
      actions[count++] = {tr(STR_BACK), ACTION_RETURN_TO_LIST};
      break;
    case State::Error:
      actions[count++] = {tr(STR_BACK), ACTION_RETURN_TO_LIST};
      actions[count++] = {tr(STR_RETRY), ACTION_RETRY_DOWNLOAD};
      break;
    default:
      break;
  }
  if (count > 0) screen.footer(actions, count);
}

void DictionaryDownloadActivity::onCancelDownload(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<DictionaryDownloadActivity*>(user);
  if (self->state_ == State::Downloading) self->cancelRequested_ = true;
}

void DictionaryDownloadActivity::returnToList() {
  closeRouting();
  operation_ = Operation::None;
  state_ = State::List;
  requestUpdate();
}

void DictionaryDownloadActivity::onReturnToList(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<DictionaryDownloadActivity*>(user);
  if (self->state_ != State::Complete && self->state_ != State::Error) return;
  if (self->state_ == State::Error && self->items_.empty()) {
    self->app.clearTapFlash();
    self->finish();
    return;
  }
  self->app.clearTapFlash();
  self->returnToList();
}

void DictionaryDownloadActivity::onRetryDownload(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<DictionaryDownloadActivity*>(user);
  if (self->state_ != State::Error) return;
  self->app.clearTapFlash();
  self->closeRouting();
  self->retryOperation();
  self->requestUpdateAndWait();
}

std::string DictionaryDownloadActivity::formatSize(const size_t bytes) {
  char buffer[24];
  if (bytes >= 1024 * 1024) {
    snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buffer, sizeof(buffer), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buffer, sizeof(buffer), "%zu B", bytes);
  }
  return buffer;
}

void DictionaryDownloadActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect content = SubpageLayout::contentRect(safeArea, metrics);
  const Rect textBounds = SubpageLayout::insetHorizontal(content, metrics.contentSidePadding);
  const int titleHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{safeArea.x, safeArea.y + metrics.topPadding, safeArea.width, metrics.headerHeight},
                 tr(STR_DICTIONARY_BROWSER));

  if (state_ == State::Loading) {
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                              tr(STR_LOADING_DICTIONARY_LIST));
  } else if (state_ == State::List) {
    if (items_.empty()) {
      UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                                tr(STR_NO_DICTIONARIES_AVAILABLE));
    } else {
      GUI.drawList(
          renderer, content, listItemCount(), selectedIndex_,
          [this](const int index) {
            if (isUpdateAllRow(index)) return std::string(tr(STR_UPDATE_ALL));
            return items_[itemIndexFromList(index)].name;
          },
          [this](const int index) {
            if (isUpdateAllRow(index)) return std::string();
            return items_[itemIndexFromList(index)].description;
          },
          nullptr,
          [this](const int index) {
            if (isUpdateAllRow(index)) return std::string();
            switch (items_[itemIndexFromList(index)].localState) {
              case DictionaryResource::LocalState::Installed:
                return std::string(tr(STR_INSTALLED));
              case DictionaryResource::LocalState::UpdateAvailable:
                return std::string(tr(STR_UPDATE_AVAILABLE));
              case DictionaryResource::LocalState::Conflict:
                return std::string(tr(STR_DICTIONARY_LOCAL_CONFLICT));
              case DictionaryResource::LocalState::NotInstalled:
                return formatSize(items_[itemIndexFromList(index)].totalSize);
            }
            return std::string();
          },
          true,
          [this](const int index) {
            return !isUpdateAllRow(index) &&
                   items_[itemIndexFromList(index)].localState == DictionaryResource::LocalState::Installed;
          });
    }
    const char* action = "";
    if (!items_.empty()) {
      if (isUpdateAllRow(selectedIndex_)) {
        action = tr(STR_UPDATE);
      } else {
        const auto localState = items_[itemIndexFromList(selectedIndex_)].localState;
        if (localState == DictionaryResource::LocalState::Installed) {
          action = tr(STR_DELETE);
        } else if (localState != DictionaryResource::LocalState::Conflict) {
          action = tr(STR_DOWNLOAD);
        }
      }
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), action, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == State::Downloading) {
    const auto& item = items_[downloadingIndex_];
    const std::string status = std::string(tr(STR_DOWNLOADING)) + " " + item.name + " (" +
                               std::to_string(currentFileIndex_ + 1) + "/" + std::to_string(currentFileTotal_) + ")";
    const int gap = SubpageLayout::sectionGap(metrics);
    const int blockHeight = lineHeight * 2 + gap + GUI.measureProgressBarHeight(renderer, metrics.progressBarHeight);
    int y = SubpageLayout::centeredTop(content, blockHeight);
    UITheme::drawCenteredWrappedText(renderer, Rect{textBounds.x, y, textBounds.width, lineHeight * 2}, UI_10_FONT_ID,
                                     status.c_str(), 2, true, EpdFontFamily::REGULAR,
                                     UITheme::TextVerticalAlignment::TOP);
    y += lineHeight * 2 + gap;
    const int percent = fileTotal_ > 0 ? static_cast<int>(fileProgress_ * 100 / fileTotal_) : 0;
    GUI.drawProgressBar(renderer, Rect{textBounds.x, y, textBounds.width, metrics.progressBarHeight}, percent, 100);
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == State::Complete) {
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, SubpageLayout::centeredTop(content, titleHeight),
                              tr(STR_DICTIONARY_INSTALLED), true, EpdFontFamily::BOLD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == State::Error) {
    const int gap = SubpageLayout::relatedGap(metrics);
    int y = SubpageLayout::centeredTop(content, titleHeight + gap + lineHeight * 2);
    UITheme::drawCenteredText(renderer, textBounds, UI_12_FONT_ID, y, tr(STR_DICTIONARY_INSTALL_FAILED), true,
                              EpdFontFamily::BOLD);
    y += titleHeight + gap;
    UITheme::drawCenteredWrappedText(renderer, Rect{textBounds.x, y, textBounds.width, lineHeight * 2}, UI_10_FONT_ID,
                                     I18N.get(errorMessage_), 2, true, EpdFontFamily::REGULAR,
                                     UITheme::TextVerticalAlignment::TOP);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  if (state_ == State::Downloading || state_ == State::Complete || state_ == State::Error) renderUi();
  renderer.displayBuffer();
}
