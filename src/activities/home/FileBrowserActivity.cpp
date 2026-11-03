#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "InxItemLayout.h"
#include "MappedInputManager.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/SubpageLayout.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/inx_library.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/FileEditUtils.h"

namespace fui = freeink::ui;

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

void formatFileName(const std::string& filename, char* buffer, size_t bufferSize);
void formatFileExtension(const std::string& filename, char* buffer, size_t bufferSize);
std::string getFileName(std::string filename);
std::string getFileExtension(const std::string& filename);

FileBrowserActivity::FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string initialPath, const Mode mode)
    : UiListActivity("FileBrowser", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      mode(mode),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

void FileBrowserActivity::loadFiles() {
  clearContentSelection();
  prewarmedStart = -1;
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
    const bool isDirectory = file.isDirectory();
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (isDirectory) {
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
              FsHelpers::hasImageExtension(filename)) {
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

bool FileBrowserActivity::usesIconLayout() const {
  return mode == Mode::Books && browserState == BrowserState::Browsing && UITheme::getInstance().hasMainTabs() &&
         InxGridGeometry::layoutFrom(SETTINGS.inxLibraryLayout) == InxItemLayout::Icons;
}

void FileBrowserActivity::drawIconGrid(UiScreen& screen, const fui::Rect rect) {
  const int start = nav.top;
  constexpr int iconSize = 72;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const bool showSelection = showMainTabContentSelection();
  for (int slot = 0; slot < InxGridGeometry::itemsPerPage && start + slot < listCount(); ++slot) {
    const int index = start + slot;
    Rect cell = InxGridGeometry::cellBounds(slot, rect.width, rect.height);
    cell.x += rect.x;
    cell.y += rect.y;
    const bool selected = showSelection && index == nav.selected;
    if (selected) renderer.fillRect(cell.x, cell.y, cell.width, cell.height, true);
    const UIIcon type = UITheme::getFileIcon(files[index]);
    const uint8_t* icon = type == UIIcon::Folder ? FolderLarge : (type == UIIcon::Image ? ImageLarge : BookLarge);
    const int iconX = cell.x + (cell.width - iconSize) / 2;
    const int iconY = cell.y + std::max(4, (cell.height - iconSize - lineHeight - 6) / 2);
    if (selected)
      renderer.drawIconInverted(icon, iconX, iconY, iconSize);
    else
      renderer.drawIcon(icon, iconX, iconY, iconSize);
    freeink::ui::ListItem item;
    provideRow(this, static_cast<uint16_t>(index), item);
    auto text = screen.theme().bodyText;
    text.align = fui::TextAlign::Center;
    text.bold = false;
    text.color = selected ? fui::Color::White : fui::Color::Black;
    screen.target().text(fui::Rect{static_cast<int16_t>(cell.x + 4), static_cast<int16_t>(iconY + iconSize + 6),
                                   static_cast<int16_t>(std::max(1, cell.width - 8)), static_cast<int16_t>(lineHeight)},
                         item.label, text);
    screen.frame().hit(fui::Rect{static_cast<int16_t>(cell.x), static_cast<int16_t>(cell.y),
                                 static_cast<int16_t>(cell.width), static_cast<int16_t>(cell.height)},
                       ACTION_ROW, static_cast<int16_t>(index), fui::InputTouch | fui::InputLongPress);
  }
  GUI.drawSideScrollBar(renderer, Rect{rect.x, rect.y, rect.width, rect.height}, files.size(), start,
                        InxGridGeometry::itemsPerPage);
}

void FileBrowserActivity::onEnter() {
  UiListActivity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    // The first screen build pulls the viewport to it (ListNav follow-on-build).
    nav.selected = static_cast<int>(findEntry(fileName));
  } else {
    loadFiles();
  }
}

void FileBrowserActivity::onExit() {
  Activity::onExit();
  files.clear();
  fileNameBuffer.reset();
  moveSourcePath.clear();
  moveSourceEntry.clear();
  moveReturnPath.clear();
}

void FileBrowserActivity::selectMainTabContentEdge(const MainTabContentEdge edge) {
  moveSelectionTo(MainTabs::contentEdgeIndex(edge, listCount()));
}

bool FileBrowserActivity::removeDirFile(const std::string& fullPath) { return removeBookFile(fullPath); }

std::string FileBrowserActivity::selectedPath() const {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return {};
  if (files[nav.selected] == MOVE_HERE_ENTRY) return basepath;
  return joinPath(basepath, cleanEntryName(files[nav.selected]));
}

void FileBrowserActivity::showNotice(const StrId message) {
  constexpr StrId options[] = {StrId::STR_OK_BUTTON};
  editPopup.show(message, options, 1, 0, [](int) {});
  requestUpdate();
}

void FileBrowserActivity::showEditMenu() {
  if (mode != Mode::Books || browserState != BrowserState::Browsing || files.empty() || nav.selected < 0 ||
      nav.selected >= listCount()) {
    return;
  }

  const std::string entry = cleanEntryName(files[nav.selected]);
  if (FsHelpers::isProtectedPathComponent(entry)) return;

  constexpr StrId actions[] = {StrId::STR_OPEN, StrId::STR_DELETE, StrId::STR_RENAME, StrId::STR_MOVE};
  static constexpr EditAction order[] = {EditAction::Open, EditAction::Delete, EditAction::Rename, EditAction::Move};
  editPopup.show(StrId::STR_FILENAME, actions, static_cast<int>(std::size(actions)), 0, [this](const int index) {
    if (index >= 0 && index < static_cast<int>(std::size(order))) executeEditAction(order[index]);
  });
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
      if (!files.empty() && nav.selected >= 0 && nav.selected < listCount()) {
        promptDelete(selectedPath(), files[nav.selected]);
      }
      return;
    case EditAction::Open:
      activateSelected();
      return;
  }
}

void FileBrowserActivity::promptRename() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string entry = files[nav.selected];
  const std::string oldPath = selectedPath();
  const bool isDirectory = entry.back() == '/';
  const std::string extension = isDirectory ? std::string() : getFileExtension(entry);
  const size_t maxLength = MAX_COMPONENT_BYTES > extension.size() ? MAX_COMPONENT_BYTES - extension.size() : 0;
  const std::string initialName = isDirectory ? cleanEntryName(entry) : getFileName(entry);
  const int returnIndex = nav.selected;
  const std::string returnPath = basepath;

  auto handler = [this, oldPath, extension, isDirectory, returnIndex, returnPath](const ActivityResult& result) {
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
    const auto moved = moveBookFile(oldPath, newPath);
    if (moved == BookMoveResult::NotMoved) {
      LOG_ERR("FileBrowser", "Failed to rename %s to %s", oldPath.c_str(), newPath.c_str());
      showNotice(StrId::STR_FILE_OPERATION_FAILED);
      return;
    }

    const bool dataOk = moved == BookMoveResult::Complete;
    finishEdit(returnPath, returnIndex, newEntry + (isDirectory ? "/" : ""));
    if (!dataOk) showNotice(StrId::STR_FILE_DATA_MIGRATION_FAILED);
  };

  if (!startActivityForResultWith<KeyboardEntryActivity>(std::move(handler), tr(STR_RENAME), initialName, maxLength,
                                                         InputType::Text)) {
    showNotice(StrId::STR_FILE_OPERATION_FAILED);
  }
}

