#pragma once

#include <SdCardFontRegistry.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "TextSettingsPreview.h"
#include "activities/UiTabListActivity.h"
#include "components/OptionPopup.h"
#include "components/themes/BaseTheme.h"

// Reader text settings with a shared live preview pane: tab bar
// (Font | Size | Layout | Style) is position 0 of the Up/Down nav ring, same
// idiom as SettingsActivity. Family/Size rows apply on Confirm; Layout/Style
// rows toggle or open an OptionPopup picker. (Tab::Family/Style are the enum
// names for the Font/Style tabs.)
class TextSettingsActivity final : public UiTabListActivity {
 public:
  enum class Tab : uint8_t { Family, Size, Layout, Style, Count };
  enum class InitialFontState : uint8_t { Unchanged, Changed };
  enum class StartMode : uint8_t { Interactive, PreloadThenExit };

  TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const SdCardFontRegistry* registry,
                       Tab initialTab = Tab::Family, InitialFontState initialFontState = InitialFontState::Unchanged,
                       StartMode startMode = StartMode::Interactive);

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return fontLoadState_.load() != FontLoadState::Idle; }
  bool handleHomeGesture() override;

 private:
  // Row indices per tab. enum class (not plain enum) so a LayoutRow can't be
  // silently confused with a StyleRow of equal value.
  enum class LayoutRow { LineSpacing, ParaSpacing, Alignment, ScreenMargin, Count };
  enum class StyleRow {
    FocusReading,
    ReadingGuideLine,
    ReadingGuideLineStyle,
    ReadingGuideLineOffset,
    Hyphenation,
    EmbeddedStyle,
    FakeBold,
    AntiAliasing,
    Count
  };
  static constexpr int HIDDEN_GUIDE_ROW_COUNT =
      static_cast<int>(StyleRow::Hyphenation) - static_cast<int>(StyleRow::ReadingGuideLineStyle);
  enum class FontLoadState : uint8_t { Idle, Preloading, Ready };
  enum class ExitDestination : uint8_t { Previous, Home };

  // --- UiTabListActivity contract ---
  int listCount() const override;
  int tabCount() const override { return static_cast<int>(Tab::Count); }
  int activeTab() const override { return static_cast<int>(tab_); }
  const char* tabLabel(int index) const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onTabAction(int index) override;
  void stepTab(int direction) override { switchTab(direction); }
  bool handleButtons() override;
  bool handleCustomInput() override;

  void applyFamily(int listIndex);
  void applySize(int listIndex);
  bool preloadFont(const SdCardFontFileInfo& file, const char* familyName);
  void exitAfterFinalFont(ExitDestination destination);
  void completeExit();
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

  std::string layoutValueText(int row) const;
  std::string styleValueText(int row) const;
  StyleRow styleRowAt(int visibleIndex) const;
  int styleRowCount() const;
  // Button-hint label for Confirm at the current ring position.
  const char* confirmLabelText() const;
  // True when the focused list row is a setting the preview cannot reflect.
  bool focusedRowHasNoPreview() const;
  void switchTab(int direction = 1);

  // Row storage for the active tab: rowItems_ (label/actionValue) is
  // rebuilt only when the tab or its backing data changes (rebuildRowItems(),
  // called from onEnter()/onTabAction()/switchTab()); rowValues_ holds the
  // live per-row value text, refreshed every buildScreen() call by assigning
  // into the existing strings (no vector growth), so steady-state rendering
  // never allocates/frees row storage.
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> rowItems_;
  void rebuildRowItems();

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
  OptionPopup optionPopup_;
  std::vector<FontEntry> fonts_;
  std::vector<SizeEntry> sizes_;
  textsettings::PreviewLayout previewLayout_;  // cached preview line layout; relaid only on setting/geometry change

  Tab tab_;
  int currentFamilyIndex_ = 0;
  int currentSizeIndex_ = 0;
  int initialFamilyIndex_ = 0;
  uint8_t initialPointSize_ = 0;
  uint8_t initialSdFontFlashPreload_ = 0;
  InitialFontState initialFontState_ = InitialFontState::Unchanged;
  StartMode startMode_ = StartMode::Interactive;

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
  ExitDestination exitDestination_ = ExitDestination::Previous;
  bool exitInProgress_ = false;
};
