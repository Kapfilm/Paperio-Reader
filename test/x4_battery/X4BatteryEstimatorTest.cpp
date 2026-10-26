#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "lib/hal/X4BatteryEstimator.h"

namespace {
constexpr uint32_t kPollMs = 1500;

void runFor(X4BatteryEstimator& estimator, uint32_t& now, uint32_t duration, uint16_t mv, uint16_t voltagePercent,
            bool usb, bool full = false) {
  for (uint32_t elapsed = 0; elapsed < duration; elapsed += kPollMs) {
    now += kPollMs;
    ASSERT_TRUE(estimator.update(now, mv, voltagePercent, usb, full));
  }
}
}  // namespace

TEST(X4BatteryEstimator, FirstBootOnUsbSeedsBelowFullThenEstimatesEndpointGradually) {
  X4BatteryEstimator estimator;
  EXPECT_FALSE(estimator.known());
  ASSERT_TRUE(estimator.update(0, 4200, 100, true));
  EXPECT_TRUE(estimator.known());
  EXPECT_EQ(estimator.percentage(), 99);
  uint32_t now = 0;
  runFor(estimator, now, 60000, 4200, 100, true);
  EXPECT_EQ(estimator.percentage(), 99);
  runFor(estimator, now, 60000, 4200, 100, true);
  EXPECT_EQ(estimator.percentage(), 100);
}

TEST(X4BatteryEstimator, RestoredFullOnUsbAlsoRemainsProvisional) {
  X4BatteryEstimator estimator;
  estimator.restore(100);
  ASSERT_TRUE(estimator.update(0, 4200, 100, true));
  EXPECT_EQ(estimator.percentage(), 99);
}

TEST(X4BatteryEstimator, EstimatedFullDoesNotIntroduceAnHourHoldAfterUnplug) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4200, 100, true));
  uint32_t now = 0;
  runFor(estimator, now, 120000, 4200, 100, true);
  ASSERT_EQ(estimator.percentage(), 100);
  now += kPollMs;
  ASSERT_TRUE(estimator.update(now, 4000, 89, false));
  runFor(estimator, now, 117000, 4000, 89, false);
  EXPECT_EQ(estimator.percentage(), 100);
  runFor(estimator, now, 15000, 4000, 89, false);
  EXPECT_LT(estimator.percentage(), 100);
}

TEST(X4BatteryEstimator, UsbPresenceAloneDoesNotForceFullBelowVoltageTarget) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4130, 99, true));
  uint32_t now = 0;
  runFor(estimator, now, 2 * 60 * 60 * 1000, 4130, 99, true);
  EXPECT_EQ(estimator.percentage(), 99);
}

TEST(X4BatteryEstimator, OneHighSampleCannotJumpFrom99To100) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4130, 99, true));
  ASSERT_TRUE(estimator.update(kPollMs, 4226, 100, true));
  EXPECT_EQ(estimator.percentage(), 99);
  uint32_t now = kPollMs;
  runFor(estimator, now, 60000, 4130, 99, true);
  EXPECT_EQ(estimator.percentage(), 99);
}

TEST(X4BatteryEstimator, ChargingAdvancesGraduallyThroughFinalEstimated100) {
  X4BatteryEstimator estimator;
  estimator.restore(90);
  ASSERT_TRUE(estimator.update(0, 4200, 100, true));
  EXPECT_EQ(estimator.percentage(), 90);
  uint32_t now = 0;
  runFor(estimator, now, 30000, 4200, 100, true);
  EXPECT_EQ(estimator.percentage(), 90);
  runFor(estimator, now, 60 * 60 * 1000, 4200, 100, true);
  EXPECT_EQ(estimator.percentage(), 100);
}

TEST(X4BatteryEstimator, ExplicitChargeCompleteCanSetFullWhileUsbIsPresent) {
  X4BatteryEstimator estimator;
  estimator.restore(90);
  ASSERT_TRUE(estimator.update(0, 4150, 98, true));
  ASSERT_TRUE(estimator.update(1500, 4150, 98, true, true));
  EXPECT_EQ(estimator.percentage(), 100);
  ASSERT_TRUE(estimator.update(3000, 4150, 98, true, true));
  EXPECT_EQ(estimator.percentage(), 100);
}

TEST(X4BatteryEstimator, ChargeCompleteWithoutUsbCannotIncreaseEstimate) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 3900, 80, false));
  uint32_t now = 0;
  runFor(estimator, now, 10 * 60 * 1000, 4150, 98, false, true);
  EXPECT_EQ(estimator.percentage(), 80);
}

TEST(X4BatteryEstimator, UnplugRetainsShortSettlingPeriodButNoHourHold) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4200, 100, true, true));
  ASSERT_TRUE(estimator.update(1500, 4000, 89, false));
  uint32_t now = 1500;
  runFor(estimator, now, 117000, 4000, 89, false);
  EXPECT_EQ(estimator.percentage(), 100);
  runFor(estimator, now, 15000, 4000, 89, false);
  EXPECT_LT(estimator.percentage(), 100);
  runFor(estimator, now, 20 * 60 * 1000, 4000, 89, false);
  EXPECT_EQ(estimator.percentage(), 89);
}

