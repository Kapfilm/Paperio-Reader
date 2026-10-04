add_executable(ReaderFontSizeTest ReaderFontSizeTest.cpp)
target_link_libraries(ReaderFontSizeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReaderFontSizeTest)

add_executable(FontSizeSelectorTest FontSizeSelectorTest.cpp)
target_include_directories(FontSizeSelectorTest BEFORE PRIVATE ../../src ../../lib/I18n ../sd_font_size/shims)
target_link_libraries(FontSizeSelectorTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FontSizeSelectorTest)
