#include <gtest/gtest.h>

#include <limits>

#include "hal/TiltGesture.h"
TEST(TiltGesture, BothAxesAndOrientations) {
  const float side[] = {10, -20, -10, 20}, fore[] = {20, 10, -20, -10};
  for (uint8_t o = 0; o < 4; ++o) {
    EXPECT_FLOAT_EQ(tilt_gesture::axis(10, 20, o, 0), side[o]);
    EXPECT_FLOAT_EQ(tilt_gesture::axis(10, 20, o, 1), -side[o]);
    EXPECT_FLOAT_EQ(tilt_gesture::axis(10, 20, o, 2), fore[o]);
    EXPECT_FLOAT_EQ(tilt_gesture::axis(10, 20, o, 3), -fore[o]);
  }
}
TEST(TiltGesture, HoldAndCooldownDoNotRepeat) {
  tilt_gesture::Detector d;
  d.reset(100);
  EXPECT_EQ(d.update(300, 500), 0);
  EXPECT_EQ(d.update(300, 700), 1);
  EXPECT_EQ(d.update(300, 1500), 0);
  EXPECT_EQ(d.update(0, 1600), 0);
  EXPECT_EQ(d.update(-300, 1700), -1);
}
TEST(TiltGesture, NeutralDoesNotBypassCooldown) {
  tilt_gesture::Detector d;
  d.reset(0);
  EXPECT_EQ(d.update(300, 600), 1);
  EXPECT_EQ(d.update(0, 610), 0);
  EXPECT_EQ(d.update(300, 700), 0);
  EXPECT_EQ(d.update(300, 1200), 1);
}
TEST(TiltGesture, NoiseResetAndWrapAreSafe) {
  tilt_gesture::Detector d;
  d.reset(UINT32_MAX - 200);
  EXPECT_EQ(d.update(100, 500), 0);
  EXPECT_EQ(d.update(std::numeric_limits<float>::quiet_NaN(), 500), 0);
  EXPECT_EQ(d.update(300, 500), 1);
  d.reset(600);
  EXPECT_EQ(d.update(300, 601), 0);
}
