# Same seed must produce a byte-identical trace regardless of optimization
# level. Optimization flags are spelled out rather than inherited from the
# build configuration, because varying them is the entire point of the check.

if(NOT CXX OR NOT SOURCE OR NOT WORKDIR)
  message(FATAL_ERROR "CheckDeterminism.cmake requires -D CXX, -D SOURCE, -D WORKDIR")
endif()

file(MAKE_DIRECTORY "${WORKDIR}")

function(atlas_build_and_run opt_level)
  set(binary "${WORKDIR}/atlas-${opt_level}")
  set(trace "${WORKDIR}/trace-${opt_level}.jsonl")

  execute_process(
    COMMAND "${CXX}" -std=c++20 "-${opt_level}" -o "${binary}" "${SOURCE}"
    RESULT_VARIABLE compile_result)
  if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR "compile at -${opt_level} failed")
  endif()

  execute_process(
    COMMAND "${binary}" --seed 7 --trace "${trace}"
    OUTPUT_QUIET
    RESULT_VARIABLE run_result)
  if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "run at -${opt_level} exited ${run_result}")
  endif()
endfunction()

atlas_build_and_run(O0)
atlas_build_and_run(O2)

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${WORKDIR}/trace-O0.jsonl" "${WORKDIR}/trace-O2.jsonl"
  RESULT_VARIABLE differ)
if(NOT differ EQUAL 0)
  message(FATAL_ERROR "FAIL  determinism: trace differs between -O0 and -O2")
endif()

message(STATUS "PASS  determinism: identical trace across -O0 and -O2")
