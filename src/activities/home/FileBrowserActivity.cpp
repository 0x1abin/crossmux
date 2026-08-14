#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "InxItemLayout.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/icons/inx_library.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/FileEditUtils.h"

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
constexpr size_t MAX_COMPONENT_BYTES = 255;
constexpr char MOVE_HERE_ENTRY[] = "\x01";

std::string cleanEntryName(std::string entry) {
  if (!entry.empty() && entry.back() == '/') entry.pop_back();
  return entry;
}

std::string joinPath(const std::string& parent, const std::string& name) {
  return parent == "/" ? "/" + name : parent + "/" + name;
}
}  // namespace

std::string getFileName(std::string filename);
std::string getFileExtension(const std::string& filename);

void FileBrowserActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  selectorIndex = static_cast<size_t>(MainTabs::contentEdgeIndex(edge, static_cast<int>(files.size())));
}

void FileBrowserActivity::loadFiles() {
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    return;
  }

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (file.isDirectory()) {
      const std::string entryName(fileNameBuffer.get());
      const std::string entryPath = joinPath(basepath, entryName);
      if (browserState == BrowserState::ChoosingMoveDestination &&
          (FsHelpers::isProtectedPathComponent(entryName) ||
           FsHelpers::isSameOrDescendantPath(entryPath, moveSourcePath))) {
        continue;
      }
      files.emplace_back(entryName + "/");
    } else {
      if (browserState == BrowserState::ChoosingMoveDestination) continue;
      std::string_view filename{fileNameBuffer.get()};
      switch (mode) {
        case Mode::Books:
          if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
              FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
              FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
            files.emplace_back(filename);
          }
          break;
        case Mode::PickFirmware:
          if (FsHelpers::checkFileExtension(filename, ".bin")) files.emplace_back(filename);
          break;
        case Mode::PickPng:
          if (FsHelpers::hasPngExtension(filename)) files.emplace_back(filename);
          break;
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
  if (browserState == BrowserState::ChoosingMoveDestination) files.insert(files.begin(), MOVE_HERE_ENTRY);
}

void FileBrowserActivity::onEnter() {
  Activity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  selectorIndex = 0;

  // If Confirm was held while this activity opened (typical when launched from a menu), ignore
  // its release — otherwise we'd immediately auto-open whatever is at index 0.
  lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);

    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    selectorIndex = findEntry(fileName);
  } else {
    loadFiles();
  }

  requestUpdate();
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  fileNameBuffer.reset();
  moveSourcePath.clear();
  moveSourceEntry.clear();
  moveReturnPath.clear();
}

bool FileBrowserActivity::usesIconLayout() const {
  return mode == Mode::Books && browserState == BrowserState::Browsing && UITheme::getInstance().hasMainTabs() &&
         InxGridGeometry::layoutFrom(SETTINGS.inxLibraryLayout) == InxItemLayout::Icons;
}

int FileBrowserActivity::iconIndexFromPoint(const int x, const int y, const int contentTop,
                                            const int contentHeight) const {
  return InxGridGeometry::indexFromPoint(x, y - contentTop, renderer.getScreenWidth(), contentHeight,
                                         InxGridGeometry::pageStart(static_cast<int>(selectorIndex), files.size()),
                                         static_cast<int>(files.size()));
}

// To avoid traversing directories twice (once for cache clearing, once for deletion),
// we do both in one pass here, instead of using Storage.removeDir
bool FileBrowserActivity::removeDirFile(const std::string& fullPath) {
  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FileBrowser", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    clearBookCache(fullPath);
    return Storage.remove(fullPath.c_str());
  }
  file.close();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    return false;
  }

  // Stack of (dirPath, postOrder): postOrder=true means rmdir this path after children are processed.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FileBrowser", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir) {
      LOG_ERR("FileBrowser", "Failed to open dir: %s", currentPath.c_str());
      return false;
    }
    if (!dir.isDirectory()) {
      LOG_ERR("FileBrowser", "Not a directory: %s", currentPath.c_str());
      return false;
    }

    // Push this dir for post-order rmdir (after all children are processed).
    stack.push_back({currentPath, true});

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += fileNameBuffer.get();

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        clearBookCache(entryPath);
        if (!Storage.remove(entryPath.c_str())) {
          LOG_ERR("FileBrowser", "Failed to remove file: %s", entryPath.c_str());
          return false;
        }
      }
    }
  }

  return true;
}

