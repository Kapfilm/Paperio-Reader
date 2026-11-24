#include <gtest/gtest.h>
#include "util/StatusBarLayout.h"

TEST(StatusBarLayout, InheritedPositionPreservesLegacySettings) {
  EXPECT_TRUE(status_bar::itemAtTop(0, 0));
  EXPECT_FALSE(status_bar::itemAtTop(0, 1));
}
TEST(StatusBarLayout, ExplicitEdgesOverrideEitherGlobalPosition) {
  for (uint8_t global : {0, 1}) {
    EXPECT_TRUE(status_bar::itemAtTop(1, global));
    EXPECT_FALSE(status_bar::itemAtTop(2, global));
  }
}
TEST(StatusBarLayout, ClockAndBatteryCanReserveDifferentEdges) {
  const std::array<status_bar::Item, 2> items{{{true, 1}, {true, 2}}};
  EXPECT_TRUE(status_bar::hasItemsAtEdge(items, true, 1));
  EXPECT_TRUE(status_bar::hasItemsAtEdge(items, false, 0));
}
TEST(StatusBarLayout, HiddenElementsDoNotReserveEmptyRows) {
  const std::array<status_bar::Item, 2> items{{{false, 1}, {true, 2}}};
  EXPECT_FALSE(status_bar::hasItemsAtEdge(items, true, 0));
  EXPECT_TRUE(status_bar::hasItemsAtEdge(items, false, 0));
}
TEST(StatusBarLayout, InheritAndExplicitPlacementCoexist) {
  const std::array<status_bar::Item, 2> items{{{true, 0}, {true, 1}}};
  EXPECT_TRUE(status_bar::hasItemsAtEdge(items, true, 1));
  EXPECT_TRUE(status_bar::hasItemsAtEdge(items, false, 1));
  EXPECT_FALSE(status_bar::hasItemsAtEdge(items, false, 0));
}
TEST(StatusBarLayout, InvalidOverrideFallsBackToGlobalSetting) {
  EXPECT_TRUE(status_bar::itemAtTop(255, 0));
  EXPECT_FALSE(status_bar::itemAtTop(255, 1));
}

TEST(StatusBarLayout, ExplicitCornersSelectEdgesAndSidesIndependently) {
  EXPECT_TRUE(status_bar::itemAtTop(3, 1));
  EXPECT_TRUE(status_bar::itemAtTop(4, 1));
  EXPECT_FALSE(status_bar::itemAtTop(5, 0));
  EXPECT_FALSE(status_bar::itemAtTop(6, 0));
  EXPECT_TRUE(status_bar::itemAtLeft(3, false));
  EXPECT_FALSE(status_bar::itemAtLeft(4, true));
  EXPECT_TRUE(status_bar::itemAtLeft(5, false));
  EXPECT_FALSE(status_bar::itemAtLeft(6, true));
}
TEST(StatusBarLayout, LegacyPlacementPreservesDefaultSide) {
  for (uint8_t position : {0, 1, 2, 255}) {
    EXPECT_TRUE(status_bar::itemAtLeft(position, true));
    EXPECT_FALSE(status_bar::itemAtLeft(position, false));
  }
}
