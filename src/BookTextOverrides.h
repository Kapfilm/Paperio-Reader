#pragma once

#include <cstdint>

struct BookTextOverrides {
  int8_t sideMargin = -1;
  int8_t topMargin = -1;
  int8_t bottomMargin = -1;
  int8_t paragraphSpacing = -1;
  int8_t numericSizes = -1;
  int8_t darkness = -1;
  int8_t fastAa = -1;

  static int value(int overrideValue, int defaultValue) {
    return overrideValue < 0 ? defaultValue : overrideValue;
  }

  static int verticalPadding(int margin, int side, int reserved) {
    return margin > 0 ? reserved + margin : (side > reserved ? side : reserved);
  }

  bool operator==(const BookTextOverrides&) const = default;
};
