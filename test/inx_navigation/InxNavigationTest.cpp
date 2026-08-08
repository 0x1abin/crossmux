#include <gtest/gtest.h>

#include "InxItemLayout.h"
#include "InxRecentLayout.h"
#include "activities/MainTab.h"
#include "components/SubpageLayout.h"
#include "components/icons/inx_apps.h"
#include "components/themes/BaseTheme.h"

namespace {
constexpr uint32_t iconHash(const InxAppIcons::Icon& icon) {
  uint32_t hash = 2166136261u;
  for (const uint8_t byte : icon) hash = (hash ^ byte) * 16777619u;
  return hash;
}

constexpr bool isSolid(const InxAppIcons::Icon& icon, const uint8_t value) {
  for (const uint8_t byte : icon) {
    if (byte != value) return false;
  }
  return true;
}
}  // namespace

TEST(InxNavigation, WrapsAcrossFiveTabs) {
  EXPECT_EQ(MainTabs::adjacent(MainTab::Recent, -1), MainTab::Statistics);
  EXPECT_EQ(MainTabs::adjacent(MainTab::Statistics, 1), MainTab::Recent);
  EXPECT_EQ(MainTabs::adjacent(MainTab::Library, 1), MainTab::Apps);
  EXPECT_EQ(MainTabs::fromX(0, 500), MainTab::Recent);
  EXPECT_EQ(MainTabs::fromX(100, 500), MainTab::Library);
  EXPECT_EQ(MainTabs::fromX(200, 500), MainTab::Apps);
  EXPECT_EQ(MainTabs::fromX(300, 500), MainTab::Settings);
  EXPECT_EQ(MainTabs::fromX(499, 500), MainTab::Statistics);
  EXPECT_EQ(MainTabs::fromX(500, 500), MainTab::None);
  EXPECT_EQ(MainTabs::backTarget(MainTab::Apps), MainTab::Recent);
  EXPECT_EQ(MainTabs::backTarget(MainTab::Recent), MainTab::None);
  EXPECT_EQ(MainTabs::contentEdgeIndex(MainTabContentEdge::First, 0), 0);
  EXPECT_EQ(MainTabs::contentEdgeIndex(MainTabContentEdge::First, 10), 0);
  EXPECT_EQ(MainTabs::contentEdgeIndex(MainTabContentEdge::Last, 1), 0);
  EXPECT_EQ(MainTabs::contentEdgeIndex(MainTabContentEdge::Last, 10), 9);
}

TEST(InxNavigation, KeepsRecentPagesInsideBounds) {
  constexpr InxRecentLayout layouts[] = {InxRecentLayout::Flow, InxRecentLayout::Grid, InxRecentLayout::List,
                                         InxRecentLayout::Icons, InxRecentLayout::Cover};
  for (const InxRecentLayout layout : layouts) {
    EXPECT_EQ(InxRecentGeometry::pageStart(0, 0, layout), 0);
    EXPECT_EQ(InxRecentGeometry::pageStart(0, 1, layout), 0);
    const int start = InxRecentGeometry::pageStart(9, 10, layout);
    EXPECT_GE(start, 0);
    EXPECT_LE(start, 9);
    EXPECT_EQ(start % InxRecentGeometry::itemsPerPage(layout), 0);
  }
}

TEST(InxNavigation, FitsOriginalCoverRatioInsideBounds) {
  EXPECT_EQ(InxCoverGeometry::fit(0, 250).width, 0);
  EXPECT_EQ(InxCoverGeometry::fit(170, 250).width, 170);
  EXPECT_EQ(InxCoverGeometry::fit(170, 250).height, 250);
  EXPECT_EQ(InxCoverGeometry::fit(300, 300).width, 204);
  EXPECT_EQ(InxCoverGeometry::fit(300, 300).height, 300);
  EXPECT_EQ(InxCoverGeometry::fit(100, 500).width, 100);
  EXPECT_EQ(InxCoverGeometry::fit(100, 500).height, 147);
  EXPECT_EQ(InxCoverGeometry::fit(96, 140).width, 95);
  EXPECT_EQ(InxCoverGeometry::fit(96, 140).height, 140);
}

TEST(InxNavigation, SizesRecentThumbnailsForCropFillWithoutUpscaling) {
  EXPECT_EQ(InxCoverGeometry::thumbnailHeightForCropFill(0), 0);
  EXPECT_EQ(InxCoverGeometry::thumbnailHeightForCropFill(128), 146);
  EXPECT_EQ(InxCoverGeometry::thumbnailHeightForCropFill(209), 237);
  EXPECT_EQ(InxCoverGeometry::thumbnailHeightForCropFill(318), 361);
  EXPECT_EQ(InxCoverGeometry::thumbnailHeightForCropFill(326), 370);
  EXPECT_EQ(InxCoverGeometry::thumbnailHeightForCropFill(550), 624);
  EXPECT_EQ(InxCoverGeometry::thumbnailHeightForCropFill(604), 685);
}

