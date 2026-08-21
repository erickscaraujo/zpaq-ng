cmake_minimum_required(VERSION 3.16)
project(zpaq_ng CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# Sanitizer builds: cmake -DSANITIZE=address,undefined -B build-asan
# Must be set before targets are created so the flags reach every target.
if(SANITIZE)
  string(REPLACE "," ";" _san_list "${SANITIZE}")
  foreach(_s IN LISTS _san_list)
    add_compile_options(-fsanitize=${_s} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${_s})
  endforeach()
endif()

# Core library: faithful port of libzpaq (compression/decompression core).
add_library(zpaq_ng_core STATIC
  src/entropy/arithmetic.cpp
  src/integrity/sha1.cpp
  src/integrity/crc32.cpp
  src/prediction/state_table.cpp
  src/prediction/predictor_model.cpp
  src/prediction/predictor.cpp
  src/compression/zpaql_compiler.cpp
  src/compression/zpaql_vm.cpp
  src/compression/zpaql_exec.cpp
  src/compression/post_processor.cpp
  src/compression/block_encoder.cpp
  src/decompression/block_decoder.cpp
)
target_include_directories(zpaq_ng_core PUBLIC src)

# JIDAC journaling archive layer + methods/preprocessing.
add_library(zpaq_ng_archive STATIC
  src/compression/lz77.cpp
  src/compression/make_config.cpp
  src/compression/compress_block.cpp
  src/archive/jidac.cpp
  src/hardware/hardware.cpp
  src/simd/simd.cpp
  src/analyzer/analyzer.cpp
  src/chunking/chunking.cpp
  src/threading/thread_pool.cpp
  src/compression_ng/engine.cpp
  src/benchmark/benchmark.cpp
  src/cli/ng_cli.cpp
  src/recovery/recovery.cpp
  src/matching/matching.cpp
)
target_link_libraries(zpaq_ng_archive PUBLIC zpaq_ng_core)
target_include_directories(zpaq_ng_archive PUBLIC src)

# Command line tool.
add_executable(zpaq_ng tools/zpaq_ng.cpp)
target_link_libraries(zpaq_ng PRIVATE zpaq_ng_archive)

enable_testing()
add_subdirectory(tests)

# Cross-compatibility regression target (requires the original binary).
add_custom_target(check-compat
  COMMAND ${CMAKE_COMMAND} -DZPAQ_NG=$<TARGET_FILE:zpaq_ng>
          -DZPAQ_ORIG=$ENV{ZPAQ_ORIG}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/compat.cmake
  COMMENT "Running cross-compatibility suite vs original zpaq")