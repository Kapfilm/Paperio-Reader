add_executable(Fb2SyncPositionTest Fb2SyncPositionTest.cpp)
target_include_directories(Fb2SyncPositionTest PRIVATE
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub/Epub/parsers)
target_link_libraries(Fb2SyncPositionTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(Fb2SyncPositionTest)
