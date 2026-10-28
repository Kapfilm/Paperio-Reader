#include <gtest/gtest.h>

#include "../../src/activities/reader/ReaderUtils.h"

TEST(ReaderRefreshCycle, X3DarkFifteenRefreshesAt15And30And45) {
  SETTINGS.frequency = 15;
  GfxRenderer renderer;
  int remaining = 15;
  for (int page = 1; page <= 45; ++page) {
    EXPECT_EQ(ReaderUtils::nextRefreshCycleMode(renderer, remaining),
              page % 15 == 0 ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH)
        << page;
  }
}

TEST(ReaderRefreshCycle, BothBoardsAndPolaritiesHonourConfiguredCadence) {
  for (bool x3 : {false, true}) {
    for (bool dark : {false, true}) {
      for (int frequency : {1, 5, 15, 30}) {
        GfxRenderer renderer;
        renderer.x3 = x3;
        renderer.dark = dark;
        SETTINGS.frequency = frequency;
        int remaining = frequency;
        for (int page = 1; page <= 2 * frequency; ++page) {
          EXPECT_EQ(ReaderUtils::nextRefreshCycleMode(renderer, remaining),
                    page % frequency == 0 ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
        }
      }
    }
  }
}

TEST(ReaderRefreshCycle, DisplayAndTriggerUseSameInterval) {
  SETTINGS.frequency = 15;
  GfxRenderer display, trigger;
  int displayCounter = 15, triggerCounter = 15;
  for (int page = 0; page < 45; ++page) {
    ReaderUtils::displayWithRefreshCycle(display, displayCounter);
    ReaderUtils::triggerWithRefreshCycle(trigger, triggerCounter);
  }
  EXPECT_EQ(display.frames, trigger.frames);
  EXPECT_EQ(displayCounter, triggerCounter);
  EXPECT_EQ(display.frames[4], HalDisplay::FAST_REFRESH);
  EXPECT_EQ(display.frames[14], HalDisplay::HALF_REFRESH);
}

TEST(ReaderRefreshCycle, DisabledPeriodicRefreshStaysFast) {
  SETTINGS.frequency = 0;
  GfxRenderer renderer;
  int remaining = 1;
  for (int page = 0; page < 50; ++page) {
    EXPECT_EQ(ReaderUtils::nextRefreshCycleMode(renderer, remaining), HalDisplay::FAST_REFRESH);
  }
  EXPECT_EQ(remaining, 1);
}

TEST(ReaderRefreshCycle, ShorterNewSettingClampsOutstandingCounter) {
  SETTINGS.frequency = 5;
  GfxRenderer renderer;
  int remaining = 15;
  for (int page = 1; page <= 5; ++page) {
    EXPECT_EQ(ReaderUtils::nextRefreshCycleMode(renderer, remaining),
              page == 5 ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  }
  EXPECT_EQ(remaining, 5);
}
