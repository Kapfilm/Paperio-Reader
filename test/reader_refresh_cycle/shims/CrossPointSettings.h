#pragma once
#include <cstdint>
struct CrossPointSettings {
  enum ORIENTATION { PORTRAIT, LANDSCAPE_CW, INVERTED, LANDSCAPE_CCW };
  enum TILT_GESTURE_ACTION { TILT_ACT_NEXT_PAGE, TILT_ACT_PREV_PAGE };
  int frequency = 15;
  bool tiltPageTurn = false;
  uint8_t tiltPositiveAction = 0, tiltNegativeAction = 0;
  int getRefreshFrequency() const { return frequency; }
};
inline CrossPointSettings SETTINGS;