std::string FileBrowserActivity::selectedPath() const {
  if (files.empty() || selectorIndex >= files.size()) return {};
  if (files[selectorIndex] == MOVE_HERE_ENTRY) return basepath;
  return joinPath(basepath, cleanEntryName(files[selectorIndex]));
}

void FileBrowserActivity::showNotice(const StrId message) {
  constexpr StrId options[] = {StrId::STR_OK_BUTTON};
  editPopup.show(message, options, 1, 0, [](int) {});
  requestUpdate();
}

void FileBrowserActivity::showEditMenu() {
  if (mode != Mode::Books || browserState != BrowserState::Browsing || files.empty() || selectorIndex >= files.size()) {
    return;
  }

  const std::string entry = cleanEntryName(files[selectorIndex]);
  if (FsHelpers::isProtectedPathComponent(entry)) return;

  const char* actions[] = {tr(STR_RENAME), tr(STR_MOVE), tr(STR_DELETE)};
  editPopup.show(entry.c_str(), actions, static_cast<int>(std::size(actions)), 0,
                 [this](const int index) { executeEditAction(static_cast<EditAction>(index)); });
  requestUpdate();
}

void FileBrowserActivity::executeEditAction(const EditAction action) {
  switch (action) {
    case EditAction::Rename:
      promptRename();
      return;
    case EditAction::Move:
      beginMove();
      return;
    case EditAction::Delete:
      if (!files.empty() && selectorIndex < files.size()) {
        promptDelete(selectedPath(), files[selectorIndex]);
      }
      return;
  }
}

void FileBrowserActivity::promptRename() {
  if (files.empty() || selectorIndex >= files.size()) return;

  const std::string entry = files[selectorIndex];
  const std::string oldPath = selectedPath();
  const bool isDirectory = entry.back() == '/';
  const std::string extension = isDirectory ? std::string() : getFileExtension(entry);
  const size_t maxLength = MAX_COMPONENT_BYTES > extension.size() ? MAX_COMPONENT_BYTES - extension.size() : 0;
  const std::string initialName = isDirectory ? cleanEntryName(entry) : getFileName(entry);
  const size_t returnIndex = selectorIndex;
  const std::string returnPath = basepath;

  auto handler = [this, oldPath, extension, isDirectory, returnIndex, returnPath](const ActivityResult& result) {
    popupClosing = false;
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);
    lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
    if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) return;

    const std::string& name = std::get<KeyboardResult>(result.data).text;
    if (!FsHelpers::isValidPathComponent(name) || FsHelpers::isProtectedPathComponent(name)) {
      showNotice(StrId::STR_INVALID_FILE_NAME);
      return;
    }

    const std::string newEntry = FileEditUtils::withPreservedExtension(name, extension);
    const std::string newPath = joinPath(returnPath, newEntry);
    if (newPath == oldPath) return;
    if (Storage.exists(newPath.c_str())) {
      showNotice(StrId::STR_TARGET_EXISTS);
      return;
    }
    if (!Storage.rename(oldPath.c_str(), newPath.c_str())) {
      LOG_ERR("FileBrowser", "Failed to rename %s to %s", oldPath.c_str(), newPath.c_str());
      showNotice(StrId::STR_FILE_OPERATION_FAILED);
      return;
    }

    const bool dataOk = relocatePathData(oldPath, newPath, isDirectory);
    finishEdit(returnPath, returnIndex, newEntry + (isDirectory ? "/" : ""));
    if (!dataOk) showNotice(StrId::STR_FILE_DATA_MIGRATION_FAILED);
  };

  if (!startActivityForResultWith<KeyboardEntryActivity>(std::move(handler), tr(STR_RENAME), initialName, maxLength,
                                                         InputType::Text)) {
    showNotice(StrId::STR_FILE_OPERATION_FAILED);
  }
}

