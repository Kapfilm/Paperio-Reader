#pragma once

#include <algorithm>
#include <array>
#include <numeric>

namespace button_hint_layout {
inline constexpr int edge = 4;
inline constexpr int gap = 6;
inline constexpr int maxTextPadding = 4;

inline int captionPadding(int screenWidth, const std::array<int, 4>& textWidths) {
  const int available = screenWidth - 2 * edge - 3 * gap;
  const int textWidth = std::accumulate(textWidths.begin(), textWidths.end(), 0);
  return std::clamp((available - textWidth) / 8, 0, maxTextPadding);
}

inline std::array<int, 4> capsuleWidths(int screenWidth, int capsuleHeight, const std::array<int, 4>& textWidths,
                                        bool equalWidths = false) {
  const int available = screenWidth - 2 * edge - 3 * gap;
  if (equalWidths) {
    const int width = available / 4;
    return {width, width, width, width};
  }
  const int textPadding = captionPadding(screenWidth, textWidths);
  std::array<int, 4> widths{};
  std::array<int, 4> minimums{};
  for (int i = 0; i < 4; ++i) {
    widths[i] = (i + 1) * available / 4 - i * available / 4;
    minimums[i] = std::max(capsuleHeight, textWidths[i] + 2 * textPadding);
  }

  for (int i = 0; i < 4; ++i) {
    for (int donor = 0; donor < 4 && widths[i] < minimums[i]; ++donor) {
      const int spare = std::max(0, widths[donor] - minimums[donor]);
      const int pixels = std::min(spare, minimums[i] - widths[i]);
      widths[i] += pixels;
      widths[donor] -= pixels;
    }
  }
  return widths;
}
}  // namespace button_hint_layout
