add_executable(I18nOffsetTest I18nOffsetTest.cpp)
target_include_directories(I18nOffsetTest PRIVATE ${REPO_ROOT}/lib/I18n)
target_link_libraries(I18nOffsetTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(I18nOffsetTest)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME I18nLargeBlobGenerator
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/large_blob_generator_test.py
    --repo-root ${REPO_ROOT} --cxx ${CMAKE_CXX_COMPILER})
