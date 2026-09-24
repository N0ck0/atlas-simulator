# One executable for the whole suite. Per-module binaries would mean more CMake
# and slower builds for no benefit at this size.
#
# gtest_main supplies main(), so no test file declares one: every TEST registers
# itself with a global registry at static-initialization time, and that main
# just runs whatever registered.
add_executable(atlas_tests
  engine/event_queue_test.cpp
  engine/simulator_test.cpp
  io/compare_test.cpp
  metrics/percentiles_test.cpp
  metrics/utilization_test.cpp
  model/cluster_test.cpp
  model/random_test.cpp
  model/workload_test.cpp
  sched/backfill_test.cpp
  sched/factory_test.cpp
  sched/scheduler_contract_test.cpp
  sched/first_fit_test.cpp
  sched/round_robin_test.cpp
  sched/least_loaded_test.cpp
  sched/resource_aware_test.cpp
)

# atlas_warnings applies to these sources but not to GoogleTest's own, which is
# the reason the warning set is a target rather than global flags.
target_link_libraries(atlas_tests PRIVATE atlas_lib atlas_warnings GTest::gtest_main)

# Registers one ctest case per TEST rather than one for the whole binary, which
# is what makes `ctest -R` filtering and `ctest -j` parallelism work. It runs the
# built binary with --gtest_list_tests to enumerate them.
include(GoogleTest)
gtest_discover_tests(atlas_tests)

# The golden trace: one run's complete output, checked in, compared byte for
# byte. It asserts nothing about what the numbers should be -- only that they
# have not changed -- which is what makes it catch a bug anywhere in the
# program at once, and why a diff here must be read rather than re-blessed.
#
# Determinism across optimization levels falls out of this for free: CI runs
# ctest under debug, release and asan, so the same golden file has to match at
# -O0, -O3 and under sanitizers. That is a stronger check than the two-binary
# comparison it replaces, and it needs no special target.
add_test(NAME golden_trace
  COMMAND ${CMAKE_COMMAND}
          -D "ATLAS=$<TARGET_FILE:atlas>"
          -D "GOLDEN=${CMAKE_CURRENT_SOURCE_DIR}/golden/small.jsonl"
          -D "OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/golden_trace_actual.jsonl"
          -P "${CMAKE_SOURCE_DIR}/cmake/RunGoldenTrace.cmake")
