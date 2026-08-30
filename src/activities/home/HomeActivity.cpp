#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCoverLoader.h"

namespace {
struct HomeMenuEntry {
  HomeMenuItem item;
  StrId label;
  UIIcon icon;
};

constexpr HomeMenuEntry kDefaultMenuOrder[] = {
    {HomeMenuItem::FILE_BROWSER, StrId::STR_BROWSE_FILES, Folder},
    {HomeMenuItem::RECENTS, StrId::STR_MENU_RECENT_BOOKS, Recent},
    {HomeMenuItem::OPDS_BROWSER, StrId::STR_OPDS_BROWSER, Library},
    {HomeMenuItem::FILE_TRANSFER, StrId::STR_FILE_TRANSFER, Transfer},
    {HomeMenuItem::SETTINGS_MENU, StrId::STR_SETTINGS_TITLE, Settings},
    {HomeMenuItem::APPS, StrId::STR_APPS_TITLE, Apps},
};
constexpr HomeMenuEntry kCarouselMenuOrder[] = {
    {HomeMenuItem::FILE_BROWSER, StrId::STR_BROWSE_FILES, Folder},
    {HomeMenuItem::RECENTS, StrId::STR_MENU_RECENT_BOOKS, Recent},
    {HomeMenuItem::OPDS_BROWSER, StrId::STR_OPDS_BROWSER, Library},
    {HomeMenuItem::APPS, StrId::STR_APPS_TITLE, Apps},
    {HomeMenuItem::FILE_TRANSFER, StrId::STR_FILE_TRANSFER, Transfer},
    {HomeMenuItem::SETTINGS_MENU, StrId::STR_SETTINGS_TITLE, Settings},
};
constexpr int kHomeMenuItemCount = 6;

constexpr const HomeMenuEntry* menuEntryAtIndex(int index, bool hasOpds, bool carousel) {
  if (index < 0) return nullptr;
  if (!hasOpds && index >= 2) ++index;
  if (index >= kHomeMenuItemCount) return nullptr;
  return &(carousel ? kCarouselMenuOrder : kDefaultMenuOrder)[index];
}

constexpr HomeMenuItem indexToMenuItem(int index, bool hasOpds, bool carousel) {
  const HomeMenuEntry* entry = menuEntryAtIndex(index, hasOpds, carousel);
  return entry == nullptr ? HomeMenuItem::NONE : entry->item;
}

constexpr int menuItemToIndex(HomeMenuItem item, bool hasOpds, bool carousel) {
  const int count = hasOpds ? kHomeMenuItemCount : kHomeMenuItemCount - 1;
  for (int i = 0; i < count; ++i) {
    if (indexToMenuItem(i, hasOpds, carousel) == item) return i;
  }
  return 0;
}

static_assert(indexToMenuItem(2, false, true) == HomeMenuItem::APPS);
static_assert(indexToMenuItem(4, false, true) == HomeMenuItem::SETTINGS_MENU);
static_assert(indexToMenuItem(3, true, true) == HomeMenuItem::APPS);
static_assert(indexToMenuItem(5, true, true) == HomeMenuItem::SETTINGS_MENU);
static_assert(indexToMenuItem(2, false, false) == HomeMenuItem::FILE_TRANSFER);
static_assert(indexToMenuItem(4, false, false) == HomeMenuItem::APPS);
static_assert(menuItemToIndex(HomeMenuItem::APPS, false, true) == 2);
static_assert(menuItemToIndex(HomeMenuItem::SETTINGS_MENU, true, true) == 5);
}  // namespace

int HomeActivity::getMenuItemCount() const {
  int count = 5;  // File Browser, Recents, File transfer, Settings, Apps
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsServers) {
    count++;
  }
  return count;
}