TEST(X4BatteryEstimator, RestoredFullImmediatelyUsesFreshLowerBatteryReading) {
  X4BatteryEstimator estimator;
  estimator.restore(100);
  ASSERT_TRUE(estimator.update(0, 4000, 89, false));
  EXPECT_EQ(estimator.percentage(), 89);
}

TEST(X4BatteryEstimator, FreshBatteryBootReconcilesUnknownSleepChargeHistory) {
  // The MCU can be unpowered throughout charging. A saved lower percentage
  // must not freeze the next session below a fresh battery-voltage estimate.
  for (int savedPercent : {0, 20, 80}) {
    X4BatteryEstimator estimator;
    estimator.restore(savedPercent);
    ASSERT_TRUE(estimator.update(0, 4100, 95, false));
    EXPECT_EQ(estimator.percentage(), 95);
    uint32_t now = 0;
    runFor(estimator, now, 30 * 60 * 1000, 4200, 100, false);
    // Once awake, voltage recovery alone cannot add charge to the estimate.
    EXPECT_EQ(estimator.percentage(), 95);
  }
}

TEST(X4BatteryEstimator, RepeatedSleepCannotRestartFullHold) {
  uint16_t savedPercent = 100;
  for (int session = 0; session < 4; ++session) {
    X4BatteryEstimator estimator;
    estimator.restore(savedPercent);
    ASSERT_TRUE(estimator.update(0, 4000, 89, false));
    uint32_t now = 0;
    runFor(estimator, now, 30 * 60 * 1000, 4000, 89, false);
    EXPECT_EQ(estimator.percentage(), 89);
    savedPercent = estimator.percentage();
  }
}

TEST(X4BatteryEstimator, LoadRemovalCannotRaiseEstimateWithoutUsb) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 3900, 80, false));
  uint32_t now = 0;
  runFor(estimator, now, 60 * 60 * 1000, 4080, 93, false);
  EXPECT_EQ(estimator.percentage(), 80);
}

TEST(X4BatteryEstimator, SustainedLowVoltageBypassesStartupSettleAndSlowDischarge) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4200, 100, false));
  ASSERT_TRUE(estimator.update(1500, 3500, 5, false));
  uint32_t now = 1500;
  runFor(estimator, now, 19500, 3500, 5, false);
  EXPECT_EQ(estimator.percentage(), 100);
  runFor(estimator, now, 1500, 3500, 5, false);
  EXPECT_EQ(estimator.percentage(), 5);
}

TEST(X4BatteryEstimator, IsolatedLowVoltageDoesNotCollapseEstimate) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4000, 89, false));
  ASSERT_TRUE(estimator.update(1500, 3500, 5, false));
  ASSERT_TRUE(estimator.update(3000, 4000, 89, false));
  uint32_t now = 3000;
  runFor(estimator, now, 30000, 4000, 89, false);
  EXPECT_EQ(estimator.percentage(), 89);
}

TEST(X4BatteryEstimator, LowVoltageDetectionRestartsAfterRecovery) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4000, 89, false));
  ASSERT_TRUE(estimator.update(1500, 3500, 5, false));
  uint32_t now = 1500;
  runFor(estimator, now, 16500, 3500, 5, false);
  ASSERT_TRUE(estimator.update(19500, 4000, 89, false));
  ASSERT_TRUE(estimator.update(21000, 3500, 5, false));
  now = 21000;
  runFor(estimator, now, 18000, 3500, 5, false);
  EXPECT_EQ(estimator.percentage(), 89);
  runFor(estimator, now, 3000, 3500, 5, false);
  EXPECT_EQ(estimator.percentage(), 5);
}

TEST(X4BatteryEstimator, SustainedLowVoltageCanCorrectStaleUsbEstimate) {
  X4BatteryEstimator estimator;
  estimator.restore(99);
  ASSERT_TRUE(estimator.update(0, 4200, 100, true));
  ASSERT_TRUE(estimator.update(1500, 3500, 5, true));
  uint32_t now = 1500;
  runFor(estimator, now, 21000, 3500, 5, true);
  EXPECT_EQ(estimator.percentage(), 5);
}

TEST(X4BatteryEstimator, DeeplyDischargedBootCannotFollowPolynomialHighTail) {
  // The SDK polynomial is only useful in the normal cell-voltage region:
  // at 2500 mV its extrapolation clamps to 100%, despite the depleted cell.
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 2500, 100, false));
  EXPECT_EQ(estimator.percentage(), 0);
}

