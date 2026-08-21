# Cross-compatibility regression suite.
#
# Usage:
#   cmake -DZPAQ_NG=<exe> -DZPAQ_ORIG=<orig zpaq exe> -P compat.cmake
#
# Verifies that archives written by the original zpaq are readable/extractable
# by ZPAQ-NG and vice versa, and that identical input+method+date produce
# byte-identical archives.

if(NOT DEFINED ZPAQ_NG)
  message(FATAL_ERROR "ZPAQ_NG not defined")
endif()
if(NOT DEFINED ZPAQ_ORIG OR NOT EXISTS "${ZPAQ_ORIG}")
  message(STATUS "ZPAQ_ORIG not set; skipping original-binary comparisons")
endif()

set(WORK "${CMAKE_CURRENT_BINARY_DIR}/compat_dir")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

# Deterministic inputs: text + "binary" (all byte values 1..255; cmake content
# strings cannot hold a null byte, so byte 0 is exercised by text.dat instead).
set(TXT "")
string(REPEAT "compatibility regression corpus 0123456789\n" 20000 TXT)
file(WRITE "${WORK}/text.dat" "${TXT}")
set(BIN "")
set(BY "")
foreach(i RANGE 1 255)
  string(ASCII ${i} C)
  string(APPEND BY "${C}")
endforeach()
foreach(i RANGE 1 16)
  string(APPEND BIN "${BY}")
endforeach()
file(WRITE "${WORK}/allbytes.bin" "${BIN}")

# --- ng -> orig -------------------------------------------------------------
execute_process(COMMAND "${ZPAQ_NG}" a "ng.zpaq" "text.dat" "allbytes.bin" -m1
    -until 20200101000000
  WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE R)
if(NOT R EQUAL 0)
  message(FATAL_ERROR "ng add failed")
endif()

if(EXISTS "${ZPAQ_ORIG}")
  execute_process(COMMAND "${ZPAQ_ORIG}" x "ng.zpaq" -test
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)
  if(NOT R EQUAL 0)
    message(FATAL_ERROR "original failed to test ng archive")
  endif()
  execute_process(COMMAND "${ZPAQ_ORIG}" x "ng.zpaq" -to "o1" -force
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)
  if(NOT R EQUAL 0)
    message(FATAL_ERROR "original failed to extract ng archive")
  endif()

  # --- orig -> ng -------------------------------------------------------------
  execute_process(COMMAND "${ZPAQ_ORIG}" a "orig.zpaq" "text.dat" "allbytes.bin"
      -m1 -until 20200101000000
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)
  if(NOT R EQUAL 0)
    message(FATAL_ERROR "orig add failed")
  endif()
  execute_process(COMMAND "${ZPAQ_NG}" x "orig.zpaq" -to "o2" -force
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)
  if(NOT R EQUAL 0)
    message(FATAL_ERROR "ng failed to extract orig archive")
  endif()

  # --- byte-identical archives (same input, method, date) ---------------------
  execute_process(COMMAND "${ZPAQ_NG}" a "ng2.zpaq" "text.dat" "allbytes.bin"
      -m1 -until 20200101000000
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE R OUTPUT_QUIET ERROR_QUIET)
  if(NOT R EQUAL 0)
    message(FATAL_ERROR "ng2 add failed")
  endif()
  file(READ "${WORK}/orig.zpaq" HA HEX)
  file(READ "${WORK}/ng2.zpaq" HB HEX)
  if(NOT HA STREQUAL HB)
    message(FATAL_ERROR "archives differ: original vs ZPAQ-NG for -m1")
  endif()
endif()

# --- extracted content equality ----------------------------------------------
foreach(P text.dat allbytes.bin)
  if(EXISTS "${ZPAQ_ORIG}")
    file(READ "${WORK}/${P}" S HEX)
    file(READ "${WORK}/o1/${P}" D HEX)
    if(NOT S STREQUAL D)
      message(FATAL_ERROR "ng->orig mismatch: ${P}")
    endif()
    file(READ "${WORK}/o2/${P}" D HEX)
    if(NOT S STREQUAL D)
      message(FATAL_ERROR "orig->ng mismatch: ${P}")
    endif()
  endif()
endforeach()

message(STATUS "compat suite OK")