void HomeActivity::requestCarouselUpdate(CarouselUpdateScope scope) {
  if (scope == CarouselUpdateScope::Full) {
    carouselUpdateScope = scope;
  } else {
    // A queued full redraw must not be downgraded by a later menu event.
    CarouselUpdateScope expected = CarouselUpdateScope::None;
    carouselUpdateScope.compare_exchange_strong(expected, CarouselUpdateScope::MenuOnly);
  }
  requestUpdate();
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;
  const bool useFullCover =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    const int currentProgress = progress++;
    const bool isEpub = FsHelpers::hasEpubExtension(book.path);
    const bool isXtc = FsHelpers::hasXtcExtension(book.path);

    // Keep the persisted path theme-neutral; Carousel redirects only this activity's copy.
    if (isEpub) {
      const Epub epub(book.path, "/.crosspoint");
      if (useFullCover) {
        const std::string thumbPath = epub.getThumbBmpPath();
        if (book.coverBmpPath != thumbPath) {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, thumbPath);
        }
        book.coverBmpPath = epub.getCoverBmpPath();
      } else if (book.coverBmpPath.empty()) {
        book.coverBmpPath = epub.getThumbBmpPath();
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
      }
    } else if (isXtc) {
      const Xtc xtc(book.path, "/.crosspoint");
      if (useFullCover) {
        const std::string thumbPath = xtc.getThumbBmpPath();
        if (book.coverBmpPath != thumbPath) {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, thumbPath);
        }
        book.coverBmpPath = xtc.getCoverBmpPath();
      } else if (book.coverBmpPath.empty()) {
        book.coverBmpPath = xtc.getThumbBmpPath();
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
      }
    } else {
      continue;
    }
    if (book.coverBmpPath.empty()) continue;

    const std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
    if (Storage.exists(coverPath.c_str())) {
      bool invalidCache = false;
      {
        HalFile cachedCover;
        if (!Storage.openFileForRead("HOME", coverPath, cachedCover)) {
          LOG_ERR("HOME", "Failed to open cached cover: %s", coverPath.c_str());
          continue;
        }
        if (cachedCover.fileSize() == 0) {
          // EPUB uses an empty thumbnail as a persistent "no supported cover" marker.
          if (isEpub && !useFullCover) continue;
          invalidCache = true;
        } else {
          Bitmap bitmap(cachedCover);
          invalidCache =
              bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0;
        }
      }
      if (!invalidCache) continue;
      LOG_ERR("HOME", "Removing invalid cached cover: %s", coverPath.c_str());
      if (!Storage.remove(coverPath.c_str())) {
        LOG_ERR("HOME", "Failed to remove invalid cached cover: %s", coverPath.c_str());
        continue;
      }
    }

    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    }
    GUI.fillPopupProgress(renderer, popupRect, 10 + currentProgress * (90 / recentBooks.size()));
    std::string generatedPath;
    if (isEpub) {
      {
        GfxRenderer::FrameBufferLoan loan(renderer);
        generatedPath = useFullCover ? BookCoverLoader::ensureFullCover(book.path)
                                     : BookCoverLoader::ensureThumbnail(book.path, coverHeight);
      }
      // Inflate used the old framebuffer bytes. Rebuild a complete, known
      // loading frame before the next progress refresh can reach the panel.
      renderer.clearScreen();
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 10 + currentProgress * (90 / recentBooks.size()));
    } else {
      generatedPath = useFullCover ? BookCoverLoader::ensureFullCover(book.path)
                                   : BookCoverLoader::ensureThumbnail(book.path, coverHeight);
    }
    if (generatedPath.empty() && isXtc) LOG_ERR("HOME", "Failed to generate XTC cover: %s", book.path.c_str());
  }

  recentsLoaded = true;
  recentsLoading = false;
  coverRendered = false;
  coverBufferStored = false;
  requestCarouselUpdate(CarouselUpdateScope::Full);
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  hasOpdsServers = OPDS_STORE.hasServers();

  const auto& metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  const auto base = static_cast<int>(recentBooks.size());
  const bool isCarousel =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;
  selectorIndex =
      initialMenuItem == HomeMenuItem::NONE ? 0 : base + menuItemToIndex(initialMenuItem, hasOpdsServers, isCarousel);
  lastCarouselBookIndex = 0;

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  if (coverBufferUnavailable) return false;

  // Thumbnail generation may borrow the framebuffer; cache only the final render.
  if (!recentsLoaded) return false;

  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;

  if (!coverBuffer || coverBufferSize < needed) {
    // The carousel region is up to ~44 KB, too large for the task stack. Allocate
    // once and reuse it for every selection during this HomeActivity lifetime.
    auto replacement = makeUniqueNoThrow<uint8_t[]>(needed);
    if (!replacement) {
      LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
      // ponytail: the theme/region is fixed for this Activity lifetime; retry
      // only after re-entering Home, when heap fragmentation may have changed.
      coverBufferUnavailable = true;
      return false;
    }
    coverBuffer = std::move(replacement);
    coverBufferSize = needed;
  }

  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer.get(),
                                   coverBufferSize)) {
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer.get(),
                                     coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  coverBuffer.reset();
  coverBufferSize = 0;
  coverBufferStored = false;
  coverBufferUnavailable = false;
}