TEST(InxNavigation, ValidatesItemLayoutsAndGridBounds) {
  EXPECT_EQ(InxGridGeometry::layoutFrom(0), InxItemLayout::Icons);
  EXPECT_EQ(InxGridGeometry::layoutFrom(1), InxItemLayout::List);
  EXPECT_EQ(InxGridGeometry::layoutFrom(2), InxItemLayout::Icons);
  EXPECT_EQ(InxGridGeometry::layoutFrom(255), InxItemLayout::Icons);

  EXPECT_EQ(InxGridGeometry::pageStart(0, 0), 0);
  EXPECT_EQ(InxGridGeometry::pageStart(0, 1), 0);
  EXPECT_EQ(InxGridGeometry::pageStart(11, 12), 0);
  EXPECT_EQ(InxGridGeometry::pageStart(12, 13), 12);
  EXPECT_EQ(InxGridGeometry::pageStart(14, 15), 12);

  EXPECT_EQ(InxGridGeometry::indexFromPoint(0, 0, 300, 400, 12, 15), 12);
  EXPECT_EQ(InxGridGeometry::indexFromPoint(299, 99, 300, 400, 12, 15), 14);
  EXPECT_EQ(InxGridGeometry::indexFromPoint(0, 100, 300, 400, 12, 15), -1);
  EXPECT_EQ(InxGridGeometry::indexFromPoint(-1, 0, 300, 400, 0, 12), -1);
  EXPECT_EQ(InxGridGeometry::indexFromPoint(300, 0, 300, 400, 0, 12), -1);
}

TEST(InxNavigation, MapsAccordionRowsWithoutFlatteningSettings) {
  constexpr std::array<int, 4> counts = {2, 1, 0, 3};
  constexpr uint8_t noneExpanded = 0;
  constexpr uint8_t displayAndSystem = (uint8_t{1} << 0) | (uint8_t{1} << 3);

  EXPECT_EQ(InxAccordionGeometry::visibleCount(counts, noneExpanded), 4);
  EXPECT_EQ(InxAccordionGeometry::rowAt(counts, noneExpanded, 2).category, 2);
  EXPECT_TRUE(InxAccordionGeometry::rowAt(counts, noneExpanded, 2).isCategory());

  EXPECT_EQ(InxAccordionGeometry::visibleCount(counts, displayAndSystem), 9);
  EXPECT_EQ(InxAccordionGeometry::rowAt(counts, displayAndSystem, 1).category, 0);
  EXPECT_EQ(InxAccordionGeometry::rowAt(counts, displayAndSystem, 1).setting, 0);
  EXPECT_EQ(InxAccordionGeometry::rowAt(counts, displayAndSystem, 7).category, 3);
  EXPECT_EQ(InxAccordionGeometry::rowAt(counts, displayAndSystem, 7).setting, 1);
  EXPECT_EQ(InxAccordionGeometry::categoryRow(counts, displayAndSystem, 3), 5);
}

TEST(InxNavigation, KeepsStatisticsViewsInsideBounds) {
  EXPECT_EQ(InxStatisticsGeometry::viewCount(0), 1);
  EXPECT_EQ(InxStatisticsGeometry::viewCount(1), 2);
  EXPECT_EQ(InxStatisticsGeometry::viewCount(10), 11);

  EXPECT_EQ(InxStatisticsGeometry::adjacentView(0, 0, 1), 0);
  EXPECT_EQ(InxStatisticsGeometry::adjacentView(0, 1, -1), 1);
  EXPECT_EQ(InxStatisticsGeometry::adjacentView(1, 1, 1), 0);
  EXPECT_EQ(InxStatisticsGeometry::adjacentView(0, 10, -1), 10);
  EXPECT_EQ(InxStatisticsGeometry::adjacentView(10, 10, 1), 0);

  EXPECT_EQ(InxStatisticsGeometry::clampView(10, 9), 9);
  EXPECT_EQ(InxStatisticsGeometry::clampView(1, 0), 0);
  EXPECT_EQ(InxStatisticsGeometry::averageSessionMs(3000, 0), 0u);
  EXPECT_EQ(InxStatisticsGeometry::averageSessionMs(3000, 3), 1000u);
}

