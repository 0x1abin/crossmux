#include <gtest/gtest.h>

#include "activities/apps/GameUi.h"

TEST(GameUiTouchGeometry, GridRejectsOutsideAndMapsEdges) {
  const Rect grid{10, 20, 90, 180};
  int row = -1;
  int column = -1;
  EXPECT_FALSE(gameGridCellFromPoint(grid, 9, 9, 9, 20, row, column));
  EXPECT_TRUE(gameGridCellFromPoint(grid, 9, 9, 10, 20, row, column));
  EXPECT_EQ(row, 0);
  EXPECT_EQ(column, 0);
  EXPECT_TRUE(gameGridCellFromPoint(grid, 9, 9, 99, 199, row, column));
  EXPECT_EQ(row, 8);
  EXPECT_EQ(column, 8);
  EXPECT_FALSE(gameGridCellFromPoint(grid, 9, 9, 100, 200, row, column));
  EXPECT_FALSE(gameGridCellFromPoint(grid, 0, 9, 10, 20, row, column));
  EXPECT_FALSE(gameGridCellFromPoint(Rect{}, 9, 9, 10, 20, row, column));
}

TEST(GameUiTouchGeometry, IntersectionUsesNearestPointAndRejectsOutside) {
  int row = -1;
  int column = -1;
  EXPECT_TRUE(gameIntersectionFromPoint(20, 30, 40, 10, 9, 61, 109, row, column));
  EXPECT_EQ(row, 2);
  EXPECT_EQ(column, 1);
  EXPECT_FALSE(gameIntersectionFromPoint(20, 30, 40, 10, 9, 400, 30, row, column));
  EXPECT_FALSE(gameIntersectionFromPoint(20, 30, 0, 10, 9, 20, 30, row, column));
  EXPECT_FALSE(gameIntersectionFromPoint(20, 30, 40, 0, 9, 20, 30, row, column));
}

TEST(GameUiTouchGeometry, ActionSlotsStayInsidePortraitAndLandscapeScreens) {
  const Rect left = gameTouchActionRect(480, 800, 20, 8, 52, 0, 2);
  const Rect right = gameTouchActionRect(480, 800, 20, 8, 52, 1, 2);
  EXPECT_EQ(left.x, 20);
  EXPECT_EQ(left.y, 728);
  EXPECT_EQ(right.x, left.x + left.width + 8);
  EXPECT_LE(right.x + right.width, 460);
  EXPECT_EQ(gameTouchActionRect(480, 800, 20, 8, 52, 2, 2).width, 0);
  EXPECT_EQ(gameTouchActionRect(40, 800, 20, 8, 52, 0, 2).width, 0);

  const Rect wide = gameTouchActionRect(800, 480, 24, 10, 54, 2, 3);
  EXPECT_EQ(wide.y, 402);
  EXPECT_LT(wide.x, 776);
  EXPECT_LE(wide.x + wide.width, 776);
}

TEST(GameUiTouchGeometry, MenuPanelCentersAndRejectsInvalidGeometry) {
  const Rect portrait = gameMenuPanelRect(480, 800, 320, 28, 32, 7);
  EXPECT_EQ(portrait.x, 80);
  EXPECT_EQ(portrait.y, 272);
  EXPECT_EQ(portrait.width, 320);
  EXPECT_EQ(portrait.height, 256);

  const Rect landscape = gameMenuPanelRect(800, 480, 340, 28, 32, 5);
  EXPECT_EQ(landscape.x, 230);
  EXPECT_EQ(landscape.y, 144);
  EXPECT_EQ(landscape.width, 340);
  EXPECT_EQ(landscape.height, 192);

  EXPECT_EQ(gameMenuPanelRect(480, 800, 0, 28, 32, 7).width, 0);
  EXPECT_EQ(gameMenuPanelRect(480, 100, 320, 28, 32, 7).height, 0);
}