void FileBrowserActivity::beginMove() {
  if (files.empty() || selectorIndex >= files.size()) return;
  moveSourceEntry = files[selectorIndex];
  moveSourcePath = selectedPath();
  moveReturnPath = basepath;
  moveReturnIndex = selectorIndex;
  moveSourceIsDirectory = moveSourceEntry.back() == '/';
  browserState = BrowserState::ChoosingMoveDestination;
  loadFiles();
  selectorIndex = 0;
  requestUpdate();
}

void FileBrowserActivity::cancelMove() {
  const std::string returnPath = moveReturnPath;
  const std::string sourceEntry = moveSourceEntry;
  const size_t returnIndex = moveReturnIndex;
  browserState = BrowserState::Browsing;
  finishEdit(returnPath, returnIndex, sourceEntry);
  moveSourcePath.clear();
  moveSourceEntry.clear();
  moveReturnPath.clear();
}

void FileBrowserActivity::completeMove() {
  if (browserState != BrowserState::ChoosingMoveDestination || moveSourcePath.empty()) return;

  const std::string cleanSourceEntry = cleanEntryName(moveSourceEntry);
  const std::string newPath = joinPath(basepath, cleanSourceEntry);
  const auto destinationError = FileEditUtils::validateMoveDestination(
      moveSourcePath, moveReturnPath, basepath, moveSourceIsDirectory, Storage.exists(newPath.c_str()));
  switch (destinationError) {
    case FileEditUtils::MoveDestinationError::None:
      break;
    case FileEditUtils::MoveDestinationError::SameDirectory:
    case FileEditUtils::MoveDestinationError::OwnDescendant:
      showNotice(StrId::STR_CANNOT_MOVE_HERE);
      return;
    case FileEditUtils::MoveDestinationError::TargetExists:
      showNotice(StrId::STR_TARGET_EXISTS);
      return;
  }
  if (!Storage.rename(moveSourcePath.c_str(), newPath.c_str())) {
    LOG_ERR("FileBrowser", "Failed to move %s to %s", moveSourcePath.c_str(), newPath.c_str());
    showNotice(StrId::STR_FILE_OPERATION_FAILED);
    return;
  }

  const bool dataOk = relocatePathData(moveSourcePath, newPath, moveSourceIsDirectory);
  const std::string returnPath = moveReturnPath;
  const size_t returnIndex = moveReturnIndex;
  browserState = BrowserState::Browsing;
  finishEdit(returnPath, returnIndex);
  moveSourcePath.clear();
  moveSourceEntry.clear();
  moveReturnPath.clear();
  if (!dataOk) showNotice(StrId::STR_FILE_DATA_MIGRATION_FAILED);
}

