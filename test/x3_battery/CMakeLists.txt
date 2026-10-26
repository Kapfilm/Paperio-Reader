cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(x3_battery_tests LANGUAGES CXX)
  enable_testing()
endif()
add_executable(x3_battery_tests
  x3_battery_tests.cpp
  ../../lib/hal/Bq27220Capacity.cpp
)
target_compile_features(x3_battery_tests PRIVATE cxx_std_17)
target_compile_options(x3_battery_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(x3_battery_tests PRIVATE ../../lib/hal)
add_test(NAME x3_battery_tests COMMAND x3_battery_tests)
