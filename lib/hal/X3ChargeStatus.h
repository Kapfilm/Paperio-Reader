#pragma once

#include <cstdint>

// Current is evidence of charge flowing into the cell, not a VBUS detector.
// BQ27220 FC may remain set after unplugging and must not be used here.
class X3ChargeStatus {
 public:
  bool pollDue(uint32_t now) const { return !attempted || now - lastAttempt >= 1000; }
  void record(bool success, int16_t currentMa, uint32_t now) {
    attempted = true;
    lastAttempt = now;
    if (success) {
      known = true;
      lastSuccess = now;
      positiveCurrent = currentMa > 5;
    }
  }
  bool chargingKnown(uint32_t now) const { return known && now - lastSuccess < 3000; }
  bool charging(uint32_t now) const { return chargingKnown(now) && positiveCurrent; }

 private:
  bool attempted = false;
  bool known = false;
  bool positiveCurrent = false;
  uint32_t lastAttempt = 0;
  uint32_t lastSuccess = 0;
};
