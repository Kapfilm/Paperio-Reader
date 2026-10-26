#pragma once

#include <algorithm>

// Compact settings categories and reader-menu tabs; retain the normal font and a safe
// minimum row size. Short/landscape screens keep pagination rather than clip.
namespace SettingsDisplayLayout {
constexpr int sectionGap(int normalGap) { return std::min(normalGap, 8); }

constexpr int listHeight(int contentHeight, int topPadding, int headerHeight, int tabHeight, int gap) {
  return std::max(0, contentHeight - topPadding - headerHeight - tabHeight - 2 * gap);
}

constexpr int rowHeight(int availableHeight, int rows, int normalRowHeight, int fontLineHeight, int toggleHeight) {
  if (rows <= 0 || normalRowHeight <= 0) return std::max(1, normalRowHeight);
  // Lyra/Mini place text at y+7; leave two pixels below its full line box.
  // Classic's existing smaller row is already sized for its y+0 baseline.
  const int minimum = std::min(normalRowHeight, std::max(fontLineHeight + 9, toggleHeight + 8));
  const int preferred = std::min(normalRowHeight, 36);
  return std::max(minimum, std::min(preferred, availableHeight / rows));
}
}  // namespace SettingsDisplayLayout
