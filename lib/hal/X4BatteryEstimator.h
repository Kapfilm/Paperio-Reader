#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>

// Voltage-based estimate, not a fuel gauge. The stock X4 does not expose a
// proven charge-complete signal. 100% is the endpoint of the rate-limited
// voltage estimate; it is not certification that the charger LED is green.
// Keep policy independent of Arduino so sleep/boot and load transients can be
// exercised with a deterministic clock on the host.
class X4BatteryEstimator {
 public:
  void restore(int percent) { restored = percent >= 0 && percent <= 100 ? percent : -1; }

  bool update(uint32_t now, uint16_t millivolts, uint16_t voltagePercent, bool usb, bool chargeComplete = false) {
    // Reject disconnected/invalid ADC readings. Sustained low but plausible
    // cell voltage is handled separately below, without the normal rate limit.
    if (millivolts < 2000 || millivolts > 4500 || voltagePercent > 100) {
      low = false;
      return false;
    }
    if (seeded && now - lastAt > 10000) low = false;
    // The SDK cubic turns upward again below its useful LiPo range (e.g.
    // 2.5 V can map to 100%). Never allow that mathematical tail to certify
    // a deeply discharged cell as full, including the very first boot sample.
    const int sample = millivolts <= 3200 ? 0 : static_cast<int>(voltagePercent) * 10;
    if (!seeded) {
      // While powered off the battery may have charged or discharged. With
      // no elapsed-time/current history, a saved number cannot veto a fresh
      // unplugged measurement forever. On USB it still prevents a new high
      // terminal-voltage sample from erasing the last useful estimate.
      tenths = usb && restored >= 0 ? std::min(restored * 10, sample) : sample;
      // Neither charge-raised startup voltage nor a persisted 100% is proof
      // of full charge. Seed below full, then let the existing slow upper
      // charge limiter reach the estimated endpoint during continued polling.
      if (usb && !chargeComplete) tenths = std::min(tenths, 990);
      filteredTenths = sample;
      seeded = true;
      previousUsb = usb;
      unpluggedAt = now;
      lastAt = now;
    }
    uint32_t elapsed = std::min<uint32_t>(now - lastAt, 60000);
    lastAt = now;
    if (usb != previousUsb) {
      previousUsb = usb;
      unpluggedAt = now;
      budget = 0;
      elapsed = 0;
      filteredTenths = sample;
    }
    // Carry sub-percent precision and round symmetrically: a constant input
    // must converge rather than leave the integer IIR permanently below it.
    filteredTenths += (sample - filteredTenths + (sample >= filteredTenths ? 3 : -3)) / 4;

    if (millivolts <= 3500) {
      if (!low) lowAt = now;
      low = true;
    } else {
      low = false;
    }
    if (low && now - lowAt >= 20000) {
      tenths = std::min(tenths, sample);
      budget = 0;
      return true;
    }
    if (usb && chargeComplete) {
      tenths = 1000;
      budget = 0;
      return true;
    }

    int target = filteredTenths;
    if (usb) {
      // Keep the same CC/CV rate limiter through the final 99 -> 100 step.
      // Reaching 100 does not create a hold or a persistent full-cycle flag.
      target = std::max(tenths, target);
    } else {
      // Relaxation after load removal cannot add charge during an awake
      // session. A reboot takes a new voltage estimate (see seeding above).
      target = std::min(tenths, target);
      if (now - unpluggedAt < 120000) target = tenths;
    }
    if (target == tenths) {
      budget = 0;
      return true;
    }
    // No hour-long hold. Ordinary discharge catches up at 1%/min, with a
    // faster correction for an obviously stale estimate (15+ percentage pts).
    const uint32_t stepMs = usb ? (tenths < 950 ? 4000 : 12000) : (tenths - target >= 150 ? 1500 : 6000);
    budget = std::min<uint32_t>(budget + elapsed, 60000);
    const int step = std::min<int>(std::abs(target - tenths), budget / stepMs);
    tenths += target > tenths ? step : -step;
    budget -= step * stepMs;
    return true;
  }

  bool known() const { return seeded; }
  // This is the base scale. Presentation corrections must never be restored
  // as estimator input or applied a second time after a sleep checkpoint.
  uint16_t percentageTenths() const { return static_cast<uint16_t>(std::clamp(tenths, 0, 1000)); }
  uint16_t percentage() const { return static_cast<uint16_t>(std::clamp(tenths / 10, 0, 100)); }

 private:
  int restored = -1;
  int tenths = 0;
  int filteredTenths = 0;
  bool seeded = false;
  bool previousUsb = false;
  bool low = false;
  uint32_t lowAt = 0;
  uint32_t unpluggedAt = 0;
  uint32_t lastAt = 0;
  uint32_t budget = 0;
};
