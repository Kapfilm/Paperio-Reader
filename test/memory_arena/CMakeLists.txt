add_executable(BuildArenaTest
  BuildArenaTest.cpp
)

target_include_directories(BuildArenaTest PRIVATE
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(BuildArenaTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BuildArenaTest)
