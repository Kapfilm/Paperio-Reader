#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <vector>

#include "EpdFont.h"
#include "builtinFonts/inter_ui_10_regular.h"
#include "builtinFonts/inter_ui_12_bold.h"
#include "builtinFonts/inter_ui_12_regular.h"
#include "util/TabBarLayout.h"

namespace {
constexpr std::array kRussianLabels = {"Экран", "Чтение", "Управление", "Система"};

std::vector<int> widths(bool classic, int selected) {
  std::vector<int> result;
  for (int i = 0; i < static_cast<int>(kRussianLabels.size()); ++i) {
    const auto* data = classic ? (i == selected ? &inter_ui_12_bold : &inter_ui_12_regular) : &inter_ui_10_regular;
    EpdFont font(data);
    int width, height;
    font.getTextDimensions(kRussianLabels[i], &width, &height);
    result.push_back(width + (classic ? 6 : 16));
  }
  return result;
}

void expectNaturalFit(int contentWidth, const std::vector<int>& natural) {
  constexpr int origin = 3;
  constexpr int margin = 20;
  int previousEnd = origin;
  std::vector<int> growth;
  for (int i = 0; i < static_cast<int>(natural.size()); ++i) {
    const auto cell = TabBarLayout::cell(natural, i, origin, contentWidth, margin);
    EXPECT_GE(cell.width, natural[i]);
    EXPECT_GE(cell.x, previousEnd);
    EXPECT_LE(cell.x + cell.width, origin + contentWidth);
    previousEnd = cell.x + cell.width;
    growth.push_back(cell.width - natural[i]);
  }
  const auto first = TabBarLayout::cell(natural, 0, origin, contentWidth, margin);
  EXPECT_EQ(first.x - origin, origin + contentWidth - previousEnd);
  EXPECT_LE(*std::max_element(growth.begin(), growth.end()) - *std::min_element(growth.begin(), growth.end()), 1);
}
}  // namespace

TEST(TabBarLayout, X3MiniFitsFullRussianLabelsWithSharedRemainingSpace) {
  const auto natural = widths(false, 0);
  EXPECT_EQ(natural, (std::vector<int>{78, 87, 136, 102}));
  expectNaturalFit(528 - 6, natural);
}

TEST(TabBarLayout, X4MiniFitsControlLabelWithoutEqualQuarterClipping) {
  const auto natural = widths(false, 0);
  EXPECT_GT(natural[2], (480 - 6 - 40) / 4);
  expectNaturalFit(480 - 6, natural);
}

TEST(TabBarLayout, ClassicFitsEveryBoldSelectionOnX4AndX3) {
  for (int selected = 0; selected < 4; ++selected) {
    const auto natural = widths(true, selected);
    expectNaturalFit(480 - 6, natural);
    expectNaturalFit(528 - 6, natural);
  }
}

TEST(TabBarLayout, ClassicReducesOuterMarginsBeforeClippingLongSelectedLabel) {
  const auto natural = widths(true, 2);
  const auto first = TabBarLayout::cell(natural, 0, 3, 474, 20);
  EXPECT_LT(first.x - 3, 20);
  expectNaturalFit(474, natural);
}

TEST(TabBarLayout, NarrowViewportKeepsCellsInsideAndAdjacent) {
  const auto natural = widths(false, 0);
  int end = 17;
  for (int i = 0; i < 4; ++i) {
    const auto cell = TabBarLayout::cell(natural, i, 17, 101, 20);
    EXPECT_EQ(cell.x, end);
    EXPECT_GE(cell.width, 0);
    end = cell.x + cell.width;
  }
  EXPECT_EQ(end, 118);
}

TEST(TabBarLayout, EmptyOrZeroViewportIsSafe) {
  EXPECT_EQ(TabBarLayout::cell({}, 0, 3, 100, 20).width, 0);
  EXPECT_EQ(TabBarLayout::cell({10}, -1, 3, 100, 20).width, 0);
  EXPECT_EQ(TabBarLayout::cell({10}, 1, 3, 100, 20).width, 0);
  const auto cell = TabBarLayout::cell({10}, 0, 3, -1, 20);
  EXPECT_EQ(cell.x, 3);
  EXPECT_EQ(cell.width, 0);
}

TEST(TabBarLayout, EmptyLabelsShareAllPixelsIncludingRemainder) {
  const std::vector<int> natural{0, 0, 0};
  const auto first = TabBarLayout::cell(natural, 0, 0, 101, 0);
  const auto second = TabBarLayout::cell(natural, 1, 0, 101, 0);
  const auto last = TabBarLayout::cell(natural, 2, 0, 101, 0);
  EXPECT_EQ(first.x, 0);
  EXPECT_EQ(second.x, first.width);
  EXPECT_EQ(last.x, second.x + second.width);
  EXPECT_EQ(last.x + last.width, 101);
  EXPECT_LE(last.width - first.width, 1);
}
