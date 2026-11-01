cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(power_button_tests LANGUAGES CXX)
  enable_testing()
endif()
set(POWER_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(power_button_tests power_button_tests.cpp ${POWER_REPO_ROOT}/src/ButtonEventManager.cpp)
target_compile_features(power_button_tests PRIVATE cxx_std_20)
target_compile_options(power_button_tests PRIVATE -Wall -Wextra -Werror -pedantic)
target_include_directories(power_button_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${POWER_REPO_ROOT}/test/shims
  ${POWER_REPO_ROOT}/src
  ${POWER_REPO_ROOT}/lib/hal)
add_test(NAME power_button_tests COMMAND power_button_tests)
