#pragma once

#include "components/themes/lyra/LyraTheme.h"

namespace InxMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics metrics = {.batteryWidth = 16,
                          .batteryHeight = 12,
                          .topPadding = 5,
                          .batteryBarHeight = 40,
                          .headerHeight = 84,
                          .verticalSpacing = 16,
                          .previewPadding = 12,
                          .previewHeightPercent = 30,
                          .contentSidePadding = 20,
                          .listRowHeight = 40,
                          .listWithSubtitleRowHeight = 60,
                          .listRowGap = 0,
                          .listRowRadius = 6,
                          .listInset = 20,
                          .listSidePadding = 8,
                          .listSelectionStyle = 1,
                          .listScrollWidth = 4,
                          .listScrollSide = 0,
                          .listTitleBold = false,
                          .headerSidePadding = 18,
                          .headerUnderlineSize = 3,
                          .headerTitleAlign = 0,
                          .headerBatterySide = 0,
                          .menuRowHeight = 64,
                          .menuSpacing = 8,
                          .tabSpacing = 8,
                          .tabBarHeight = 40,
                          .scrollBarWidth = 4,
                          .scrollBarRightOffset = 5,
                          .homeTopPadding = 56,
                          .homeCoverHeight = 226,
                          .homeCoverTileHeight = 242,
                          .homeRecentBooksCount = 1,
                          .homeShowRecentBookTitle = false,
                          .homeContinueReadingInMenu = false,
                          .homeMenuTopOffset = 16,
                          .buttonHintsHeight = 40,
                          .sideButtonHintsWidth = 30,
                          .progressBarHeight = 16,
                          .progressBarMarginTop = 1,
                          .statusBarHorizontalMargin = 5,
                          .statusBarVerticalMargin = 19,
                          .keyboardKeyHeight = 48,
                          .keyboardKeySpacing = 0,
                          .keyboardCenteredText = false,
                          .keyboardVerticalOffset = -7,
                          .keyboardTextFieldWidthPercent = 85,
                          .keyboardWidthPercent = 94,
                          .popupTopOffsetRatio = 0.165f,
                          .popupMarginX = 16,
                          .popupMarginY = 12,
                          .popupFrameThickness = 2,
                          .popupCornerRadius = 6,
                          .popupTextBold = false,
                          .popupTextInverted = false,
                          .popupTextBaselineOffsetY = -2,
                          .popupProgressBarHeight = 4,
                          .popupProgressDrawOutline = false,
                          .popupProgressClampPercent = false,
                          .popupProgressFillInverted = false,
                          .popupProgressOutlineInverted = false,
                          .optionPopupItemSpacing = 8,
                          .optionPopupInnerPadding = 20,
                          .optionPopupSelectionHPadding = 16,
                          .optionPopupSelectionVPadding = 12,
                          .optionPopupTitleGap = 16,
                          .optionPopupUseSmallFont = true,
                          .optionPopupOptionFontBold = false,
                          .optionPopupSelectionRadius = 6,
                          .optionPopupSelectionLight = true,
                          .optionPopupDrawAllRows = false,
                          .optionPopupDialogSideMargin = 20,
                          .optionPopupTitleSeparator = true,
                          .textFieldHorizontalPadding = 6,
                          .textFieldNormalThickness = 1,
                          .textFieldCursorThickness = 3,
                          .textFieldLineEndOffset = 0,
                          .controlRadius = 6,
                          .sheetRadius = 6,
                          .capsuleRadius = 6,
                          .headerBatteryDetached = true};
  metrics.topPadding = 0;
  metrics.batteryBarHeight = 24;
  metrics.headerHeight = 66;
  metrics.verticalSpacing = 0;
  metrics.contentSidePadding = 20;
  metrics.listRowHeight = 66;
  metrics.listWithSubtitleRowHeight = 66;
  metrics.listRowGap = 0;
  metrics.listRowRadius = 0;
  metrics.listInset = 0;
  metrics.listSidePadding = 20;
  metrics.listSelectionStyle = 0;
  metrics.listScrollWidth = 6;
  metrics.listScrollSide = 0;
  metrics.listTitleBold = false;
  metrics.listSeparatorStyle = 2;
  metrics.listValueMaxWidth = 200;
  metrics.listSelectionCoversScrollReservation = true;
  metrics.tabBarHeight = 40;
  metrics.menuRowHeight = 66;
  metrics.menuSpacing = 0;
  metrics.scrollBarWidth = 6;
  metrics.scrollBarRightOffset = 2;
  return metrics;
}
inline constexpr ThemeMetrics values = makeValues();
}  // namespace InxMetrics

class InxTheme final : public LyraTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title, const char* subtitle = nullptr,
                  bool backButton = true) const override;
  void drawSubHeader(const GfxRenderer& renderer, Rect rect, const char* label,
                     const char* rightLabel = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  bool tabIndexFromPoint(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs, int x, int y,
                         int& index) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const override;
  int getListRowStep(bool hasSubtitle) const override;
  int getListPageItems(int contentHeight, bool hasSubtitle) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue, const std::function<bool(int index)>& rowDimmed = nullptr,
                bool showSelection = true, const std::function<bool(int index)>& rowHeading = nullptr) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon, int rowSpacing = -1) const override;
  MenuRowGeometry getMenuRowGeometry(const GfxRenderer& renderer, const Rect& rect, int selectedIndex,
                                     int rowCount) const override;
  void drawOptionPopup(const GfxRenderer& renderer, const char* title, const std::vector<std::string>& options,
                       int selectedIndex) const override;
  void drawMainTabBar(const GfxRenderer& renderer, Rect rect, MainTab selected) const override;
  void drawMainTabStatusBar(const GfxRenderer& renderer, Rect rect) const override;
};
