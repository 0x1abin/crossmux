#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  // Picker modes filter to one file type and return its path via ActivityResult.
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
  void finishEdit(const std::string& returnPath, size_t fallbackIndex, const std::string& selectedEntry = "");
  void showNotice(StrId message);
  std::string selectedPath() const;

  ButtonNavigator buttonNavigator;
  OptionPopup editPopup;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;
  bool confirmHeld = false;
  bool confirmLongHandled = false;
  bool popupClosing = false;

  Mode mode = Mode::Books;
  BrowserState browserState = BrowserState::Browsing;

  std::string moveSourcePath;
  std::string moveSourceEntry;
  std::string moveReturnPath;
  size_t moveReturnIndex = 0;
  bool moveSourceIsDirectory = false;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  // Data loading
  void loadFiles();
  bool isPickerMode() const { return mode != Mode::Books; }
  size_t findEntry(const std::string& name) const;
  bool usesIconLayout() const;
  int iconIndexFromPoint(int x, int y, int contentTop, int contentHeight) const;
  void drawIconGrid(const Rect& rect, bool showSelection) const;
  std::string entryLabel(size_t index) const;
  std::string entryExtension(size_t index) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books)
      : Activity("FileBrowser", renderer, mappedInput),
        mode(mode),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  MainTab mainTab() const override {
    return mode == Mode::Books && browserState == BrowserState::Browsing ? MainTab::Library : MainTab::None;
  }
  bool mainTabBackReturnsToTabs() const override { return browserState == BrowserState::Browsing && basepath == "/"; }
  void selectMainTabContentEdge(MainTabContentEdge edge) override;
};
