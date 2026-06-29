enable_language(C)

# tinflate.c compiled once per LZ-copy variant, each into its own executable so
# the two variants (which export the same symbols) never share a link.
function(add_uzlib_lzcopy_variant suffix use_memcpy)
  set(uzlib_lib "uzlib_lz_${suffix}")
  add_library(${uzlib_lib} STATIC ${REPO_ROOT}/lib/uzlib/src/tinflate.c)
  target_include_directories(${uzlib_lib} PUBLIC ${REPO_ROOT}/lib/uzlib/src)
  target_compile_definitions(${uzlib_lib} PUBLIC UZLIB_CONF_USE_MEMCPY=${use_memcpy})

  set(test_exe "UzlibLzCopyTest_${suffix}")
  add_executable(${test_exe} UzlibLzCopyTest.cpp UzlibChksumStubs.cpp)
  target_compile_definitions(${test_exe} PRIVATE UZLIB_CONF_USE_MEMCPY=${use_memcpy})
  target_include_directories(${test_exe} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${test_exe} PRIVATE
    crosspoint_test_common
    ${uzlib_lib}
    GTest::gtest_main
  )
  gtest_discover_tests(${test_exe} TEST_PREFIX "${suffix}.")
endfunction()

add_uzlib_lzcopy_variant(byte 0)
add_uzlib_lzcopy_variant(memcpy 1)