void FileBrowserActivity::beginMove() {
  if (files.empty() || nav.selected < 0 || nav.selected >= listCount()) return;
  moveSourceEntry = files[nav.selected];
  moveSourcePath = selectedPath();
  moveReturnPath = basepath;
  moveReturnIndex = nav.selected;
  moveSourceIsDirectory = moveSourceEntry.back() == '/';
  browserState = BrowserState::ChoosingMoveDestination;
  {
    RenderLock lock(*this);
    loadFiles();
    nav.selected = 0;
    nav.top = 0;
  }
  requestUpdate();
}

void FileBrowserActivity::cancelMove() {
  const std::string returnPath = moveReturnPath;
  const std::string sourceEntry = moveSourceEntry;
  const int returnIndex = moveReturnIndex;
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
  const auto moved = moveBookFile(moveSourcePath, newPath);
  if (moved == BookMoveResult::NotMoved) {
    LOG_ERR("FileBrowser", "Failed to move %s to %s", moveSourcePath.c_str(), newPath.c_str());
    showNotice(StrId::STR_FILE_OPERATION_FAILED);
    return;
  }

  const bool dataOk = moved == BookMoveResult::Complete;
  const std::string returnPath = moveReturnPath;
  const int returnIndex = moveReturnIndex;
  browserState = BrowserState::Browsing;
  finishEdit(returnPath, returnIndex);
  moveSourcePath.clear();
  moveSourceEntry.clear();
  moveReturnPath.clear();
  if (!dataOk) showNotice(StrId::STR_FILE_DATA_MIGRATION_FAILED);
}

