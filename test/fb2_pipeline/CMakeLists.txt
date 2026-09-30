cmake_minimum_required(VERSION 3.16)
project(Fb2Pipeline C CXX)
set(CMAKE_CXX_STANDARD 20)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${REPO_ROOT}/test" existing EXCLUDE_FROM_ALL)
get_target_property(EPUB_SOURCES EpubPipelineTest SOURCES)
list(FILTER EPUB_SOURCES INCLUDE REGEX "^/")
get_target_property(EPUB_INCLUDES EpubPipelineTest INCLUDE_DIRECTORIES)
add_executable(fb2_pipeline
  Main.cpp ${EPUB_SOURCES}
  ${REPO_ROOT}/test/epub_pipeline/LoggingImpl.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2NavigationIndex.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2AnchorIndex.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2Encoding.cpp
  ${REPO_ROOT}/lib/Fb2/native/Fb2Parser.cpp
  ${REPO_ROOT}/lib/Fb2/native/ZipReader.cpp
  ${REPO_ROOT}/lib/Fb2/native/Inflate.cpp)
target_include_directories(fb2_pipeline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/lib/Fb2 ${EPUB_INCLUDES})
target_compile_definitions(fb2_pipeline PRIVATE ENABLE_SERIAL_LOG=1 LOG_LEVEL=2)
target_link_libraries(fb2_pipeline PRIVATE SaxParser)

add_executable(nav_index_test NavIndexTest.cpp ${REPO_ROOT}/lib/Fb2/Fb2NavigationIndex.cpp)
target_include_directories(nav_index_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/lib/Fb2 ${EPUB_INCLUDES})

get_target_property(FB2_PIPELINE_SOURCES fb2_pipeline SOURCES)
list(REMOVE_ITEM FB2_PIPELINE_SOURCES Main.cpp)
add_executable(fb2_notes NoteRegression.cpp ${FB2_PIPELINE_SOURCES})
target_include_directories(fb2_notes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/lib/Fb2 ${EPUB_INCLUDES})
target_compile_definitions(fb2_notes PRIVATE ENABLE_SERIAL_LOG=1 LOG_LEVEL=1)
target_link_libraries(fb2_notes PRIVATE SaxParser)
set(FB2_NOTES_BASELINE_DIR "" CACHE PATH "Optional original converter and Epub sources for before/after comparison")
if(FB2_NOTES_BASELINE_DIR)
  set(BASELINE_SOURCES ${FB2_PIPELINE_SOURCES})
  list(FILTER BASELINE_SOURCES EXCLUDE REGEX "/lib/(Fb2/Fb2|Epub/Epub)\.cpp$")
  add_executable(fb2_notes_baseline NoteRegression.cpp ${BASELINE_SOURCES}
    ${FB2_NOTES_BASELINE_DIR}/Fb2.cpp ${FB2_NOTES_BASELINE_DIR}/Epub.cpp)
  target_include_directories(fb2_notes_baseline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/lib/Fb2 ${EPUB_INCLUDES})
  target_compile_definitions(fb2_notes_baseline PRIVATE ENABLE_SERIAL_LOG=1 LOG_LEVEL=1)
  target_link_libraries(fb2_notes_baseline PRIVATE SaxParser)
endif()

add_executable(anchor_index_test AnchorIndexTest.cpp ${REPO_ROOT}/lib/Fb2/Fb2AnchorIndex.cpp)
target_include_directories(anchor_index_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/lib/Fb2 ${EPUB_INCLUDES})
add_executable(fb2_crossrefs CrossReferenceRegression.cpp ${FB2_PIPELINE_SOURCES})
target_include_directories(fb2_crossrefs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REPO_ROOT}/lib/Fb2 ${EPUB_INCLUDES})
target_compile_definitions(fb2_crossrefs PRIVATE ENABLE_SERIAL_LOG=1 LOG_LEVEL=1)
target_link_libraries(fb2_crossrefs PRIVATE SaxParser)
