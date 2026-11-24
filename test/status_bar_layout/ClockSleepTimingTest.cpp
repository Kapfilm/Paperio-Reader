#include <gtest/gtest.h>
#include "util/ClockSleepTiming.h"

TEST(ClockSleepTiming, SleepIsBoundedForWatchdogAndButtonFallback) {
  EXPECT_EQ(clock_sleep::waitToMinuteUs(120, 0), 2000000u);
  EXPECT_EQ(clock_sleep::waitToMinuteUs(147, 500000), 2000000u);
}
TEST(ClockSleepTiming, LastSecondWakesAtMinuteBoundary) {
  EXPECT_EQ(clock_sleep::waitToMinuteUs(179, 0), 1000000u);
  EXPECT_EQ(clock_sleep::waitToMinuteUs(179, 900000), 100000u);
  EXPECT_EQ(clock_sleep::waitToMinuteUs(179, 999999), 1u);
}
TEST(ClockSleepTiming, ClockRolloverAndUnalignedSecondsStayMonotonic) {
  EXPECT_EQ(clock_sleep::waitToMinuteUs(86399, 999000), 1000u);
  EXPECT_EQ(clock_sleep::waitToMinuteUs(86400, 0), 2000000u);
  EXPECT_EQ(clock_sleep::waitToMinuteUs(178, 500000), 1500000u);
}