void HomeActivity::loop() {
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bookCount = static_cast<int>(recentBooks.size());
  const int renderedMenuCount = menuCount - (metrics.homeContinueReadingInMenu ? 0 : bookCount);
  const bool isCarousel =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

  auto activateSelection = [this, isCarousel] {
    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
      return;
    }
    const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
    switch (indexToMenuItem(menuIndex, hasOpdsServers, isCarousel)) {
      case HomeMenuItem::FILE_BROWSER:
        onFileBrowserOpen();
        break;
      case HomeMenuItem::RECENTS:
        onRecentsOpen();
        break;
      case HomeMenuItem::OPDS_BROWSER:
        onOpdsBrowserOpen();
        break;
      case HomeMenuItem::FILE_TRANSFER:
        onFileTransferOpen();
        break;
      case HomeMenuItem::SETTINGS_MENU:
        onSettingsOpen();
        break;
      case HomeMenuItem::APPS:
        onAppsOpen();
        break;
      default:
        break;
    }
  };

  if (isCarousel) {
    const bool coversFocused = selectorIndex < bookCount;
    const int rowIndex = coversFocused ? selectorIndex : selectorIndex - bookCount;

    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      if (coversFocused) {
        selectorIndex = ButtonNavigator::nextIndex(rowIndex, bookCount);
        lastCarouselBookIndex = selectorIndex;
      } else {
        selectorIndex = bookCount + ButtonNavigator::nextIndex(rowIndex, renderedMenuCount);
      }
      requestCarouselUpdate(coversFocused ? CarouselUpdateScope::Full : CarouselUpdateScope::MenuOnly);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      if (coversFocused) {
        selectorIndex = ButtonNavigator::previousIndex(rowIndex, bookCount);
        lastCarouselBookIndex = selectorIndex;
      } else {
        selectorIndex = bookCount + ButtonNavigator::previousIndex(rowIndex, renderedMenuCount);
      }
      requestCarouselUpdate(coversFocused ? CarouselUpdateScope::Full : CarouselUpdateScope::MenuOnly);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (bookCount > 0) {
        if (coversFocused) {
          lastCarouselBookIndex = selectorIndex;
          selectorIndex = bookCount;
        } else {
          selectorIndex = std::clamp(lastCarouselBookIndex, 0, bookCount - 1);
        }
        requestCarouselUpdate(CarouselUpdateScope::Full);
      }
      return;
    }
  } else {
    buttonNavigator.onNext([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });
  }

  const auto swipe = mappedInput.wasSwipe();
  if (isCarousel) {
    const bool coversFocused = selectorIndex < bookCount;
    const int rowIndex = coversFocused ? selectorIndex : selectorIndex - bookCount;
    switch (swipe) {
      case MappedInputManager::SwipeDir::Left:
        if (coversFocused) {
          selectorIndex = ButtonNavigator::nextIndex(rowIndex, bookCount);
          lastCarouselBookIndex = selectorIndex;
        } else {
          selectorIndex = bookCount + ButtonNavigator::nextIndex(rowIndex, renderedMenuCount);
        }
        requestCarouselUpdate(coversFocused ? CarouselUpdateScope::Full : CarouselUpdateScope::MenuOnly);
        return;
      case MappedInputManager::SwipeDir::Right:
        if (coversFocused) {
          selectorIndex = ButtonNavigator::previousIndex(rowIndex, bookCount);
          lastCarouselBookIndex = selectorIndex;
        } else {
          selectorIndex = bookCount + ButtonNavigator::previousIndex(rowIndex, renderedMenuCount);
        }
        requestCarouselUpdate(coversFocused ? CarouselUpdateScope::Full : CarouselUpdateScope::MenuOnly);
        return;
      case MappedInputManager::SwipeDir::Up:
      case MappedInputManager::SwipeDir::Down:
        if (bookCount > 0) {
          if (coversFocused) {
            lastCarouselBookIndex = selectorIndex;
            selectorIndex = bookCount;
          } else {
            selectorIndex = std::clamp(lastCarouselBookIndex, 0, bookCount - 1);
          }
          requestCarouselUpdate(CarouselUpdateScope::Full);
        }
        return;
      case MappedInputManager::SwipeDir::None:
        break;
    }
  } else {
    if (swipe == MappedInputManager::SwipeDir::Up) {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
      return;
    }
    if (swipe == MappedInputManager::SwipeDir::Down) {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
      return;
    }
  }

  int tx = 0;
  int ty = 0;
  if (!recentBooks.empty() && mappedInput.wasScreenTouchDown(tx, ty) && tx >= 0 && tx < renderer.getScreenWidth() &&
      ty >= metrics.homeTopPadding && ty < metrics.homeTopPadding + metrics.homeCoverTileHeight) {
    int touchedBook = 0;
    if (isCarousel) {
      const int centerBook =
          selectorIndex < bookCount ? selectorIndex : std::clamp(lastCarouselBookIndex, 0, bookCount - 1);
      if (tx < renderer.getScreenWidth() / 3) {
        touchedBook = ButtonNavigator::previousIndex(centerBook, bookCount);
      } else if (tx >= renderer.getScreenWidth() * 2 / 3) {
        touchedBook = ButtonNavigator::nextIndex(centerBook, bookCount);
      } else {
        touchedBook = centerBook;
      }
      lastCarouselBookIndex = touchedBook;
    }
    if (selectorIndex != touchedBook) {
      selectorIndex = touchedBook;
      requestCarouselUpdate(CarouselUpdateScope::Full);
    }
    return;
  }

  if (!recentBooks.empty() &&
      mappedInput.wasTapInRect(0, metrics.homeTopPadding, renderer.getScreenWidth(), metrics.homeCoverTileHeight)) {
    if (!isCarousel) selectorIndex = 0;
    activateSelection();
    return;
  }

  const int menuTop = metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset;
  const int renderedMenuSelection =
      metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size();
  int menuRow = -1;
  MappedInputManager::RowTouch menuTouch;
  if (isCarousel) {
    const int menuBottom = renderer.getScreenHeight();
    const int columnWidth = renderer.getScreenWidth() / renderedMenuCount;
    menuTouch = mappedInput.colTouch(menuRow, 0, columnWidth, renderedMenuCount, menuBottom - metrics.menuRowHeight,
                                     menuBottom, columnWidth);
  } else {
    menuTouch = mappedInput.rowTouch(menuRow, menuTop, metrics.menuRowHeight + metrics.menuSpacing, renderedMenuCount,
                                     0, INT32_MAX, metrics.menuRowHeight);
  }
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        metrics.homeContinueReadingInMenu ? menuRow : menuRow + static_cast<int>(recentBooks.size());
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        if (isCarousel) {
          requestCarouselUpdate(CarouselUpdateScope::MenuOnly);
        } else {
          requestUpdate();
        }
      }
    } else {
      selectorIndex = touchedIndex;
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }

  if (!SETTINGS.standbyShortcutEnabled) {
    sawBackPressInActivity = false;
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    sawBackPressInActivity = true;
  }
  if (sawBackPressInActivity && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    sawBackPressInActivity = false;
    onStandbyOpen();
  }
}