bool FileBrowserActivity::relocateDirectoryData(const std::string& oldPath, const std::string& newPath) {
  if (!fileNameBuffer) return false;

  bool ok = true;
  std::vector<std::string> directories;
  directories.reserve(16);
  directories.push_back(newPath);
  while (!directories.empty()) {
    std::string directoryPath = std::move(directories.back());
    directories.pop_back();
    auto directory = Storage.open(directoryPath.c_str());
    if (!directory || !directory.isDirectory()) {
      LOG_ERR("FileBrowser", "Failed to scan moved directory: %s", directoryPath.c_str());
      ok = false;
      continue;
    }

    directory.rewindDirectory();
    for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) continue;
      const std::string entryPath = joinPath(directoryPath, fileNameBuffer.get());
      if (entry.isDirectory()) {
        directories.push_back(entryPath);
        continue;
      }
      const std::string oldEntryPath = FsHelpers::rebasePath(entryPath, newPath, oldPath);
      if (!relocateBookArtifacts(oldEntryPath, entryPath)) {
        LOG_ERR("FileBrowser", "Failed to migrate reading artifacts: %s", entryPath.c_str());
        ok = false;
      }
    }
  }

  if (!RECENT_BOOKS.updatePathPrefix(oldPath, newPath)) ok = false;
  if (!READING_STATS.updateBookPathPrefix(oldPath, newPath)) ok = false;
  if (FsHelpers::isSameOrDescendantPath(APP_STATE.openEpubPath, oldPath)) {
    APP_STATE.openEpubPath = FsHelpers::rebasePath(APP_STATE.openEpubPath, oldPath, newPath);
    if (!APP_STATE.saveToFile()) ok = false;
  }
  return ok;
}

bool FileBrowserActivity::relocatePathData(const std::string& oldPath, const std::string& newPath,
                                           const bool isDirectory) {
  if (isDirectory) return relocateDirectoryData(oldPath, newPath);
  const bool artifactsOk = relocateBookArtifacts(oldPath, newPath);
  const bool referencesOk = relocateBookReferences(oldPath, newPath);
  return artifactsOk && referencesOk;
}

void FileBrowserActivity::finishEdit(const std::string& returnPath, const size_t fallbackIndex,
                                     const std::string& selectedEntry) {
  basepath = returnPath;
  loadFiles();
  if (!selectedEntry.empty()) {
    selectorIndex = findEntry(selectedEntry);
  } else if (files.empty()) {
    selectorIndex = 0;
  } else {
    selectorIndex = std::min(fallbackIndex, files.size() - 1);
  }
  requestUpdate(true);
}

void FileBrowserActivity::promptDelete(const std::string& fullPath, const std::string& entry) {
  const size_t returnIndex = selectorIndex;
  auto handler = [this, fullPath, returnIndex](const ActivityResult& result) {
    popupClosing = false;
    lockLongPressBack = mappedInput.isPressed(MappedInputManager::Button::Back);
    lockNextConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
    if (result.isCancelled) return;

    LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
    if (removeDirFile(fullPath)) {
      finishEdit(basepath, returnIndex);
      return;
    }
    LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
    showNotice(StrId::STR_FILE_OPERATION_FAILED);
  };

  const std::string heading = tr(STR_DELETE) + std::string("? ");
  if (!startActivityForResultWith<ConfirmationActivity>(std::move(handler), heading, cleanEntryName(entry))) {
    showNotice(StrId::STR_FILE_OPERATION_FAILED);
  }
}

