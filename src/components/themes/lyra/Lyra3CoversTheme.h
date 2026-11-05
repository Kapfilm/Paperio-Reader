#pragma once

#include <array>

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

// Lyra theme metrics (zero runtime cost)
namespace Lyra3CoversMetrics {
constexpr int coverInset = 8;
constexpr int coverHeightForTile(const int height) { return height > 2 * coverInset ? height - 2 * coverInset : 1; }
constexpr ThemeMetrics values = {.batteryWidth = 16,
                                 .batteryHeight = 12,
                                 .topPadding = 5,
                                 .batteryBarHeight = 40,
                                 .headerHeight = 84,
                                 .verticalSpacing = 16,
                                 .contentSidePadding = 20,
                                 .listRowHeight = 40,
                                 .listWithSubtitleRowHeight = 60,
                                 .menuRowHeight = 44,
                                 .menuSpacing = 2,
                                 .tabSpacing = 8,
                                 .tabBarHeight = 40,
                                 .scrollBarWidth = 4,
                                 .scrollBarRightOffset = 5,
                                 .homeTopPadding = 40,
                                 .homeCoverHeight = 244,
                                 .homeCoverTileHeight = 260,
                                 .homeRecentBooksCount = 3,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 5,
                                 .statusBarVerticalMargin = 19,
                                 .keyboardKeyWidth = 31,
                                 .keyboardKeyHeight = 40,
                                 .keyboardKeySpacing = 0,
                                 .keyboardBottomKeyHeight = 35,
                                 .keyboardBottomKeySpacing = 5,
                                 .keyboardBottomAligned = true,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -7,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 90};
}  // namespace Lyra3CoversMetrics

class Lyra3CoversTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           const int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;

 private:
  // Keep the actual aspect-fitted artwork bounds with the unselected framebuffer
  // cache. Selection is drawn after that cache is stored, so changing focus never
  // leaves the previous black frame behind.
  mutable std::array<Rect, 3> coverRects_{{Rect{}, Rect{}, Rect{}}};
  mutable Rect coverArea_{};
  mutable int cachedBookCount_ = 0;
};
