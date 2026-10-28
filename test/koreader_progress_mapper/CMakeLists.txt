add_executable(ProgressMapperTest
  ProgressMapperTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ProgressMapper.cpp
  ${REPO_ROOT}/test/zip_entry_reader/LoggingStub.cpp
)
target_include_directories(ProgressMapperTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/test/shims
)
target_link_libraries(ProgressMapperTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ProgressMapperTest)
