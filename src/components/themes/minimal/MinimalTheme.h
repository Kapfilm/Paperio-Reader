#pragma once

#include "components/themes/lyra/LyraTheme.h"

namespace MinimalMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics metrics = LyraMetrics::values;
  metrics.homeTopPadding = 50;
  metrics.homeCoverHeight = 640;
  metrics.homeCoverTileHeight = 710;
  metrics.homeRecentBooksCount = 1;
  metrics.homeContinueReadingInMenu = false;
  metrics.homeMenuTopOffset = 0;
  return metrics;
}

constexpr ThemeMetrics values = makeValues();
}  // namespace MinimalMetrics

class MinimalTheme final : public LyraTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
  void drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                const std::function<std::string(int index)>& rowTitle,
                const std::function<std::string(int index)>& rowSubtitle,
                const std::function<UIIcon(int index)>& rowIcon, const std::function<std::string(int index)>& rowValue,
                bool highlightValue) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
};