void FileBrowserActivity::finishEdit(const std::string& returnPath, const int fallbackIndex,
                                     const std::string& selectedEntry) {
  RenderLock lock(*this);
  basepath = returnPath;
  loadFiles();
  if (!selectedEntry.empty()) {
    nav.selected = static_cast<int>(findEntry(selectedEntry));
    if (!files.empty() && files[nav.selected] == selectedEntry) establishContentSelection(nav.selected);
  } else if (files.empty()) {
    nav.selected = 0;
  } else {
    nav.selected = std::clamp(fallbackIndex, 0, listCount() - 1);
  }
  nav.follow(listCount());
  lock.unlock();
  requestUpdate(true);
}

void FileBrowserActivity::promptDelete(const std::string& fullPath, const std::string& entry) {
  const int returnIndex = nav.selected;
  auto handler = [this, fullPath, returnIndex](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (removeDirFile(fullPath)) {
      finishEdit(basepath, returnIndex);
      return;
    }
    LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
    showNotice(StrId::STR_FILE_OPERATION_FAILED);
  };

  const std::string heading = tr(STR_DELETE_ITEM_CONFIRM);
  if (!startActivityForResultWith<ConfirmationActivity>(std::move(handler), heading, cleanEntryName(entry))) {
    showNotice(StrId::STR_FILE_OPERATION_FAILED);
  }
}

void FileBrowserActivity::activateIndex(const int index) {
  (void)index;  // base already synced nav.selected to the tapped row
  // Activation navigates or opens; a lingering flash would gray an unrelated
  // row on the next list.
  app.clearTapFlash();
  activateSelected();
}

void FileBrowserActivity::onRowLongPress(const int index) {
  (void)index;  // base already synced nav.selected to the pressed row
  app.clearTapFlash();
  if (mode != Mode::Books) {
    activateSelected();
  } else {
    showEditMenu();
  }
}

#if FREEINK_CAP_TOUCH
bool FileBrowserActivity::canSwipeDelete(const int index) const {
  return mode == Mode::Books && browserState == BrowserState::Browsing && index >= 0 && index < listCount() &&
         !FsHelpers::isProtectedPathComponent(cleanEntryName(files[index]));
}

void FileBrowserActivity::swipeDelete(const int index) {
  if (!canSwipeDelete(index)) return;
  nav.selected = index;
  promptDelete(selectedPath(), files[index]);
}
#endif

