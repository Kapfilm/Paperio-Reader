#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include "lib/hal/X4BatteryDisplayScale.h"
#include "lib/hal/X4BatteryEstimator.h"

namespace {
uint16_t displayed(const X4BatteryEstimator& estimator) {
  return X4BatteryDisplayScale::percent(estimator.percentageTenths());
}
}  // namespace

TEST(X4BatteryDisplayScale, DenseDomainIsMonotoneBoundedAndAddsAtMostFourPoints) {
  uint16_t previous = 0;
  uint16_t largestAdjustment = 0;
  for (uint16_t base = 0; base <= 1000; ++base) {
    SCOPED_TRACE(base);
    const uint16_t mapped = X4BatteryDisplayScale::tenths(base);
    EXPECT_GE(mapped, previous);
    EXPECT_GE(mapped, base);
    EXPECT_LE(mapped, 1000);
    EXPECT_LE(mapped - base, 40);
    if (base <= 800) EXPECT_EQ(mapped, base);
    largestAdjustment = std::max<uint16_t>(largestAdjustment, mapped - base);
    previous = mapped;
  }
  EXPECT_EQ(largestAdjustment, 40);
}

TEST(X4BatteryDisplayScale, ExplicitKnotsAreContinuousAtTheirBoundaries) {
  EXPECT_EQ(X4BatteryDisplayScale::tenths(0), 0);
  EXPECT_EQ(X4BatteryDisplayScale::tenths(799), 799);
  EXPECT_EQ(X4BatteryDisplayScale::tenths(800), 800);
  EXPECT_EQ(X4BatteryDisplayScale::tenths(801), 801);
  EXPECT_EQ(X4BatteryDisplayScale::tenths(919), 958);
  EXPECT_EQ(X4BatteryDisplayScale::tenths(920), 960);
  EXPECT_EQ(X4BatteryDisplayScale::tenths(921), 960);
  EXPECT_EQ(X4BatteryDisplayScale::tenths(999), 999);
  EXPECT_EQ(X4BatteryDisplayScale::tenths(1000), 1000);
}

TEST(X4BatteryDisplayScale, SubFullBaseNeverRoundsUpToDisplayed100) {
  for (uint16_t base = 0; base <= 990; ++base) {
    EXPECT_LE(X4BatteryDisplayScale::percent(base), 99) << base;
  }
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4226, 100, true));
  EXPECT_EQ(estimator.percentageTenths(), 990);
  EXPECT_EQ(displayed(estimator), 99);
  for (uint32_t now = 1500; now <= 120000; now += 1500) {
    ASSERT_TRUE(estimator.update(now, 4226, 100, true));
  }
  EXPECT_EQ(estimator.percentageTenths(), 1000);
  EXPECT_EQ(displayed(estimator), 100);
  EXPECT_EQ(X4BatteryDisplayScale::percent(1000), 100);
}

TEST(X4BatteryDisplayScale, OutOfRangeHighInputSaturatesInsteadOfWrapping) {
  EXPECT_EQ(X4BatteryDisplayScale::tenths(1001), 1000);
  EXPECT_EQ(X4BatteryDisplayScale::percent(UINT16_MAX), 100);
}

TEST(X4BatteryDisplayScale, PartialChargeAndUnplugUseTheSameDisplayCoordinates) {
  X4BatteryEstimator estimator;
  estimator.restore(93);
  ASSERT_TRUE(estimator.update(0, 4100, 93, true));
  EXPECT_EQ(estimator.percentage(), 93);
  const uint16_t before = displayed(estimator);
  EXPECT_EQ(before, 96);
  ASSERT_TRUE(estimator.update(1500, 4100, 93, false));
  EXPECT_EQ(estimator.percentage(), 93);
  EXPECT_EQ(displayed(estimator), before);
  for (uint32_t now = 3000; now <= 5 * 60000; now += 1500) {
    ASSERT_TRUE(estimator.update(now, 4100, 93, false));
    EXPECT_EQ(displayed(estimator), before);
  }
}

TEST(X4BatteryDisplayScale, BaseCheckpointRoundTripDoesNotApplyCorrectionTwice) {
  // Model NVS with the public base checkpoint API, exactly as the HAL writes
  // it. The displayed 96 must never be passed back as a base estimate.
  uint16_t checkpoint = 92;
  for (int boot = 0; boot < 6; ++boot) {
    X4BatteryEstimator estimator;
    estimator.restore(checkpoint);
    ASSERT_TRUE(estimator.update(0, 4068, 92, true));
    EXPECT_EQ(estimator.percentage(), 92);
    EXPECT_EQ(displayed(estimator), 96);
    EXPECT_NE(displayed(estimator), estimator.percentage());
    checkpoint = estimator.percentage();
  }
  EXPECT_EQ(checkpoint, 92);
}

TEST(X4BatteryDisplayScale, EveryOldBaseCheckpointStillRestoresWithoutMigration) {
  for (uint16_t oldCheckpoint = 0; oldCheckpoint <= 100; ++oldCheckpoint) {
    SCOPED_TRACE(oldCheckpoint);
    X4BatteryEstimator estimator;
    estimator.restore(oldCheckpoint);
    ASSERT_TRUE(estimator.update(0, 4200, 100, true));
    const uint16_t base = std::min<uint16_t>(oldCheckpoint, 99);
    EXPECT_EQ(estimator.percentage(), base);
    EXPECT_EQ(displayed(estimator), X4BatteryDisplayScale::percent(base * 10));
  }
}

TEST(X4BatteryDisplayScale, BatteryRebootUsesFreshBaseReadingBeforePresentation) {
  X4BatteryEstimator estimator;
  estimator.restore(20);
  ASSERT_TRUE(estimator.update(0, 4068, 92, false));
  EXPECT_EQ(estimator.percentage(), 92);
  EXPECT_EQ(displayed(estimator), 96);
  const uint16_t checkpoint = estimator.percentage();
  X4BatteryEstimator rebooted;
  rebooted.restore(checkpoint);
  ASSERT_TRUE(rebooted.update(0, 4068, 92, false));
  EXPECT_EQ(rebooted.percentage(), checkpoint);
  EXPECT_EQ(displayed(rebooted), displayed(estimator));
}

TEST(X4BatteryDisplayScale, ExplicitChargeCompleteStillHasItsOwnFullState) {
  X4BatteryEstimator estimator;
  estimator.restore(92);
  ASSERT_TRUE(estimator.update(0, 4100, 92, true));
  EXPECT_EQ(displayed(estimator), 96);
  ASSERT_TRUE(estimator.update(1500, 4200, 100, true, true));
  EXPECT_EQ(estimator.percentage(), 100);
  EXPECT_EQ(displayed(estimator), 100);
}

TEST(X4BatteryDisplayScale, DischargeRemainsMonotoneAcrossTheUpperKnots) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 4200, 100, false));
  uint16_t previous = displayed(estimator);
  for (uint32_t now = 1500; now <= 35 * 60000; now += 1500) {
    ASSERT_TRUE(estimator.update(now, 3900, 75, false));
    const uint16_t current = displayed(estimator);
    EXPECT_LE(current, previous);
    previous = current;
  }
  EXPECT_EQ(estimator.percentage(), 75);
  EXPECT_EQ(displayed(estimator), 75);
}

TEST(X4BatteryDisplayScale, DeepDischargeProtectionIsNotRaisedByPresentation) {
  X4BatteryEstimator estimator;
  ASSERT_TRUE(estimator.update(0, 2500, 100, false));
  EXPECT_EQ(estimator.percentage(), 0);
  EXPECT_EQ(displayed(estimator), 0);
}
