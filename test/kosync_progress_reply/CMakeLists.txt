include(FetchContent)
FetchContent_Declare(
  arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(arduinojson)

add_executable(KOReaderProgressReplyTest KOReaderProgressReplyTest.cpp)
target_include_directories(KOReaderProgressReplyTest PRIVATE ${REPO_ROOT}/lib/KOReaderSync ${arduinojson_SOURCE_DIR}/src)
target_link_libraries(KOReaderProgressReplyTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KOReaderProgressReplyTest)
