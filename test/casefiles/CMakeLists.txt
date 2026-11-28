add_executable(CaseFilesTest CaseFilesTest.cpp ../../src/activities/games/casefiles/CaseFilesModel.cpp)
target_include_directories(CaseFilesTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(CaseFilesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CaseFilesTest)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CASE_TEXT_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/text_fixture")
add_custom_command(OUTPUT "${CASE_TEXT_FIXTURE_DIR}/I18n.h"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_text_fixture.py"
    "${REPO_ROOT}/src/activities/games/casefiles/translations.json" "${CASE_TEXT_FIXTURE_DIR}/I18n.h"
  DEPENDS generate_text_fixture.py "${REPO_ROOT}/src/activities/games/casefiles/translations.json")
add_executable(CaseFilesTextTest CaseFilesTextTest.cpp
  "${CASE_TEXT_FIXTURE_DIR}/I18n.h"
  ../../src/activities/games/casefiles/CaseFilesModel.cpp
  ../../src/activities/games/casefiles/CaseFilesText.cpp)
target_include_directories(CaseFilesTextTest BEFORE PRIVATE "${CASE_TEXT_FIXTURE_DIR}" ${REPO_ROOT}/src)
target_link_libraries(CaseFilesTextTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CaseFilesTextTest)
