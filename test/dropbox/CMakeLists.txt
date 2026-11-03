include(FetchContent)
FetchContent_GetProperties(arduinojson SOURCE_DIR arduinojson_SOURCE_DIR)

add_executable(DropboxClientTest
  DropboxClientTest.cpp
  ${REPO_ROOT}/src/DropboxStore.cpp
  ${REPO_ROOT}/src/network/DropboxClient.cpp
)
target_include_directories(DropboxClientTest BEFORE PRIVATE
  shims ${REPO_ROOT}/src ${REPO_ROOT}/src/network ${arduinojson_SOURCE_DIR}/src
)
target_link_libraries(DropboxClientTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_compile_options(DropboxClientTest PRIVATE -Wno-deprecated-declarations)
if(NOT APPLE)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(DropboxClientTest PRIVATE OpenSSL::Crypto)
endif()
gtest_discover_tests(DropboxClientTest)
