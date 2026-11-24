#pragma once

#include <array>
#include <cstdint>
#include <cstddef>

namespace status_bar {
// 0 inherits, 1/2 legacy top/bottom, 3/4 top-left/right, 5/6 bottom-left/right.
constexpr bool itemAtTop(uint8_t position, uint8_t defaultPosition) {
  if (position == 1 || position == 3 || position == 4) return true;
  if (position == 2 || position == 5 || position == 6) return false;
  return defaultPosition == 0;
}
constexpr bool itemAtLeft(uint8_t position, bool defaultLeft) {
  if (position == 3 || position == 5) return true;
  if (position == 4 || position == 6) return false;
  return defaultLeft;
}
struct Item {
  bool visible;
  uint8_t position;
};
template <size_t N>
constexpr bool hasItemsAtEdge(const std::array<Item, N>& items, bool top, uint8_t defaultPosition) {
  for (const auto& item : items) {
    if (item.visible && itemAtTop(item.position, defaultPosition) == top) return true;
  }
  return false;
}
}  // namespace status_bar
