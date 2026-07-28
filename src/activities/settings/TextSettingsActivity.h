#pragma once

#include <SdCardFontRegistry.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "TextSettingsPreview.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

// Reader text settings with a shared live preview pane: tab bar
// (Font | Size | Layout | Style) is position 0 of the Up/Down nav ring, same
// idiom as SettingsActivity. Family/Size rows apply on Confirm; Layout/Style
// rows toggle or open an OptionPopup picker. (Tab::Family/Style are the enum
// names for the Font/Style tabs.)
class TextSettingsActivity final : public Activity {
 public:
  enum class Tab : uint8_t { Family, Size, Layout, Style, Count };

  TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SdCardFontRegistry* registry,
                       Tab initialTab = Tab::Family);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return fontLoadState_.load() != FontLoadState::Idle; }

 private:
  // Row indices per tab. enum class (not plain enum) so a LayoutRow can't be
  // silently confused with a StyleRow of equal value.
  enum class LayoutRow { LineSpacing, ParaSpacing, Alignment, ScreenMargin, Count };
  enum class StyleRow { FocusReading, Hyphenation, EmbeddedStyle, AntiAliasing, Count };

  enum class FontLoadState : uint8_t { Idle, Preloading, Ready };

  void applyFamily(int listIndex, bool forceReload = false);
  void applySize(int listIndex);
  void promptSdFamily(int listIndex);
  void selectSdFamily(int listIndex, bool preload);
  bool preloadFont(const SdCardFontFileInfo& file, const char* familyName);
  void finishPreload(bool succeeded);
  void showPreloadFailure();
  const SdCardFontFileInfo* fontFileForFamily(int listIndex, uint8_t pointSize) const;
#ifdef ENABLE_CHINESE_VERSION
  void maybeOfferCompleteChineseFont();
#endif
  // Repopulates sizes_ (and currentSizeIndex_) from the active family's
  // installed point sizes. Call after any family change.
  void rebuildSizeList();
  void confirmLayoutRow(int row);
  void confirmStyleRow(int row);
  // Applies the row at the given list index for the active tab (Confirm and tap share this).
  void activateRow(int row);

  // Handles tab/list/swipe touch input; returns true if an event was consumed (caller returns).
  bool handleTouch();

  // Vertical layout of the preview/tab-bar/list panes.
  // Shared by render() (to draw) and loop() (to hit-test touch) to avoid drift
  struct PaneGeometry {
    int previewTop;
    int tabTop;
    int listTop;
    int listHeight;
  };
  PaneGeometry paneGeometry() const;
  std::string layoutValueText(int row) const;
  std::string styleValueText(int row) const;
  // True when the focused list row is a setting the preview cannot reflect.
  bool focusedRowHasNoPreview() const;
  void updateTabs();
  void switchTab(int direction = 1);
  int currentListSize() const;
  // Navigation ring position for the active tab: 0 = tab bar, 1..N = list item N-1.
  int& selectedIndex();
  int selectedIndex() const;

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;
  };

  struct SizeEntry {
    std::string name;  // the point size, rendered for display ("14 pt")
    uint8_t pointSize;
  };

  const SdCardFontRegistry* registry_;
  ButtonNavigator buttonNavigator_;
  OptionPopup optionPopup_;
  std::vector<TabInfo> tabs_;
  std::vector<FontEntry> fonts_;
  std::vector<SizeEntry> sizes_;
  textsettings::PreviewLayout previewLayout_;  // cached preview line layout; relaid only on setting/geometry change

  Tab tab_;
  int selectedIndex_[static_cast<int>(Tab::Count)] =
      {};  // per-Tab nav position (0 = tab bar, 1..N = row); set in onEnter
  int currentFamilyIndex_ = 0;
  int currentSizeIndex_ = 0;

  ThemeMetrics metrics_ = {};
  int afterHeader = 0;
  int bottomReserved = 0;
  int usableHeight = 0;
  int previewHeight = 0;
  std::atomic<FontLoadState> fontLoadState_{FontLoadState::Idle};
  std::atomic<size_t> preloadCompleted_{0};
  std::atomic<size_t> preloadTotal_{1};
  const char* preloadFamilyName_ = "";
  uint8_t preloadPointSize_ = 0;
  bool preloadVerifying_ = false;
  unsigned lastPreloadPercent_ = 101;
};