void FileBrowserActivity::activateSelected() {
  if (files.empty()) return;
  // A touch activation can carry a row index captured before a delete/reload
  // shrank the list; the next render re-registers the rows.
  if (nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string& entry = files[nav.selected];
  if (browserState == BrowserState::ChoosingMoveDestination && entry == MOVE_HERE_ENTRY) {
    completeMove();
    return;
  }
  const bool isDirectory = (entry.back() == '/');

  if (mode != Mode::Books && !isDirectory) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  RenderLock lock(*this);
  if (basepath.back() != '/') basepath += "/";
  if (isDirectory) {
    basepath += entry.substr(0, entry.length() - 1);
    loadFiles();
    nav.selected = 0;
    nav.top = 0;
    lock.unlock();
    requestUpdate();
  } else {
    const std::string fullPath = basepath + entry;
    lock.unlock();
    onSelectBook(fullPath);
  }
}

bool FileBrowserActivity::handleCustomInput() {
  if (editPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;

  // Long press BACK (1s+) goes to root folder (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && browserState == BrowserState::Browsing &&
      mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= GO_HOME_MS &&
      basepath != "/") {
    {
      // buildScreen() runs on the render task and reads basepath plus the
      // file list; mutate only under the render lock.
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
    }
    requestUpdate();
    return true;
  }

  return false;
}

bool FileBrowserActivity::handleButtons() {
  if (mode == Mode::Books && browserState == BrowserState::Browsing &&
      mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, GO_HOME_MS)) {
    app.clearTapFlash();
    showEditMenu();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        {
          // buildScreen() runs on the render task and reads basepath plus the
          // file list; mutate only under the render lock.
          RenderLock lock(*this);
          basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
          if (basepath.empty()) basepath = "/";
          loadFiles();

          const auto pos = oldPath.find_last_of('/');
          const std::string dirName = oldPath.substr(pos + 1) + "/";
          nav.selected = static_cast<int>(findEntry(dirName));
          if (!files.empty() && files[nav.selected] == dirName) establishContentSelection(nav.selected);
          nav.top = 0;
          nav.follow(listCount());
        }

        requestUpdate();
      } else if (browserState == BrowserState::ChoosingMoveDestination) {
        cancelMove();
      } else if (mode != Mode::Books) {
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
    return true;
  }

  return false;
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

void FileBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = pageContentRect();
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(content.y), static_cast<int16_t>(renderer.getScreenWidth() - content.x - content.width),
      static_cast<int16_t>(renderer.getScreenHeight() - content.y - content.height), static_cast<int16_t>(content.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Full path band at the bottom: separator on top, left-truncated so the
  // deepest directory stays visible.
  {
    const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(pathLineHeight + metrics.verticalSpacing));
    screen.target().fill(fui::Rect{band.x, band.y, band.width, 3}, fui::Paint::solid(fui::Color::Black));
    const int pathY =
        band.y + metrics.verticalSpacing / 2 + (band.height - metrics.verticalSpacing / 2 - pathLineHeight) / 2;
    const int pathMaxWidth = band.width - metrics.contentSidePadding * 2;
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
    renderer.drawText(SMALL_FONT_ID, band.x + metrics.contentSidePadding, pathY, pathDisplay);
  }

  if (files.empty()) {
    screen.centeredText(mode == Mode::PickFirmware ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND),
                        screen.theme().bodyText);
    return;
  }

  if (usesIconLayout()) {
    syncPagedViewport(InxGridGeometry::itemsPerPage);
    prewarmRowGlyphs(nav.top);
    drawIconGrid(screen, screen.body());
    return;
  }

  fui::ListProps props;
  props.rowProvider = &FileBrowserActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(files.size());
  props.action = ACTION_ROW;
  // Tap opens/navigates; long-press prompts delete (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;  // air between the extension and the row edge
  // Match the pre-FreeInkUI file-list size while keeping two-line wrapping for
  // long names.
  fui::TextStyle label = UITheme::getInstance().hasMainTabs() ? screen.theme().bodyText : screen.theme().smallText;
  label.maxLines = 2;
  props.labelText = label;
  // The trailing value here is just the short extension: skip the balanced
  // 60%-band wrap cap and let both name lines run the full width before it.
  props.balanceWrappedLabelWithValue = false;
  // Wrapped two-line names shrink how many rows fit a page, so the last row
  // of a page can end up in leftover space: draw it as a partial preview so
  // files past the fold are visibly present, not silently absent.
  props.partialTrailingRow = UITheme::getInstance().hasMainTabs();
  syncListViewport(screen, props);
  prewarmRowGlyphs(nav.top);
#if FREEINK_CAP_TOUCH
  if (mode == Mode::Books) configureSwipeDelete(screen, props);
#endif
  screen.list(props);
}

void FileBrowserActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName = browserState == BrowserState::ChoosingMoveDestination ? std::string(tr(STR_MOVE))
                           : mode == Mode::PickFirmware ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
                           : mode == Mode::PickPng      ? std::string(tr(STR_CUSTOM_IMAGE))
                                                        : ((basepath == "/") ? std::string(tr(STR_SD_CARD))
                                                                             : basepath.substr(basepath.rfind('/') + 1));
  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  drawPageHeader(Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());
}

