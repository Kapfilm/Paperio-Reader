#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "lib/hal/BatteryCalibrationLog.h"

namespace {
std::vector<std::array<uint32_t, 5>> samples(const std::string& csv) {
  std::vector<std::array<uint32_t, 5>> result;
  std::istringstream lines(csv);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty() || line.front() == '#' || line.starts_with("uptime_ms")) continue;
    std::istringstream fields(line);
    std::array<uint32_t, 5> row{};
    for (auto& value : row) {
      std::string field;
      if (!std::getline(fields, field, ',')) {
        ADD_FAILURE() << "Incomplete calibration row: " << line;
        return {};
      }
      value = static_cast<uint32_t>(std::stoul(field));
    }
    result.push_back(row);
  }
  return result;
}

class BatteryCalibrationLogTest : public ::testing::Test {
 protected:
  void SetUp() override {
    calibration_test::state = {};
    calibration_test::state.markerPresent = true;
    calibration_test::nowMs = 10000;
    calibration_test::errorLogs = 0;
    calibration_test::infoLogs = 0;
  }
  BatteryCalibrationLog logger;
};
}  // namespace

TEST_F(BatteryCalibrationLogTest, NoEnableMarkerMeansNoWritesOrCreatedLog) {
  calibration_test::state.markerPresent = false;
  EXPECT_FALSE(logger.begin());
  logger.record(10000, 4000, 84, 83, false);
  EXPECT_FALSE(logger.enabled());
  EXPECT_EQ(calibration_test::state.opens, 0u);
  EXPECT_EQ(calibration_test::state.writes, 0u);
  EXPECT_TRUE(calibration_test::state.data.empty());
  EXPECT_EQ(calibration_test::errorLogs, 0u);
}

TEST_F(BatteryCalibrationLogTest, UnavailableStorageIsNotAccessedBeforeEnableCheck) {
  calibration_test::state.ready = false;
  EXPECT_FALSE(logger.begin());
  logger.record(10000, 4000, 84, 83, false);
  EXPECT_EQ(calibration_test::state.existsChecks, 0u);
  EXPECT_EQ(calibration_test::state.opens, 0u);
  EXPECT_EQ(calibration_test::state.writes, 0u);
}

TEST_F(BatteryCalibrationLogTest, BootCreatesHeaderAndMarkerAndFirstRecordIsImmediate) {
  ASSERT_TRUE(logger.begin());
  EXPECT_TRUE(calibration_test::state.data.starts_with("uptime_ms,millivolts,raw_percent,estimate_percent,usb\n"));
  EXPECT_NE(calibration_test::state.data.find("# boot uptime_ms=10000\n"), std::string::npos);
  logger.record(10001, 4000, 84, 83, false);
  const auto rows = samples(calibration_test::state.data);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0], (std::array<uint32_t, 5>{10001, 4000, 84, 83, 0}));
  EXPECT_EQ(calibration_test::state.opens, calibration_test::state.closes);
  EXPECT_EQ(calibration_test::state.lastOpenFlags, O_WRONLY | O_CREAT | O_APPEND);
}

TEST_F(BatteryCalibrationLogTest, UnchangedUsbLogsAtMostOnceEveryFifteenSeconds) {
  ASSERT_TRUE(logger.begin());
  logger.record(10000, 4000, 84, 83, false);
  const auto writesAfterFirst = calibration_test::state.writes;
  logger.record(10001, 3999, 84, 83, false);
  logger.record(24999, 3998, 84, 83, false);
  EXPECT_EQ(calibration_test::state.writes, writesAfterFirst);
  logger.record(25000, 3997, 84, 83, false);
  EXPECT_EQ(samples(calibration_test::state.data).size(), 2u);
}

TEST_F(BatteryCalibrationLogTest, UsbEdgesAreImmediateAndRestartCadence) {
  ASSERT_TRUE(logger.begin());
  logger.record(10000, 4000, 84, 83, false);
  logger.record(11000, 4100, 95, 83, true);
  logger.record(12000, 4000, 84, 83, false);
  logger.record(26999, 4000, 84, 83, false);
  ASSERT_EQ(samples(calibration_test::state.data).size(), 3u);
  logger.record(27000, 4000, 84, 83, false);
  const auto rows = samples(calibration_test::state.data);
  ASSERT_EQ(rows.size(), 4u);
  EXPECT_EQ(rows[1][4], 1u);
  EXPECT_EQ(rows[2][4], 0u);
  EXPECT_EQ(rows.back()[0], 27000u);
}

TEST_F(BatteryCalibrationLogTest, CadenceSurvivesUptimeCounterWrap) {
  ASSERT_TRUE(logger.begin());
  const uint32_t first = std::numeric_limits<uint32_t>::max() - 5000;
  logger.record(first, 4000, 84, 83, false);
  logger.record(first + 14999u, 4000, 84, 83, false);
  ASSERT_EQ(samples(calibration_test::state.data).size(), 1u);
  logger.record(first + 15000u, 4000, 84, 83, false);
  EXPECT_EQ(samples(calibration_test::state.data).size(), 2u);
}

