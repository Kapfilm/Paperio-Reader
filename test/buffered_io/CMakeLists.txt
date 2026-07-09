add_executable(BufferedFileIOTest
  BufferedFileIOTest.cpp
)

target_include_directories(BufferedFileIOTest PRIVATE
  ${REPO_ROOT}/test/shims
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(BufferedFileIOTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BufferedFileIOTest)
