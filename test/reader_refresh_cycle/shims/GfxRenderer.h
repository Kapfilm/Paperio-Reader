#pragma once
#include <vector>
struct HalDisplay {
  enum RefreshMode { FAST_REFRESH, HALF_REFRESH };
};
struct GfxRenderer {
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  bool x3 = true;
  bool dark = true;
  mutable std::vector<HalDisplay::RefreshMode> frames;
  bool isX3() const { return x3; }
  bool isDarkMode() const { return dark; }
  void setOrientation(Orientation) {}
  void displayBuffer(HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH) const { frames.push_back(mode); }
  void triggerDisplay(HalDisplay::RefreshMode mode) const { frames.push_back(mode); }
  void setNextDisplayRefreshMode(HalDisplay::RefreshMode) const {}
};
