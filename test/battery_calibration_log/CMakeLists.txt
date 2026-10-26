add_executable(BatteryCalibrationLogTest
  BatteryCalibrationLogTest.cpp
  ${REPO_ROOT}/lib/hal/BatteryCalibrationLog.cpp
)
target_include_directories(BatteryCalibrationLogTest BEFORE PRIVATE shims)
target_link_libraries(BatteryCalibrationLogTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BatteryCalibrationLogTest)
