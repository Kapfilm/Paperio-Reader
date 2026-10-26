#pragma once

#include <cstddef>
#include <cstdint>

// Optional, loop-owned diagnostics. Create /battery-calibration.enable on the
// SD card before boot to append samples to /battery-calibration.csv.
// Disabled by default; never changes sleep policy or removes an existing log.
class BatteryCalibrationLog {
 public:
  // Call once after Storage.begin(). False means disabled or unavailable.
  bool begin();
  void record(uint32_t now, uint16_t millivolts, uint16_t rawPercent, uint16_t estimatePercent, bool usb);
  bool enabled() const { return active; }

 private:
  bool append(const char* text, size_t length);
  void disable(const char* reason);

  bool active = false;
  bool haveRecord = false;
  bool previousUsb = false;
  uint32_t lastRecordAt = 0;
};
