#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "./FileBrowserActivity.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "components/CoverGridHomeUi.h"
#include "util/ButtonNavigator.h"

struct Rect;

class HomeActivity final : public Activity {
  enum class CarouselUpdateScope { None, MenuOnly, Full };

  std::unique_ptr<CoverGridHomeUi> coverGridUi;
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  bool recentsLoading = false;
  bool recentsLoaded = false;
  bool firstRenderDone = false;
  bool hasOpdsServers = false;
  bool hasPlugins = false;
  // The home "library" slot (index 2) shows Plugins when any plugin is
  // installed, otherwise OPDS. The index converters gate on its presence.
  bool hasLibrarySlot() const { return hasPlugins || hasOpdsServers; }
  bool hasContinueReading = false;
  int lastCarouselBookIndex = 0;
  bool coverRendered = false;           // Track if cover has been rendered once
  bool coverBufferStored = false;       // Track if cover buffer is stored
  bool coverBufferUnavailable = false;  // Stop retrying an optional snapshot after OOM
  std::atomic<CarouselUpdateScope> carouselUpdateScope{CarouselUpdateScope::None};
  std::unique_ptr<uint8_t[]> coverBuffer;  // HomeActivity's own buffer for cover image
  size_t coverBufferSize = 0;              // Bytes allocated to coverBuffer
  // Logical rect last passed to drawRecentBookCover. The cover snapshot only
  // needs to cover this region, not the entire framebuffer, so we cache the
  // tile instead of all 48 KB. Set in render() before the call.
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;

  void onSelectBook(const std::string& path);
  void onFileBrowserOpen();
  void onLibraryOpen();
  void onRecentsOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();
  void onAppsOpen();
  void onPluginsOpen();

  int getMenuItemCount() const;
  static constexpr bool canRenderCarouselMenuOnly(bool isCarousel, bool recentsLoaded, CarouselUpdateScope scope) {
    return isCarousel && recentsLoaded && scope == CarouselUpdateScope::MenuOnly;
  }
  void requestCarouselUpdate(CarouselUpdateScope scope);
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void loadRecentCovers(int coverHeight);
  void fillCoverGridFromLibrary();
  void resolveGridCoverPaths();
  void loadGridCover(RecentBook& book, int height, bool& showingLoading, Rect& popupRect);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE)
      : Activity("Home", renderer, mappedInput), initialMenuItem(initialMenuItemValue) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