TEST(InxNavigation, PaginatesButtonMenusAndKeepsIconIdsStable) {
  EXPECT_EQ(InxMenuGeometry::pageItems(0), 1);
  EXPECT_EQ(InxMenuGeometry::pageItems(65), 1);
  EXPECT_EQ(InxMenuGeometry::pageItems(132), 2);
  EXPECT_EQ(InxMenuGeometry::pageStart(0, 5, 132), 0);
  EXPECT_EQ(InxMenuGeometry::pageStart(2, 5, 132), 2);
  EXPECT_EQ(InxMenuGeometry::pageStart(9, 5, 132), 4);

  EXPECT_EQ(static_cast<int>(UIIcon::ReadingHeatmap), static_cast<int>(UIIcon::AirPage) + 1);
  EXPECT_EQ(static_cast<int>(UIIcon::ReadingProfile), static_cast<int>(UIIcon::ReadingHeatmap) + 1);
  EXPECT_EQ(static_cast<int>(UIIcon::Achievements), static_cast<int>(UIIcon::ReadingProfile) + 1);
  EXPECT_EQ(static_cast<int>(UIIcon::Calculator), static_cast<int>(UIIcon::Achievements) + 1);
  EXPECT_EQ(static_cast<int>(UIIcon::Woodfish), static_cast<int>(UIIcon::Calculator) + 1);
}

TEST(InxNavigation, KeepsAppIconAssetsValidAndDistinct) {
  constexpr std::array<const InxAppIcons::Icon*, 17> icons = {
      &InxAppIcons::Transfer,    &InxAppIcons::Opds,        &InxAppIcons::WeRead,     &InxAppIcons::ReadingStats,
      &InxAppIcons::Sudoku,      &InxAppIcons::Gomoku,      &InxAppIcons::Sokoban,    &InxAppIcons::ChineseChess,
      &InxAppIcons::Minesweeper, &InxAppIcons::Game2048,    &InxAppIcons::Avatar,     &InxAppIcons::AirPage,
      &InxAppIcons::Buddy,       &InxAppIcons::PixelSwitch, &InxAppIcons::Calculator, &InxAppIcons::Standby,
      &InxAppIcons::Woodfish,
  };

  for (size_t index = 0; index < icons.size(); ++index) {
    EXPECT_EQ(icons[index]->size(), static_cast<size_t>(InxAppIcons::bytes));
    EXPECT_FALSE(isSolid(*icons[index], 0xff));
    EXPECT_FALSE(isSolid(*icons[index], 0x00));
    for (size_t other = index + 1; other < icons.size(); ++other) {
      EXPECT_NE(iconHash(*icons[index]), iconHash(*icons[other]));
    }
  }

  EXPECT_EQ(InxAppIcons::get(UIIcon::Transfer), InxAppIcons::Transfer.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Opds), InxAppIcons::Opds.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::ReadingStats), InxAppIcons::ReadingStats.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Sudoku), InxAppIcons::Sudoku.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Gomoku), InxAppIcons::Gomoku.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Sokoban), InxAppIcons::Sokoban.data());
#ifdef ENABLE_CHINESE_VERSION
  EXPECT_EQ(InxAppIcons::get(UIIcon::ChineseChess), InxAppIcons::ChineseChess.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::WeRead), InxAppIcons::WeRead.data());
#endif
  EXPECT_EQ(InxAppIcons::get(UIIcon::Minesweeper), InxAppIcons::Minesweeper.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Avatar), InxAppIcons::Avatar.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Standby), InxAppIcons::Standby.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Game2048), InxAppIcons::Game2048.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Buddy), InxAppIcons::Buddy.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::PixelSwitch), InxAppIcons::PixelSwitch.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::AirPage), InxAppIcons::AirPage.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Calculator), InxAppIcons::Calculator.data());
  EXPECT_EQ(InxAppIcons::get(UIIcon::Woodfish), InxAppIcons::Woodfish.data());
}

TEST(InxNavigation, KeepsSubpageContentInsideChrome) {
  ThemeMetrics metrics{};
  metrics.topPadding = 0;
  metrics.headerHeight = 66;
  metrics.tabBarHeight = 40;
  metrics.verticalSpacing = 0;

  EXPECT_EQ(SubpageLayout::relatedGap(metrics), 4);
  EXPECT_EQ(SubpageLayout::sectionGap(metrics), 12);

  const Rect body = SubpageLayout::contentRect(Rect{0, 0, 480, 760}, metrics, true, 24);
  EXPECT_EQ(body.y, 106);
  EXPECT_EQ(body.height, 630);

  const Rect inset = SubpageLayout::insetHorizontal(body, 20);
  EXPECT_EQ(inset.x, 20);
  EXPECT_EQ(inset.width, 440);
  EXPECT_EQ(SubpageLayout::centeredTop(body, 200), 321);
  EXPECT_EQ(SubpageLayout::centeredTop(body, 900), body.y);

  metrics.verticalSpacing = 16;
  const Rect spacedBody = SubpageLayout::contentRect(Rect{0, 0, 480, 760}, metrics, false);
  EXPECT_EQ(spacedBody.y, 82);
  EXPECT_EQ(spacedBody.height, 662);
}

TEST(InxNavigation, MeasuresCompleteProgressBlock) {
  EXPECT_EQ(ProgressBarGeometry::contentHeight(16, 24), 55);
  EXPECT_EQ(ProgressBarGeometry::contentHeight(16, 24, false), 16);
  EXPECT_EQ(ProgressBarGeometry::contentHeight(0, 24), 0);
}