void FileBrowserActivity::drawFooter() {
  const char* backLabel =
      browserState == BrowserState::ChoosingMoveDestination
          ? tr(STR_BACK)
          : ((basepath == "/") ? (mode != Mode::Books ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK));
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingFile = mode != Mode::Books && !files.empty() && nav.selected >= 0 && nav.selected < listCount() &&
                             files[nav.selected].back() != '/';
  const bool movingHere = browserState == BrowserState::ChoosingMoveDestination && !files.empty() &&
                          nav.selected >= 0 && nav.selected < listCount() && files[nav.selected] == MOVE_HERE_ENTRY;
  const char* confirmLabel =
      files.empty() ? "" : (movingHere ? tr(STR_MOVE_HERE) : (selectingFile ? tr(STR_SELECT) : tr(STR_OPEN)));
  const auto labels = mainTabButtonLabels(backLabel, confirmLabel, !files.empty());
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void FileBrowserActivity::render(RenderLock&& lock) {
  if (editPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  const auto found = std::find(files.begin(), files.end(), name);
  return found == files.end() ? 0 : static_cast<size_t>(found - files.begin());
}

void formatFileName(const std::string& filename, char* buffer, const size_t bufferSize) {
  if (filename.empty()) {
    buffer[0] = '\0';
    return;
  }
  const bool isDirectory = filename.back() == '/';
  const size_t dot = isDirectory ? filename.size() - 1 : filename.rfind('.');
  const int length = static_cast<int>(dot == std::string::npos ? filename.size() : dot);
  const char* format = isDirectory && !UITheme::getInstance().getTheme().showsFileIcons() ? "[%.*s]" : "%.*s";
  snprintf(buffer, bufferSize, format, length, filename.c_str());
  // Compose only the display copy; filesystem lookup needs the raw entry bytes.
  utf8ComposeNfcInPlace(buffer);
}

void formatFileExtension(const std::string& filename, char* buffer, const size_t bufferSize) {
  buffer[0] = '\0';
  if (filename.empty() || filename.back() == '/') return;
  if (const char* extension = strrchr(filename.c_str(), '.')) snprintf(buffer, bufferSize, "%s", extension);
}

void FileBrowserActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<FileBrowserActivity*>(ctx);
  if (index >= self->files.size()) return;
  const std::string& entry = self->files[index];
  if (entry == MOVE_HERE_ENTRY) {
    item.label = tr(STR_MOVE_HERE);
    item.actionValue = static_cast<int16_t>(index);
    return;
  }
  formatFileName(entry, self->rowNameBuf, sizeof(self->rowNameBuf));
  item.label = self->rowNameBuf;
  formatFileExtension(entry, self->rowExtBuf, sizeof(self->rowExtBuf));
  if (self->rowExtBuf[0] != '\0') {
    item.value = self->rowExtBuf;
  }
  item.icon = listIconFor(UITheme::getFileIcon(entry));
  item.actionValue = static_cast<int16_t>(index);
}

void FileBrowserActivity::prewarmRowGlyphs(const int start) {
  const int total = static_cast<int>(files.size());
  int clamped = start;
  if (clamped > total - PREWARM_WINDOW) clamped = total - PREWARM_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == prewarmedStart) return;
  prewarmedStart = clamped;
  const int count = total - clamped < PREWARM_WINDOW ? total - clamped : PREWARM_WINDOW;

  struct PrewarmCtx {
    FileBrowserActivity* self;
    int first;
    int count;
  } prewarmCtx{this, clamped, count};
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        auto* c = const_cast<PrewarmCtx*>(static_cast<const PrewarmCtx*>(ctx));
        if (i < static_cast<uint32_t>(c->count)) {
          formatFileName(c->self->files[c->first + i], c->self->rowNameBuf, sizeof(c->self->rowNameBuf));
          return c->self->rowNameBuf;
        }
        return c->self->basepath.c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(count) + 1);
}
