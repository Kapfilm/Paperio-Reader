#pragma once
// Minimal host-test stub for HalDisplay.h. JpegToBmpConverter.cpp includes it but
// only the no-size jpegFileToBmpStream() entry point reads display dimensions; the
// WithSize variants exercised by these tests pass explicit targets.
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

// ESP (heap-gate accessor) comes from the shims' Arduino.h, which the shared
// zip_entry_reader/HalStorage.h now includes — defining it here again would be
// a redefinition.
#include <Arduino.h>
