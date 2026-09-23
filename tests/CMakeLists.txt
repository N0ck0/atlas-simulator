# One executable for the whole suite. Per-module binaries would mean more CMake
# and slower builds for no benefit at this size.
#
# gtest_main supplies main(), so no test file declares one: every TEST registers
# itself with a global registry at static-initialization time, and that main
# just runs whatever registered.
add_executable(atlas_tests
  engine/event_queue_test.cpp
  engine/simulator_test.cpp
  metrics/percentiles_test.cpp
  metrics/utilization_test.cpp
  model/cluster_test.cpp
  model/random_test.cpp
  model/workload_test.cpp)

# atlas_warnings applies to these sources but not to GoogleTest's own, which is
# the reason the warning set is a target rather than global flags.
target_link_libraries(atlas_tests PRIVATE atlas_lib atlas_warnings GTest::gtest_main)

# Registers one ctest case per TEST rather than one for the whole binary, which
# is what makes `ctest -R` filtering and `ctest -j` parallelism work. It runs the
# built binary with --gtest_list_tests to enumerate them.
include(GoogleTest)
gtest_discover_tests(atlas_tests)
