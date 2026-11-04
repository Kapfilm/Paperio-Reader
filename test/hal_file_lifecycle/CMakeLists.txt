find_package(Threads REQUIRED)
add_executable(HalFileLifecycleTest HalFileLifecycleTest.cpp
  ${REPO_ROOT}/lib/hal/HalStorage.cpp ${REPO_ROOT}/lib/hal/HalSpiBus.cpp)
target_include_directories(HalFileLifecycleTest PRIVATE shims ${REPO_ROOT}/lib/hal)
target_link_libraries(HalFileLifecycleTest PRIVATE gtest_main Threads::Threads)
gtest_discover_tests(HalFileLifecycleTest)
