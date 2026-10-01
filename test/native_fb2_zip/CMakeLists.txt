add_executable(NativeFb2ZipTest
  NativeFb2ZipTest.cpp
  InflateOptimizationTest.cpp
  Fb2CoreTest.cpp
  BufferedScanStorageTest.cpp
  BufferedByteReaderTest.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2Encoding.cpp
  ${REPO_ROOT}/lib/Fb2/native/Fb2Parser.cpp
  ${REPO_ROOT}/lib/Fb2/native/ZipReader.cpp
  ${REPO_ROOT}/lib/Fb2/native/Inflate.cpp
  ${REPO_ROOT}/lib/CooperativeAbort/CooperativeAbort.cpp
)

target_include_directories(NativeFb2ZipTest PRIVATE
  ${REPO_ROOT}/test/shims
  ${REPO_ROOT}/lib/Fb2
  ${REPO_ROOT}/lib/Fb2/native
  ${REPO_ROOT}/lib/CooperativeAbort
)

target_link_libraries(NativeFb2ZipTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NativeFb2ZipTest)
