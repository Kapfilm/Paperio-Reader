add_executable(LamplighterTest LamplighterTest.cpp ${REPO_ROOT}/src/activities/games/lamplighter/LamplighterModel.cpp)
target_include_directories(LamplighterTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(LamplighterTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(LamplighterTest)
