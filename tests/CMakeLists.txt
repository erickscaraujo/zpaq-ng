# Round-trip test: add a file to an archive, extract it, and compare bytes.
add_test(NAME roundtrip
         COMMAND ${CMAKE_COMMAND} -DZPAQ_NG=$<TARGET_FILE:zpaq_ng> -P
                 ${CMAKE_CURRENT_SOURCE_DIR}/roundtrip.cmake)
set_tests_properties(roundtrip PROPERTIES TIMEOUT 120)

# Recovery: build an archive in memory, corrupt a block, and verify partial
# recovery keeps only fully validated blocks.
add_executable(recovery_test recovery_test.cpp)
target_link_libraries(recovery_test PRIVATE zpaq_ng_archive)
add_test(NAME recovery_test COMMAND recovery_test)
set_tests_properties(recovery_test PROPERTIES TIMEOUT 120)

# Fuzz smoke: the archive/block decoders must never crash or throw on
# arbitrary (random/truncated) input. The same source doubles as a libFuzzer
# entry point under clang -fsanitize=fuzzer.
add_executable(fuzz_archive_smoke ${CMAKE_SOURCE_DIR}/src/fuzz/fuzz_archive.cpp)
target_compile_definitions(fuzz_archive_smoke PRIVATE ZPAQ_FUZZ_STANDALONE)
target_link_libraries(fuzz_archive_smoke PRIVATE zpaq_ng_archive)
add_test(NAME fuzz_smoke COMMAND fuzz_archive_smoke 2000 1)
set_tests_properties(fuzz_smoke PROPERTIES TIMEOUT 120)

# libFuzzer target (clang only): cmake -DSANITIZE=fuzzer -B build-fuzz
if(SANITIZE MATCHES "fuzzer")
  add_executable(fuzz_archive ${CMAKE_SOURCE_DIR}/src/fuzz/fuzz_archive.cpp)
  target_link_libraries(fuzz_archive PRIVATE zpaq_ng_archive)
endif()