void HomeActivity::render(RenderLock&&) {
  static_assert(canRenderCarouselMenuOnly(true, true, CarouselUpdateScope::MenuOnly));
  static_assert(!canRenderCarouselMenuOnly(false, true, CarouselUpdateScope::MenuOnly));
  static_assert(!canRenderCarouselMenuOnly(true, false, CarouselUpdateScope::MenuOnly));
  static_assert(!canRenderCarouselMenuOnly(true, true, CarouselUpdateScope::Full));

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const bool isCarousel =
      static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_CAROUSEL;

  const int homeMenuItemCount = hasOpdsServers ? kHomeMenuItemCount : kHomeMenuItemCount - 1;
  const bool showContinueReading = metrics.homeContinueReadingInMenu && !recentBooks.empty();
  std::vector<const char*> menuItems;
  std::vector<UIIcon> menuIcons;
  menuItems.reserve(homeMenuItemCount + (showContinueReading ? 1 : 0));
  menuIcons.reserve(homeMenuItemCount + (showContinueReading ? 1 : 0));
  for (int i = 0; i < homeMenuItemCount; ++i) {
    const HomeMenuEntry* entry = menuEntryAtIndex(i, hasOpdsServers, isCarousel);
    menuItems.push_back(I18N.get(entry->label));
    menuIcons.push_back(entry->icon);
  }

  if (showContinueReading) {
    menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
    menuIcons.insert(menuIcons.begin(), Book);
  }

  const Rect headerRect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding};
  const Rect menuRect{0, metrics.homeTopPadding + metrics.homeCoverTileHeight + metrics.homeMenuTopOffset, pageWidth,
                      pageHeight - (metrics.headerHeight + metrics.homeTopPadding + metrics.verticalSpacing +
                                    metrics.homeMenuTopOffset + metrics.buttonHintsHeight)};
  auto drawHeader = [&] {
    GUI.drawHeader(renderer, headerRect,
                   metrics.homeShowRecentBookTitle && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr);
  };
  auto drawMenu = [&] {
    GUI.drawHomeMenu(
        renderer, menuRect, static_cast<int>(menuItems.size()),
        metrics.homeContinueReadingInMenu ? selectorIndex : selectorIndex - recentBooks.size(),
        [&menuItems](int index) { return std::string(menuItems[index]); },
        [&menuIcons](int index) { return menuIcons[index]; });
  };

  const CarouselUpdateScope updateScope = carouselUpdateScope.exchange(CarouselUpdateScope::None);
  const bool menuOnlyUpdate = canRenderCarouselMenuOnly(isCarousel, recentsLoaded, updateScope);
  if (menuOnlyUpdate) {
    renderer.fillRect(headerRect.x, headerRect.y, headerRect.width, headerRect.height, false);
    drawHeader();
    renderer.fillRect(0, pageHeight - metrics.menuRowHeight, pageWidth, metrics.menuRowHeight, false);
    drawMenu();
    renderer.displayBuffer();
    return;
  }

  if (isCarousel) {
    coverRendered = false;
    coverBufferStored = false;
  }

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  drawHeader();

  // Record the tile rect so storeCoverBuffer (called from the theme) knows
  // which sub-region of the framebuffer to snapshot. ~16 KB in Portrait
  // instead of the 48 KB full framebuffer the previous bind captured.
  coverRectX = 0;
  coverRectY = metrics.homeTopPadding;
  coverRectW = pageWidth;
  coverRectH = metrics.homeCoverTileHeight;

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  drawMenu();

  if (!isCarousel) {
    const auto labels = mappedInput.mapLabels(SETTINGS.standbyShortcutEnabled ? tr(STR_STANDBY_TITLE) : "",
                                              tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}

void HomeActivity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onRecentsOpen() { activityManager.goToRecentBooks(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onAppsOpen() { activityManager.goToApps(); }

void HomeActivity::onStandbyOpen() { activityManager.goToStandby(); }
