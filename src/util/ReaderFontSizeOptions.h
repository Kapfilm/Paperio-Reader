#pragma once
#include <cstdint>

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
}  // namespace reader_font_size
