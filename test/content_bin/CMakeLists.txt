add_executable(CompiledContentTest
  CompiledContentTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/content/CompiledContent.cpp
)

target_include_directories(CompiledContentTest PRIVATE
  ${REPO_ROOT}/test/shims        # HalStorage.h (FsFile), Arduino.h
  ${REPO_ROOT}/lib/Epub          # Epub/css/CssStyle.h, Epub/content/CompiledContent.h
  ${REPO_ROOT}/lib/Epub/Epub/content
  ${REPO_ROOT}/lib/Serialization # Serialization.h
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/Logging
)

target_link_libraries(CompiledContentTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CompiledContentTest)
