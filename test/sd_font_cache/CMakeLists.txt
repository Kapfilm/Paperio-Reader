add_executable(SdFontCacheTest SdFontCacheTest.cpp ../../lib/EpdFont/SdCardFontManager.cpp)
target_include_directories(SdFontCacheTest BEFORE PRIVATE shims ../../lib/EpdFont)
target_link_libraries(SdFontCacheTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SdFontCacheTest)