void FileBrowserActivity::loop() {
  if (editPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    popupClosing = !editPopup.isActive();
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      confirmHeld = false;
      confirmLongHandled = false;
    }
    return;
  }
  if (popupClosing) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      return;
    }
    popupClosing = false;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      return;
    }
  }

  // Long press BACK (1s+) goes to root folder (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && browserState == BrowserState::Browsing &&
      mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/" && !lockLongPressBack) {
    basepath = "/";
    loadFiles();
    selectorIndex = 0;
    requestUpdate();
    return;
  }

  if (lockLongPressBack && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    lockLongPressBack = false;
    return;
  }

  const int pathReserved = renderer.getLineHeight(SMALL_FONT_ID) + UITheme::getInstance().getMetrics().verticalSpacing;
  const int pageItems = usesIconLayout()
                            ? InxGridGeometry::itemsPerPage
                            : UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, pathReserved);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;

  auto activateSelected = [this] {
    if (files.empty()) return;

    const std::string& entry = files[selectorIndex];
    if (browserState == BrowserState::ChoosingMoveDestination && entry == MOVE_HERE_ENTRY) {
      completeMove();
      return;
    }

    const bool isDirectory = entry.back() == '/';

    // Picker modes return a file path; directories still navigate normally.
    if (isPickerMode() && !isDirectory) {
      std::string cleanBasePath = basepath;
      if (cleanBasePath.back() != '/') cleanBasePath += "/";
      ActivityResult res{FilePathResult{cleanBasePath + entry}};
      res.isCancelled = false;
      setResult(std::move(res));
      finish();
      return;
    }

    if (isDirectory) {
      basepath = joinPath(basepath, cleanEntryName(entry));
      loadFiles();
      selectorIndex = 0;
      requestUpdate();
      return;
    }

    if (browserState == BrowserState::Browsing) onSelectBook(joinPath(basepath, entry));
  };

  if (usesIconLayout()) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTouchDown(x, y)) {
      const int touched = iconIndexFromPoint(x, y, contentTop, contentHeight);
      if (touched >= 0 && touched != static_cast<int>(selectorIndex)) {
        selectorIndex = static_cast<size_t>(touched);
        requestUpdate();
      }
      return;
    }
    if (mappedInput.wasScreenTapped(x, y)) {
      const int touched = iconIndexFromPoint(x, y, contentTop, contentHeight);
      if (touched >= 0) {
        selectorIndex = static_cast<size_t>(touched);
        activateSelected();
      }
      return;
    }
  } else {
    int touchSel = static_cast<int>(selectorIndex);
    const auto listTouch = handleListTouch(touchSel, static_cast<int>(files.size()), contentTop, contentHeight, false);
    if (listTouch != ListTouchResult::None) {
      selectorIndex = static_cast<size_t>(touchSel);
      if (listTouch == ListTouchResult::Activated) activateSelected();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  if (mode == Mode::Books && browserState == BrowserState::Browsing && confirmHeld && !confirmLongHandled &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= GO_HOME_MS) {
    confirmLongHandled = true;
    showEditMenu();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (lockNextConfirmRelease) {
      lockNextConfirmRelease = false;
    } else if (!confirmLongHandled) {
      activateSelected();
    }
    confirmHeld = false;
    confirmLongHandled = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
        if (basepath.empty()) basepath = "/";
        loadFiles();

        const auto pos = oldPath.find_last_of('/');
        const std::string dirName = oldPath.substr(pos + 1) + "/";
        selectorIndex = findEntry(dirName);

        requestUpdate();
      } else if (browserState == BrowserState::ChoosingMoveDestination) {
        cancelMove();
      } else if (isPickerMode()) {
        // Pickers cancel back to their caller instead of going home.
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
  }

  int listSize = static_cast<int>(files.size());
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void FileBrowserActivity::drawIconGrid(const Rect& rect, const bool showSelection) const {
  const int start = InxGridGeometry::pageStart(static_cast<int>(selectorIndex), files.size());
  const int cellWidth = rect.width / InxGridGeometry::columns;
  const int cellHeight = rect.height / InxGridGeometry::rows;
  constexpr int iconSize = 72;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  for (int slot = 0; slot < InxGridGeometry::itemsPerPage && start + slot < static_cast<int>(files.size()); ++slot) {
    const int index = start + slot;
    const int column = slot % InxGridGeometry::columns;
    const int row = slot / InxGridGeometry::columns;
    const Rect cell{rect.x + column * cellWidth + 4, rect.y + row * cellHeight + 4, cellWidth - 8, cellHeight - 8};
    const bool selected = showSelection && index == static_cast<int>(selectorIndex);
    if (selected) renderer.fillRect(cell.x, cell.y, cell.width, cell.height, true);

    const UIIcon type = UITheme::getFileIcon(files[index]);
    const uint8_t* icon = type == UIIcon::Folder ? FolderLarge : (type == UIIcon::Image ? ImageLarge : BookLarge);
    const int iconX = cell.x + (cell.width - iconSize) / 2;
    const int iconY = cell.y + std::max(4, (cell.height - iconSize - lineHeight - 6) / 2);
    if (selected)
      renderer.drawIconInverted(icon, iconX, iconY, iconSize);
    else
      renderer.drawIcon(icon, iconX, iconY, iconSize);

    const std::string label =
        renderer.truncatedText(UI_10_FONT_ID, getFileName(files[index]).c_str(), std::max(1, cell.width - 8));
    const int labelX = cell.x + (cell.width - renderer.getTextWidth(UI_10_FONT_ID, label.c_str())) / 2;
    renderer.drawText(UI_10_FONT_ID, labelX, iconY + iconSize + 6, label.c_str(), !selected);
  }

  GUI.drawSideScrollBar(renderer, rect, files.size(), start, InxGridGeometry::itemsPerPage);
}

std::string getFileName(std::string filename) {
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

std::string FileBrowserActivity::entryLabel(const size_t index) const {
  return files[index] == MOVE_HERE_ENTRY ? tr(STR_MOVE_HERE) : getFileName(files[index]);
}

std::string FileBrowserActivity::entryExtension(const size_t index) const {
  return files[index] == MOVE_HERE_ENTRY ? std::string() : getFileExtension(files[index]);
}

void FileBrowserActivity::render(RenderLock&&) {
  if (editPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName;
  if (browserState == BrowserState::ChoosingMoveDestination) {
    folderName = tr(STR_SELECT_DESTINATION);
  } else {
    switch (mode) {
      case Mode::Books:
        folderName = (basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1);
        break;
      case Mode::PickFirmware:
        folderName = tr(STR_SELECT_FIRMWARE_FILE);
        break;
      case Mode::PickPng:
        folderName = tr(STR_READING_BACKGROUND);
        break;
    }
  }
  drawPageHeader(Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());

  const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int pathReserved = pathLineHeight + metrics.verticalSpacing;
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - pathReserved;
  const bool showSelection = showMainTabContentSelection();
  if (files.empty()) {
    const char* emptyMsg = (mode == Mode::PickFirmware) ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, emptyMsg);
  } else if (usesIconLayout()) {
    drawIconGrid(Rect{0, contentTop, pageWidth, contentHeight}, showSelection);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, files.size(), selectorIndex,
        [this](int index) { return entryLabel(index); }, nullptr,
        [this](int index) {
          return files[index] == MOVE_HERE_ENTRY ? UIIcon::Folder : UITheme::getFileIcon(files[index]);
        },
        [this](int index) { return entryExtension(index); }, false, nullptr, showSelection);
  }

  // Full path display
  {
    const int pathY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - pathLineHeight;
    const int separatorY = pathY - metrics.verticalSpacing / 2;
    renderer.drawLine(0, separatorY, pageWidth - 1, separatorY, 3, true);
    const int pathMaxWidth = pageWidth - metrics.contentSidePadding * 2;
    // Left-truncate so the deepest directory is always visible
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, pathY, pathDisplay);
  }

  // Help text
  const char* backLabel = browserState == BrowserState::ChoosingMoveDestination
                              ? tr(STR_BACK)
                              : ((basepath == "/") ? (isPickerMode() ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK));
  const bool moveHere = !files.empty() && files[selectorIndex] == MOVE_HERE_ENTRY;
  const bool selectingFile = isPickerMode() && !files.empty() && files[selectorIndex].back() != '/';
  const char* confirmLabel =
      files.empty() ? "" : (moveHere ? tr(STR_MOVE_HERE) : (selectingFile ? tr(STR_SELECT) : tr(STR_OPEN)));
  const auto labels = usesMainTabBar()
                          ? mainTabButtonLabels(backLabel, confirmLabel, files.size() > 1)
                          : mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                                  files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  for (size_t i = 0; i < files.size(); i++)
    if (files[i] == name) return i;
  return 0;
}
