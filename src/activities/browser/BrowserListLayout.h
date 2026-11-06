#pragma once

#include <GfxRenderer.h>

#include "fontIds.h"

namespace BrowserList {
inline constexpr int leftPadding = 16;
inline constexpr int rightPadding = 24;
inline constexpr int rowPadding = 6;
inline constexpr int folderTextOffset = 30;

inline int rowHeight(const GfxRenderer& renderer) { return renderer.getLineHeight(UI_10_FONT_ID) + 2 * rowPadding; }

// The outline follows the row's ink color, with no opaque icon background.
inline void drawFolderIcon(const GfxRenderer& renderer, int x, int y, int height, bool ink) {
  y += (height - 16) / 2;
  renderer.drawLine(x, y + 4, x, y + 15, ink);
  renderer.drawLine(x, y + 15, x + 20, y + 15, ink);
  renderer.drawLine(x + 20, y + 15, x + 20, y + 4, ink);
  renderer.drawLine(x, y + 4, x + 20, y + 4, ink);
  renderer.drawLine(x, y + 4, x, y, ink);
  renderer.drawLine(x, y, x + 8, y, ink);
  renderer.drawLine(x + 8, y, x + 11, y + 4, ink);
}
}  // namespace BrowserList
