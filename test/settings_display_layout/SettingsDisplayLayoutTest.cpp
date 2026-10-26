#include <gtest/gtest.h>

#include <array>

#include "builtinFonts/inter_ui_10_regular.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/minimal/MinimalTheme.h"
#include "util/SettingsDisplayLayout.h"

namespace {
// After removing the interface-font selector, the X4 Display category has
// 15 rows including the three section dividers and the Refresh submenu row.
constexpr int kDisplayRowsX4 = 15;
constexpr int kDisplayRowsX3 = 14;  // Dark mode is hidden on X3.

int heightFor(int screenHeight, const ThemeMetrics& metrics, bool bottomHints) {
  return SettingsDisplayLayout::listHeight(screenHeight - (bottomHints ? metrics.buttonHintsHeight : 0),
                                           metrics.topPadding, metrics.headerHeight, metrics.tabBarHeight,
                                           SettingsDisplayLayout::sectionGap(metrics.verticalSpacing));
}
int rowFor(int height, int count, const ThemeMetrics& metrics) {
  return SettingsDisplayLayout::rowHeight(height, count, metrics.listRowHeight, inter_ui_10_regular.advanceY,
                                          BaseTheme::toggleSwitchHeight);
}
}  // namespace

TEST(SettingsDisplayLayout, X4PortraitFitsEveryDisplayItemWithoutChangingFont) {
  const auto& metrics = MinimalMetrics::values;
  const int height = heightFor(800, metrics, true);
  const int row = rowFor(height, kDisplayRowsX4, metrics);
  EXPECT_EQ(inter_ui_10_regular.advanceY, 25);
  EXPECT_EQ(height, 615);
  EXPECT_EQ(row, 36);
  EXPECT_GE(height / row, kDisplayRowsX4);
  EXPECT_GE(row, 7 + inter_ui_10_regular.ascender - inter_ui_10_regular.descender);
  EXPECT_GE(row, BaseTheme::toggleSwitchHeight + 8);
}

TEST(SettingsDisplayLayout, X3PortraitAlsoFitsItsVisibleDisplayItems) {
  const auto& metrics = MinimalMetrics::values;
  const int height = heightFor(792, metrics, true);
  const int row = rowFor(height, kDisplayRowsX3, metrics);
  EXPECT_EQ(height, 607);
  EXPECT_GE(height / row, kDisplayRowsX3);
}

TEST(SettingsDisplayLayout, EveryBundledThemeFitsPortraitDisplayPage) {
  const std::array metrics = {BaseMetrics::values, LyraMetrics::values, Lyra3CoversMetrics::values,
                              LyraCarouselMetrics::values, MinimalMetrics::values};
  for (const auto& theme : metrics) {
    const int height = heightFor(800, theme, true);
    const int row = rowFor(height, kDisplayRowsX4, theme);
    EXPECT_GT(row, 0);
    EXPECT_GE(height / row, kDisplayRowsX4);
    EXPECT_LE(row, theme.listRowHeight);
  }
}

TEST(SettingsDisplayLayout, LandscapePreservesReadableRowsAndAllowsPagination) {
  const auto& metrics = MinimalMetrics::values;
  const int height = heightFor(480, metrics, false);
  const int row = rowFor(height, kDisplayRowsX4, metrics);
  EXPECT_EQ(row, 34);
  EXPECT_GE(row, 7 + inter_ui_10_regular.ascender - inter_ui_10_regular.descender);
  EXPECT_GT(height / row, 0);
  EXPECT_LT(height / row, kDisplayRowsX4);
}

TEST(SettingsDisplayLayout, ClassicKeepsItsAlreadySmallerRows) {
  const auto& metrics = BaseMetrics::values;
  EXPECT_EQ(rowFor(heightFor(800, metrics, true), kDisplayRowsX4, metrics), metrics.listRowHeight);
}

TEST(SettingsDisplayLayout, EmptyAndTinyAreasRemainSafe) {
  EXPECT_EQ(SettingsDisplayLayout::listHeight(50, 5, 84, 40, 8), 0);
  EXPECT_GT(SettingsDisplayLayout::rowHeight(0, 15, 40, 25, 24), 0);
  EXPECT_EQ(SettingsDisplayLayout::rowHeight(600, 0, 40, 25, 24), 40);
  EXPECT_EQ(SettingsDisplayLayout::rowHeight(600, 0, 0, 25, 24), 1);
}
