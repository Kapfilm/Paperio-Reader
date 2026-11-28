add_executable(WordClockTest WordClockTest.cpp)
target_include_directories(WordClockTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(WordClockTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WordClockTest)
