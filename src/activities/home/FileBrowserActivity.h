#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class FileBrowserActivity final : public UiListActivity {
 public:
  enum class Mode { Books, PickFirmware, PickPng };

 private:
  enum class EditAction : uint8_t { Rename, Move, Delete };
  enum class BrowserState : uint8_t { Browsing, ChoosingMoveDestination };

  // Deletion
  bool removeDirFile(const std::string& fullPath);
  void promptDelete(const std::string& fullPath, const std::string& entry);

  void showEditMenu();
  void executeEditAction(EditAction action);
  void promptRename();
  void beginMove();
  void cancelMove();
  void completeMove();
  bool relocateDirectoryData(const std::string& oldPath, const std::string& newPath);
  bool relocatePathData(const std::string& oldPath, const std::string& newPath, bool isDirectory);
  void finishEdit(const std::string& returnPath, int fallbackIndex, const std::string& selectedEntry = "");
  void showNotice(StrId message);
  std::string selectedPath() const;

  Mode mode = Mode::Books;
  BrowserState browserState = BrowserState::Browsing;
  OptionPopup editPopup;

  std::string moveSourcePath;
  std::string moveSourceEntry;
  std::string moveReturnPath;
  int moveReturnIndex = 0;
  bool moveSourceIsDirectory = false;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  // Per-row render buffers, derived from `files` and rebuilt only when it
  // changes (loadFiles()) rather than on every repaint — buildScreen() used to
  // rebuild a name/extension string and a ListItem per file on every render
  // (cursor move, tap flash, ...), which meant a 500-file directory allocated
  // 500 strings per repaint instead of once per directory load.
  std::vector<std::string> rowNames;
  std::vector<std::string> rowExtensions;
  std::vector<std::string> gridLabels;
  std::vector<freeink::ui::ListItem> rowItems;
  // getFileName()'s "[folder]" bracket formatting depends on the active
  // theme's showsFileIcons(); tracked so a theme change while this activity is
  // paused underneath (e.g. a Settings screen reached via a picker flow)
  // invalidates the cached rows on return instead of rendering stale ones.
  bool rowsUseFileIcons = false;

  void rebuildRowItems();
  bool usesIconLayout() const;
  void drawIconGrid(UiScreen& screen, freeink::ui::Rect rect) const;

  int listCount() const override { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Long-press BACK goes to root; short Back goes up a directory (home/cancel at
  // root), and Confirm activates on RELEASE (a hold is "delete").
  bool handleCustomInput() override;
  bool handleButtons() override;
  // Header shows the current folder name (battery indicator via GUI.drawHeader);
  // footer labels depend on path depth and picker mode.
  void drawChrome() override;
  void drawFooter() override;
  void activateSelected();

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books);
  void onEnter() override;
  void onExit() override;
  void render(RenderLock&& lock) override;
  MainTab mainTab() const override {
    return mode == Mode::Books && browserState == BrowserState::Browsing ? MainTab::Library : MainTab::None;
  }
  bool mainTabBackReturnsToTabs() const override { return browserState == BrowserState::Browsing && basepath == "/"; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;
};
