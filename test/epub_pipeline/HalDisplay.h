#pragma once
// Minimal host-test stub for HalDisplay.h (JpegToBmpConverter.cpp reads display
// dimensions from it). ESP/EspClass comes from the Arduino shim.
#include <Arduino.h>

#include <cstdint>

class HalDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH = 480;
  static constexpr uint16_t DISPLAY_HEIGHT = 800;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = 60;

  uint16_t getDisplayWidth() const { return DISPLAY_WIDTH; }
  uint16_t getDisplayHeight() const { return DISPLAY_HEIGHT; }
};

// Singleton used as `display.getDisplay...()` in the converter.
inline HalDisplay display;
