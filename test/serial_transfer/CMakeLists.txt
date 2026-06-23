add_executable(SerialTransferTest
  SerialTransferTest.cpp
  ${REPO_ROOT}/lib/SerialTransfer/SerialTransfer/SerialTransferProtocol.cpp
)

target_include_directories(SerialTransferTest PRIVATE
  ${REPO_ROOT}/test/shims
  ${REPO_ROOT}/lib/SerialTransfer
)

target_link_libraries(SerialTransferTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SerialTransferTest)
