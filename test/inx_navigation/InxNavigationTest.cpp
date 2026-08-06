#include <gtest/gtest.h>

#include "InxItemLayout.h"
#include "InxRecentLayout.h"
#include "activities/MainTab.h"
#include "components/themes/BaseTheme.h"

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
}
