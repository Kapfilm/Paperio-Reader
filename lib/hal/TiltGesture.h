#pragma once
#include <cmath>
#include <cstdint>

// Axis choices follow InkMod 51a381bd's X3 QMI8658 orientation mapping.
namespace tilt_gesture {
inline float axis(float gx, float gy, uint8_t orientation, uint8_t direction) {
  const bool foreAft = direction == 2 || direction == 3;
  float value;
  switch (orientation) {
    case 1:
      value = foreAft ? gx : -gy;
      break;
    case 2:
      value = foreAft ? -gy : -gx;
      break;
    case 3:
      value = foreAft ? -gx : gy;
      break;
    default:
      value = foreAft ? gy : gx;
      break;
  }
  return direction == 1 || direction == 3 ? -value : value;
}

class Detector {
  bool active_ = false;
  uint32_t last_ = 0;

 public:
  void reset(uint32_t now) {
    active_ = false;
    last_ = now;
  }
  int update(float rate, uint32_t now) {
    if (!std::isfinite(rate)) return 0;
    if (active_) {
      if (std::fabs(rate) < 50.0f) active_ = false;
      return 0;
    }
    if (static_cast<uint32_t>(now - last_) < 600) return 0;
    const int event = rate > 270.0f ? 1 : rate < -270.0f ? -1 : 0;
    if (event) {
      active_ = true;
      last_ = now;
    }
    return event;
  }
};
}  // namespace tilt_gesture
