#pragma once

#include <algorithm>
#include <vector>

namespace TabBarLayout {
struct Cell {
  int x;
  int width;
};

// Give each label its natural width, then share remaining space equally.
// Shrink outer margins before clipping long labels on a narrow viewport.
inline Cell cell(const std::vector<int>& widths, int index, int x, int width, int preferredMargin) {
  if (index < 0 || index >= static_cast<int>(widths.size())) return {x, 0};
  width = std::max(0, width);
  int total = 0;
  int before = 0;
  for (int i = 0; i < static_cast<int>(widths.size()); ++i) {
    const int natural = std::max(0, widths[i]);
    total += natural;
    if (i < index) before += natural;
  }
  const int margin = std::min(std::max(0, preferredMargin), std::max(0, (width - total) / 2));
  const int available = width - 2 * margin;
  const int count = static_cast<int>(widths.size());
  int start, end;
  if (total <= available) {
    const int extra = available - total;
    start = before + extra * index / count;
    end = before + std::max(0, widths[index]) + extra * (index + 1) / count;
  } else {
    start = available * before / total;
    end = available * (before + std::max(0, widths[index])) / total;
  }
  return {x + margin + start, end - start};
}
}  // namespace TabBarLayout
