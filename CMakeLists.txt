cmake_minimum_required(VERSION 3.25)
project(atlas LANGUAGES CXX VERSION 0.1.0)

# ccache is a build-time convenience, never a requirement, so it is wired in
# only when present rather than declared as a dependency.
find_program(ATLAS_CCACHE ccache)
if(ATLAS_CCACHE)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${ATLAS_CCACHE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -std=c++20 rather than -std=gnu++20. A GNU extension that compiles here but
# not under clang is a portability bug found in CI instead of at the keyboard.
set(CMAKE_CXX_EXTENSIONS OFF)

# Lets clangd and the VS Code C/C++ extension read the real flags instead of
# guessing them.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ATLAS_WERROR "Treat compiler warnings as errors" OFF)

# The warning set lives on one INTERFACE target so that libatlas, the CLI, and
# the test binaries cannot drift apart on it as S2 splits them up. Off by
# default and on in CI: a warning should stop a merge, not a local experiment.
# Flags are GCC/Clang spellings; those are the only two compilers targeted.
add_library(atlas_warnings INTERFACE)
target_compile_options(atlas_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion
  $<$<BOOL:${ATLAS_WERROR}>:-Werror>)

# The simulator core, built once and linked by both the CLI and (from step 2.4)
# the test binaries. A static library rather than an object library so that
# pybind11 can link it as-is in S6.
add_library(atlas_lib STATIC
  src/atlas/engine/simulator.cpp
  src/atlas/io/options.cpp
  src/atlas/io/report.cpp
  src/atlas/io/trace.cpp
  src/atlas/metrics/offered_load.cpp
  src/atlas/metrics/percentiles.cpp
  src/atlas/metrics/time_sample.cpp
  src/atlas/metrics/utilization.cpp)

# PUBLIC so anything linking atlas_lib can find its headers too. Includes are
# written from src/ down -- `#include "atlas/engine/clock.hpp"` -- so a header's
# path names its module and cannot collide with a system header.
target_include_directories(atlas_lib PUBLIC src)
target_link_libraries(atlas_lib PRIVATE atlas_warnings)
set_target_properties(atlas_lib PROPERTIES OUTPUT_NAME atlas)

# The CLI. Thin by design: argument parsing lives in the library, so main() only
# chooses between running the self-checks and running one simulation.
# selfcheck.cpp is transitional, and moves into GoogleTest cases in step 2.5.
add_executable(atlas src/main.cpp src/selfcheck.cpp)
target_link_libraries(atlas PRIVATE atlas_lib atlas_warnings)

# Invariant 4 needs two binaries built at different optimization levels, so it
# cannot be a normal test target. Temporary scaffolding: it becomes a ctest
# case against a checked-in golden trace in step 2.6.
add_custom_target(check-determinism
  COMMAND ${CMAKE_COMMAND}
          -D "CXX=${CMAKE_CXX_COMPILER}"
          -D "SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
          -D "WORKDIR=${CMAKE_CURRENT_BINARY_DIR}/determinism"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckDeterminism.cmake"
  VERBATIM)
