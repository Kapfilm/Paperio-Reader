#pragma once

#include <cstdint>

// Experimental display policy, not a measured SOC or an ADC calibration.
// Two full-charge trials settled near 4068 mV (about 92% on the SDK curve).
// A bounded +4 point adjustment is a conservative presentation choice for a
// further device test. It does not claim the remaining capacity is exactly 96%.
// Keep the base estimator and its persisted checkpoints in their original scale.
class X4BatteryDisplayScale {
 public:
  static constexpr uint16_t tenths(uint16_t base) {
    if (base <= 800) return base;
    if (base <= 920) return static_cast<uint16_t>(800 + (base - 800) * 160 / 120);
    if (base < 1000) return static_cast<uint16_t>(960 + (base - 920) * 40 / 80);
    return 1000;
  }

  static constexpr uint16_t percent(uint16_t baseTenths) { return tenths(baseTenths) / 10; }
};
