#pragma once

#include "components/themes/lyra/LyraTheme.h"

namespace MinimalMetrics {
constexpr ThemeMetrics makeValues() {
  ThemeMetrics values = LyraMetrics::values;
  values.homeTopPadding = 50;
  values.homeCoverHeight = 583;
  values.homeCoverTileHeight = 690;
  values.homeRecentBooksCount = 1;
  values.homeContinueReadingInMenu = false;
  values.homeMenuTopOffset = 0;
  return values;
}

constexpr ThemeMetrics values = makeValues();
}  // namespace MinimalMetrics

class MinimalTheme final : public LyraTheme {
 public:
  void drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                  const char* subtitle = nullptr) const override;
  void drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                  bool selected) const override;
  void drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                       const char* btn4) const override;
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};