TEST_F(BatteryCalibrationLogTest, ExistingLogIsPreservedAndNewBootIsDistinguishable) {
  const std::string old =
      "uptime_ms,millivolts,raw_percent,estimate_percent,usb\n# boot uptime_ms=20\n"
      "100,3900,70,70,0\n";
  calibration_test::state.data = old;
  ASSERT_TRUE(logger.begin());
  EXPECT_TRUE(calibration_test::state.data.starts_with(old));
  EXPECT_EQ(calibration_test::state.data.find("uptime_ms,millivolts", old.size()), std::string::npos);
  EXPECT_NE(calibration_test::state.data.find("# boot", old.size()), std::string::npos);
  logger.record(10001, 4000, 84, 83, false);
  const auto rows = samples(calibration_test::state.data);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows.front()[0], 100u);
  EXPECT_EQ(rows.back()[0], 10001u);
  EXPECT_EQ(calibration_test::state.opens, calibration_test::state.closes);
}

TEST_F(BatteryCalibrationLogTest, OpenFailureDisablesFurtherAttemptsAndLogsOnce) {
  calibration_test::state.failOpen = true;
  EXPECT_FALSE(logger.begin());
  const auto opens = calibration_test::state.opens;
  for (uint32_t now = 0; now < 100000; now += 1500) logger.record(now, 4000, 84, 83, false);
  EXPECT_EQ(calibration_test::state.opens, opens);
  EXPECT_EQ(calibration_test::errorLogs, 1u);
}

TEST_F(BatteryCalibrationLogTest, WriteFailureClosesFileAndDisablesFurtherAttempts) {
  ASSERT_TRUE(logger.begin());
  calibration_test::state.failWrite = true;
  logger.record(10000, 4000, 84, 83, false);
  const auto writes = calibration_test::state.writes;
  logger.record(30000, 4000, 84, 83, true);
  EXPECT_FALSE(logger.enabled());
  EXPECT_EQ(calibration_test::state.writes, writes);
  EXPECT_EQ(calibration_test::state.opens, calibration_test::state.closes);
  EXPECT_EQ(calibration_test::errorLogs, 1u);
}

TEST_F(BatteryCalibrationLogTest, PartialWriteAlsoDisablesLog) {
  ASSERT_TRUE(logger.begin());
  calibration_test::state.shortWrite = true;
  logger.record(10000, 4000, 84, 83, false);
  EXPECT_FALSE(logger.enabled());
  EXPECT_EQ(calibration_test::state.opens, calibration_test::state.closes);
  EXPECT_EQ(calibration_test::errorLogs, 1u);
}

TEST_F(BatteryCalibrationLogTest, FailedCloseDisablesLogEvenAfterSuccessfulWrite) {
  ASSERT_TRUE(logger.begin());
  calibration_test::state.failClose = true;
  logger.record(10000, 4000, 84, 83, false);
  const auto writes = calibration_test::state.writes;
  logger.record(30000, 4000, 84, 83, false);
  EXPECT_FALSE(logger.enabled());
  EXPECT_EQ(calibration_test::state.writes, writes);
  EXPECT_EQ(calibration_test::errorLogs, 1u);
}

TEST_F(BatteryCalibrationLogTest, CardRemovalDisablesWithoutFurtherOpenAttempts) {
  ASSERT_TRUE(logger.begin());
  const auto opens = calibration_test::state.opens;
  calibration_test::state.ready = false;
  logger.record(10000, 4000, 84, 83, false);
  calibration_test::state.ready = true;
  logger.record(30000, 4000, 84, 83, false);
  EXPECT_FALSE(logger.enabled());
  EXPECT_EQ(calibration_test::state.opens, opens);
  EXPECT_EQ(calibration_test::errorLogs, 1u);
}

TEST_F(BatteryCalibrationLogTest, ExistingFullLogIsNotChangedOrDeleted) {
  calibration_test::state.data = "existing calibration bytes";
  calibration_test::state.virtualSize = 8ULL * 1024 * 1024;
  const auto old = calibration_test::state.data;
  EXPECT_FALSE(logger.begin());
  EXPECT_EQ(calibration_test::state.data, old);
  EXPECT_EQ(calibration_test::state.writes, 0u);
  EXPECT_EQ(calibration_test::state.opens, calibration_test::state.closes);
  EXPECT_EQ(calibration_test::errorLogs, 1u);
}

TEST_F(BatteryCalibrationLogTest, NewRowCannotCrossSizeLimit) {
  ASSERT_TRUE(logger.begin());
  calibration_test::state.virtualSize = 8ULL * 1024 * 1024 - 1;
  const auto old = calibration_test::state.data;
  logger.record(10000, 4000, 84, 83, false);
  EXPECT_FALSE(logger.enabled());
  EXPECT_EQ(calibration_test::state.data, old);
  EXPECT_EQ(*calibration_test::state.virtualSize, 8ULL * 1024 * 1024 - 1);
  EXPECT_EQ(calibration_test::errorLogs, 1u);
}