TEST(X4BatteryEstimator, SustainedDeepDischargeOverridesPolynomialHighTail) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4000, 84, false));
  ASSERT_TRUE(estimator.update(1500, 2500, 100, false));
  uint32_t now = 1500;
  runFor(estimator, now, 21000, 2500, 100, false);
  EXPECT_EQ(estimator.percentage(), 0);
}

TEST(X4BatteryEstimator, InvalidInputCannotSeedOrOverwriteKnownEstimate) {
  X4BatteryEstimator estimator;
  EXPECT_FALSE(estimator.update(0, 0, 0, false));
  EXPECT_FALSE(estimator.update(1500, 4501, 100, true));
  EXPECT_FALSE(estimator.update(3000, 4000, 101, false));
  EXPECT_FALSE(estimator.known());
  ASSERT_TRUE(estimator.update(4500, 4000, 89, false));
  EXPECT_FALSE(estimator.update(6000, 0, 0, false));
  EXPECT_FALSE(estimator.update(7500, 65535, 0, false));
  EXPECT_EQ(estimator.percentage(), 89);
}

TEST(X4BatteryEstimator, InvalidSamplesBreakLowVoltageConfirmation) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4000, 89, false));
  ASSERT_TRUE(estimator.update(1500, 3500, 5, false));
  EXPECT_FALSE(estimator.update(18000, 0, 0, false));
  EXPECT_FALSE(estimator.update(50000, 0, 0, false));
  ASSERT_TRUE(estimator.update(60000, 3500, 5, false));
  // The two low samples separated by missing ADC readings do not establish
  // twenty seconds of continuously observed low cell voltage.
  EXPECT_EQ(estimator.percentage(), 89);
}

TEST(X4BatteryEstimator, UnobservedLongGapDoesNotConfirmLowVoltage) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4000, 89, false));
  ASSERT_TRUE(estimator.update(1500, 3500, 5, false));
  ASSERT_TRUE(estimator.update(60000, 3500, 5, false));
  EXPECT_EQ(estimator.percentage(), 89);
}

TEST(X4BatteryEstimator, InvalidPersistedValuesFallBackToFreshMeasurement) {
  for (int saved : {-1, 101, 255}) {
    X4BatteryEstimator estimator;
    estimator.restore(saved);
    ASSERT_TRUE(estimator.update(0, 4000, 89, false));
    EXPECT_EQ(estimator.percentage(), 89);
  }
}

TEST(X4BatteryEstimator, MillisRolloverPreservesUnplugTimingAndDischarge) {
  X4BatteryEstimator estimator;
  uint32_t now = std::numeric_limits<uint32_t>::max() - 60000;
  ASSERT_TRUE(estimator.update(now, 4200, 100, true, true));
  now += kPollMs;
  ASSERT_TRUE(estimator.update(now, 4000, 89, false));
  runFor(estimator, now, 117000, 4000, 89, false);
  EXPECT_EQ(estimator.percentage(), 100);
  runFor(estimator, now, 15000, 4000, 89, false);
  EXPECT_LT(estimator.percentage(), 100);
}

TEST(X4BatteryEstimator, MillisRolloverPreservesSustainedLowVoltageDetection) {
  X4BatteryEstimator estimator;
  uint32_t now = std::numeric_limits<uint32_t>::max() - 10000;
  ASSERT_TRUE(estimator.update(now, 4000, 89, false));
  now += kPollMs;
  ASSERT_TRUE(estimator.update(now, 3500, 5, false));
  runFor(estimator, now, 21000, 3500, 5, false);
  EXPECT_EQ(estimator.percentage(), 5);
}

TEST(X4BatteryEstimator, TimeAtTargetCannotBeSpentOnFutureChargeJump) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 3900, 80, true));
  uint32_t now = 0;
  runFor(estimator, now, 60 * 60 * 1000, 3900, 80, true);
  runFor(estimator, now, 9000, 4200, 100, true);
  EXPECT_EQ(estimator.percentage(), 80);
}

TEST(X4BatteryEstimator, UsbEdgeCannotSpendChargeBudgetOnDischarge) {
  X4BatteryEstimator estimator;
  estimator.restore(80);
  ASSERT_TRUE(estimator.update(0, 4200, 100, true));
  ASSERT_TRUE(estimator.update(3000, 4200, 100, true));
  ASSERT_TRUE(estimator.update(4500, 3700, 50, false));
  uint32_t now = 4500;
  runFor(estimator, now, 117000, 3700, 50, false);
  EXPECT_EQ(estimator.percentage(), 80);
}

TEST(X4BatteryEstimator, LongPollGapCannotCauseUnboundedChargeJump) {
  X4BatteryEstimator estimator;
  estimator.restore(50);
  ASSERT_TRUE(estimator.update(0, 4200, 100, true));
  ASSERT_TRUE(estimator.update(24 * 60 * 60 * 1000, 4200, 100, true));
  EXPECT_LE(estimator.percentage(), 51);
}
