# Same seed must produce a byte-identical trace regardless of optimization
# level. Configures and builds the project twice, at -O0 and -O2, runs both, and
# compares the traces.
#
# It drives CMake rather than invoking the compiler directly so that it tests
# what the build system actually produces, across all of libatlas rather than
# one file. CMAKE_BUILD_TYPE is deliberately empty: Debug and Release would each
# contribute their own -O flag and fight the one being varied here, which is the
# entire point of the check.

if(NOT CXX OR NOT SOURCE_DIR OR NOT WORKDIR)
  message(FATAL_ERROR "CheckDeterminism.cmake requires -D CXX, -D SOURCE_DIR, -D WORKDIR")
endif()

function(atlas_build_and_run opt_level)
  set(build_dir "${WORKDIR}/${opt_level}")

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${build_dir}" -G Ninja
            "-DCMAKE_CXX_COMPILER=${CXX}" "-DCMAKE_BUILD_TYPE=" "-DCMAKE_CXX_FLAGS=-${opt_level}"
    OUTPUT_QUIET RESULT_VARIABLE configure_result)
  if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "configure at -${opt_level} failed")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${build_dir}"
    OUTPUT_QUIET RESULT_VARIABLE build_result)
  if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "build at -${opt_level} failed")
  endif()

  execute_process(
    COMMAND "${build_dir}/atlas" --seed 7 --trace "${WORKDIR}/trace-${opt_level}.jsonl"
    OUTPUT_QUIET RESULT_VARIABLE run_result)
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
