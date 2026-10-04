#pragma once
#include <cstdint>
#include <vector>
#include <cstddef>

namespace reader_font_size {
constexpr uint8_t namedPoints[] = {12, 14, 16, 18};
constexpr uint8_t namedPointSize(uint8_t index) {
  return namedPoints[index < 4 ? index : 1];
}
constexpr uint8_t nearestNamedSize(uint8_t point) {
  uint8_t best = 0;
  int distance = 256;
  for (uint8_t i = 0; i < 4; ++i) {
    const int delta = int(namedPoints[i]) - point;
    const int candidate = delta < 0 ? -delta : delta;
    if (candidate < distance) { best = i; distance = candidate; }
  }
  return best;
}
// Slot zero inherits the global setting; a positive slot selects a local size.
inline uint8_t overrideSizeSlot(int8_t named, uint8_t point, bool numeric,
                                const std::vector<uint8_t>& available) {
  if (named < 0 && !point) return 0;
  if (!numeric) return 1 + (point ? nearestNamedSize(point) : (named >= 0 && named < 4 ? named : 1));
  if (available.empty()) return 0;
  const uint8_t target = point ? point : namedPointSize(static_cast<uint8_t>(named));
  size_t best = 0;
  int distance = 256;
  for (size_t i = 0; i < available.size() && i < 255; ++i) {
    const int delta = int(available[i]) - target;
    const int candidate = delta < 0 ? -delta : delta;
    if (candidate < distance) { best = i; distance = candidate; }
  }
  return static_cast<uint8_t>(best + 1);
}
struct BookSizeChoice { int8_t named; uint8_t point; };
inline BookSizeChoice choiceForSlot(uint8_t slot, bool numeric, const std::vector<uint8_t>& available) {
  if (!slot || slot > available.size()) return {-1, 0};
  const uint8_t point = available[slot - 1];
  return {static_cast<int8_t>(numeric ? nearestNamedSize(point) : slot - 1),
          static_cast<uint8_t>(numeric ? point : 0)};
}
}  // namespace reader_font_size
