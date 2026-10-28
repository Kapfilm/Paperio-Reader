add_executable(UiTextSizeTest UiTextSizeTest.cpp ../../src/UiFonts.cpp ../../lib/EpdFont/EpdFont.cpp ../../lib/EpdFont/EpdFontFamily.cpp ../../lib/Utf8/Utf8.cpp)
target_include_directories(UiTextSizeTest BEFORE PRIVATE shims ../../src ../../lib/EpdFont ../../lib/Utf8)
target_link_libraries(UiTextSizeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(UiTextSizeTest)
