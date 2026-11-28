add_executable(KakuroTest KakuroTest.cpp KakuroUiTest.cpp ${REPO_ROOT}/src/activities/games/kakuro/KakuroBoard.cpp)
target_include_directories(KakuroTest PRIVATE ${REPO_ROOT}/test/shims ${REPO_ROOT}/src)
target_link_libraries(KakuroTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KakuroTest)
