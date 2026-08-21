# Round-trip test for zpaq_ng.
# Usage: cmake -DZPAQ_NG=<exe> -P roundtrip.cmake
# Creates <build>/rt_dir, adds files, extracts them, compares bytes.

if(NOT DEFINED ZPAQ_NG)
  message(FATAL_ERROR "ZPAQ_NG not defined")
endif()

set(WORK "${CMAKE_CURRENT_BINARY_DIR}/rt_dir")
file(MAKE_DIRECTORY "${WORK}")

# Deterministic text content.
set(CONTENT "")
string(REPEAT "The quick brown fox jumps over the lazy dog. 0123456789\n" 9000 CONTENT)
file(WRITE "${WORK}/sample.txt" "${CONTENT}")

# A second text file with a different pattern (forces a second fragment).
set(CONTENT2 "")
string(REPEAT "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\n" 3000 CONTENT2)
file(WRITE "${WORK}/pattern.txt" "${CONTENT2}")

# Add to archive using relative paths (stored names stay relative).
execute_process(COMMAND "${ZPAQ_NG}" a "test.zpaq" "sample.txt" "pattern.txt"
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE RES)
if(NOT RES EQUAL 0)
  message(FATAL_ERROR "add failed with code ${RES}")
endif()

# Extract to out/.
execute_process(COMMAND "${ZPAQ_NG}" x "test.zpaq" -to "out" -force
  WORKING_DIRECTORY "${WORK}"
  RESULT_VARIABLE RES)
if(NOT RES EQUAL 0)
  message(FATAL_ERROR "extract failed with code ${RES}")
endif()

# Compare extracted bytes (out/<stored name>).
foreach(F sample.txt pattern.txt)
  set(SRC "${WORK}/${F}")
  set(DST "${WORK}/out/${F}")
  if(NOT EXISTS "${SRC}")
    message(FATAL_ERROR "source ${SRC} missing")
  endif()
  if(NOT EXISTS "${DST}")
    message(FATAL_ERROR "extracted ${DST} missing")
  endif()
  file(READ "${SRC}" SA HEX)
  file(READ "${DST}" DA HEX)
  if(NOT SA STREQUAL DA)
    message(FATAL_ERROR "content mismatch for ${F}")
  endif()
endforeach()

message(STATUS "roundtrip OK")