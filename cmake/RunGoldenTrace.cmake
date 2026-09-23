# Runs atlas at the golden configuration and compares the trace it emits with
# the one checked into tests/golden/, byte for byte.
#
# A script rather than a C++ test because the thing under test is the whole
# program's output, not any one function: the binary has to actually run.

if(NOT ATLAS OR NOT GOLDEN OR NOT OUTPUT)
  message(FATAL_ERROR "RunGoldenTrace.cmake requires -D ATLAS, -D GOLDEN, -D OUTPUT")
endif()

execute_process(
  COMMAND "${ATLAS}" --seed 7 --nodes 4 --jobs 40 --rate 0.05 --trace "${OUTPUT}"
  OUTPUT_QUIET RESULT_VARIABLE run_result)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "atlas exited ${run_result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${GOLDEN}" "${OUTPUT}"
  RESULT_VARIABLE differ)
if(NOT differ EQUAL 0)
  message(FATAL_ERROR
    "trace differs from ${GOLDEN}\n"
    "  diff ${GOLDEN} ${OUTPUT}\n"
    "Read the diff before regenerating. If the change is intended, `make golden-update`.")
endif()
