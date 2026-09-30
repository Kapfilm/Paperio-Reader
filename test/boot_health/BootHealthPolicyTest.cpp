#include <gtest/gtest.h>
#include "src/network/BootHealthPolicy.h"
using namespace boot_health;
static ImageId image(uint32_t address, uint8_t hash) { ImageId i; i.address = address; i.sha[0] = hash; return i; }
TEST(BootHealth, CountsOnlyCrashesAndDoesNotLoseStreakOnSleep) {
  const auto id = image(0x10000, 1);
  auto r = onBoot({}, id, true);
  EXPECT_EQ(r.failures, 0u); // Reset belongs to previous image, not the new one.
  r = onBoot(r, id, true); r = onBoot(r, id, false); r = onBoot(r, id, true);
  EXPECT_EQ(r.failures, 2u);
  r = onBoot(r, id, true); r = onBoot(r, id, true);
  EXPECT_EQ(r.failures, 3u);
}
TEST(BootHealth, ReplacementAtSameAddressDoesNotInheritFailuresOrFallback) {
  auto old = onBoot({}, image(0x10000, 1), false);
  old.failures = 3; old.armed = 1; old.blocked = 1;
  auto fresh = onBoot(old, image(0x10000, 2), true);
  EXPECT_EQ(fresh.failures, 0u); EXPECT_EQ(fresh.armed, 0u); EXPECT_EQ(fresh.blocked, 0u);
}
TEST(BootHealth, OnlyHealthyReaderCanBecomeRecordedFallback) {
  auto r = onBoot({}, image(0x10000, 1), false);
  const auto next = image(0x650000, 2);
  EXPECT_EQ(stageUpdate(r, next).armed, 0u);
  r.trusted = 1;
  EXPECT_TRUE(equal(stageUpdate(r, next).fallback, r.current));
  EXPECT_EQ(stageUpdate(r, next).armed, 1u);
  r.failures = 1; EXPECT_EQ(stageUpdate(r, next).armed, 0u);
  r.failures = 0; r.blocked = 1; EXPECT_EQ(stageUpdate(r, next).armed, 0u);
}
TEST(BootHealth, RollbackLatchSurvivesAllSubsequentBootsOfRejectedImage) {
  auto r = onBoot({}, image(0x10000, 1), false); r.blocked = 1;
  EXPECT_EQ(onBoot(r, r.current, false).blocked, 1u);
  EXPECT_EQ(onBoot(r, r.current, true).blocked, 1u);
}
TEST(BootHealth, DoesNotConfirmSplashOrHungMainLoop) {
  HealthWindow w;
  EXPECT_FALSE(w.tick(0, false, false));
  EXPECT_FALSE(w.tick(90000, false, false));
  EXPECT_FALSE(w.tick(90001, true, false));
  EXPECT_FALSE(w.tick(180000, true, false)); // A single delayed call isn't a healthy minute.
  for (uint32_t t=181000; t<240000; t+=1000) EXPECT_FALSE(w.tick(t,true,false));
  EXPECT_TRUE(w.tick(240000,true,false));
}
TEST(BootHealth, HungRendererCannotConfirmEvenWhenMainLoopLives) {
  HealthWindow w;
  w.tick(0,true,false);
  for (uint32_t t=1000;t<=90000;t+=1000) EXPECT_FALSE(w.tick(t,true,true));
  EXPECT_FALSE(w.tick(91000,true,false));
}
TEST(BootHealth, BriefRenderingIsAllowedAndMillisRolloverIsSafe) {
  HealthWindow w;
  const uint32_t start=UINT32_MAX-30000;
  for(uint32_t delta=0;delta<60000;delta+=1000) EXPECT_FALSE(w.tick(start+delta,true,delta%10000==1000));
  EXPECT_TRUE(w.tick(start+60000,true,false));
}
TEST(BootHealth, InvalidPersistentRecordsNeverSupplyFallback) {
  Record r; r.magic=0; r.trusted=1;r.armed=1;
  auto fresh=onBoot(r,image(0x10000,1),true);
  EXPECT_EQ(fresh.trusted,0u);EXPECT_EQ(fresh.armed,0u);EXPECT_EQ(fresh.failures,0u);
}

TEST(BootHealth, FirstBootDoesNotInheritCrashBetweenInstallAndRestart) {
  auto old = onBoot({}, image(0x10000, 1), false); old.trusted = 1;
  const auto next = image(0x650000, 2);
  auto staged = stageUpdate(old, next);
  auto first = onBoot(staged, next, true);
  EXPECT_EQ(first.failures, 0u); EXPECT_EQ(first.armed, 1u);
  EXPECT_EQ(onBoot(first, next, true).failures, 1u);
}
