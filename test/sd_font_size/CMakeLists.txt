add_executable(SdFontSizeTest SdFontSizeTest.cpp ../../src/SdCardFontSystem.cpp ../../lib/EpdFont/SdCardFontRegistry.cpp)
target_include_directories(SdFontSizeTest BEFORE PRIVATE shims ../../src ../../lib/EpdFont)
target_link_libraries(SdFontSizeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SdFontSizeTest)
